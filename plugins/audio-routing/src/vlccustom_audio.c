#include <jni.h>
#include <dlfcn.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    ROUTE_STEREO = 0,
    ROUTE_LEFT = 1,
    ROUTE_RIGHT = 2,
};

typedef void (*vlc_audio_play_cb)(void *, const void *, unsigned, int64_t);
typedef void (*vlc_audio_pause_cb)(void *, int64_t);
typedef void (*vlc_audio_flush_cb)(void *, int64_t);
typedef void (*vlc_audio_drain_cb)(void *);
typedef int (*vlc_audio_setup_cb)(void **, char *, unsigned *, unsigned *);
typedef void (*vlc_audio_cleanup_cb)(void *);
typedef int (*vlc_audio_output_set_fn)(void *, const char *);
typedef void (*vlc_audio_set_callbacks_fn)(
    void *, vlc_audio_play_cb, vlc_audio_pause_cb, vlc_audio_pause_cb,
    vlc_audio_flush_cb, vlc_audio_drain_cb, void *);
typedef void (*vlc_audio_set_format_callbacks_fn)(
    void *, vlc_audio_setup_cb, vlc_audio_cleanup_cb);

static JavaVM *java_vm;
static pthread_key_t env_key;
static pthread_once_t env_key_once = PTHREAD_ONCE_INIT;
static jclass audio_track_class;
static jmethodID audio_track_ctor;
static jmethodID audio_track_play;
static jmethodID audio_track_pause;
static jmethodID audio_track_flush;
static jmethodID audio_track_stop;
static jmethodID audio_track_release;
static jmethodID audio_track_write;
static jmethodID audio_track_min_buffer;
static jmethodID audio_track_playback_head;
static atomic_int route_mode = ROUTE_STEREO;
static atomic_ullong rendered_samples;
static atomic_bool output_meter_enabled;
static atomic_ullong output_energy_left;
static atomic_ullong output_energy_right;
static atomic_ullong output_frames;
static atomic_ullong output_meter_generation;
static pthread_mutex_t output_meter_lock = PTHREAD_MUTEX_INITIALIZER;

static void *libvlc_handle;
static vlc_audio_set_callbacks_fn vlc_set_callbacks;
static vlc_audio_set_format_callbacks_fn vlc_set_format_callbacks;
static vlc_audio_output_set_fn vlc_set_output;

typedef struct audio_state {
    struct audio_state *next;
    uintptr_t player;
    pthread_mutex_t lock;
    jobject track;
    unsigned rate;
    bool playing;
    unsigned fade_remaining;
    unsigned fade_total;
    int previous_mode;
    int current_mode;
} audio_state_t;

static pthread_mutex_t states_lock = PTHREAD_MUTEX_INITIALIZER;
static audio_state_t *audio_states;

static audio_state_t *
get_audio_state(uintptr_t player)
{
    pthread_mutex_lock(&states_lock);
    audio_state_t *state = audio_states;
    while (state != NULL && state->player != player)
        state = state->next;
    if (state == NULL) {
        state = calloc(1, sizeof(*state));
        if (state != NULL) {
            state->player = player;
            pthread_mutex_init(&state->lock, NULL);
            state->next = audio_states;
            audio_states = state;
        }
    }
    pthread_mutex_unlock(&states_lock);
    return state;
}

static void
detach_thread(void *data)
{
    JNIEnv *env = data;
    JavaVM *jvm;
    if (env != NULL && (*env)->GetJavaVM(env, &jvm) == JNI_OK)
        (*jvm)->DetachCurrentThread(jvm);
}

static void
create_env_key(void)
{
    pthread_key_create(&env_key, detach_thread);
}

static JNIEnv *
get_env(void)
{
    if (java_vm == NULL)
        return NULL;

    pthread_once(&env_key_once, create_env_key);
    JNIEnv *env = pthread_getspecific(env_key);
    if (env != NULL)
        return env;

    if ((*java_vm)->GetEnv(java_vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) {
        JavaVMAttachArgs args = {
            .version = JNI_VERSION_1_6,
            .name = "vlccustom-audio",
            .group = NULL,
        };
        if ((*java_vm)->AttachCurrentThread(java_vm, &env, &args) != JNI_OK)
            return NULL;
        pthread_setspecific(env_key, env);
    }
    return env;
}

static bool
ensure_libvlc(void)
{
    if (libvlc_handle == NULL)
        libvlc_handle = dlopen("libvlc.so", RTLD_NOW | RTLD_LOCAL);
    if (libvlc_handle == NULL)
        return false;

    if (vlc_set_callbacks == NULL)
        vlc_set_callbacks = (vlc_audio_set_callbacks_fn)
            dlsym(libvlc_handle, "libvlc_audio_set_callbacks");
    if (vlc_set_format_callbacks == NULL)
        vlc_set_format_callbacks = (vlc_audio_set_format_callbacks_fn)
            dlsym(libvlc_handle, "libvlc_audio_set_format_callbacks");
    if (vlc_set_output == NULL)
        vlc_set_output = (vlc_audio_output_set_fn)
            dlsym(libvlc_handle, "libvlc_audio_output_set");
    return vlc_set_callbacks != NULL && vlc_set_format_callbacks != NULL;
}

static void
release_track(JNIEnv *env, audio_state_t *state)
{
    state->playing = false;
    if (state->track != NULL) {
        (*env)->CallVoidMethod(env, state->track, audio_track_stop);
        (*env)->CallVoidMethod(env, state->track, audio_track_flush);
        (*env)->CallVoidMethod(env, state->track, audio_track_release);
        (*env)->DeleteGlobalRef(env, state->track);
        state->track = NULL;
    }
}

static bool
create_track(JNIEnv *env, audio_state_t *state, unsigned rate)
{
    const jint channel_config = 12; /* AudioFormat.CHANNEL_OUT_STEREO */
    const jint encoding = 2;        /* AudioFormat.ENCODING_PCM_16BIT */
    const jint stream = 3;          /* AudioManager.STREAM_MUSIC */
    jint minimum = (*env)->CallStaticIntMethod(
        env, audio_track_class, audio_track_min_buffer, (jint)rate,
        channel_config, encoding);
    if ((*env)->ExceptionCheck(env) || minimum <= 0) {
        (*env)->ExceptionClear(env);
        return false;
    }

    jobject track = (*env)->NewObject(
        env, audio_track_class, audio_track_ctor, stream, (jint)rate,
        channel_config, encoding, minimum * 2, 1 /* MODE_STREAM */, 0);
    if ((*env)->ExceptionCheck(env) || track == NULL) {
        (*env)->ExceptionClear(env);
        return false;
    }

    state->track = (*env)->NewGlobalRef(env, track);
    (*env)->DeleteLocalRef(env, track);
    if (state->track == NULL)
        return false;

    state->rate = rate;
    (*env)->CallVoidMethod(env, state->track, audio_track_play);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        release_track(env, state);
        return false;
    }
    state->playing = true;
    return true;
}

static bool
ensure_track_playing(JNIEnv *env, audio_state_t *state)
{
    if (state->track == NULL) {
        const unsigned rate = state->rate == 0 ? 48000 : state->rate;
        if (!create_track(env, state, rate))
            return false;
    }
    if (!state->playing) {
        (*env)->CallVoidMethod(env, state->track, audio_track_play);
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            return false;
        }
        state->playing = true;
    }
    return true;
}

static int
setup_audio(void **data, char *format, unsigned *rate, unsigned *channels)
{
    audio_state_t *state = *data;
    JNIEnv *env = get_env();
    if (state == NULL || env == NULL)
        return -1;

    memcpy(format, "S16N", 4);
    *channels = 2;

    pthread_mutex_lock(&state->lock);
    release_track(env, state);
    bool created = create_track(env, state, *rate);
    if (!created && *rate != 48000) {
        *rate = 48000;
        created = create_track(env, state, 48000);
    }
    if (created) {
        state->current_mode = atomic_load_explicit(
            &route_mode, memory_order_acquire);
        state->previous_mode = state->current_mode;
        state->fade_remaining = 0;
        state->fade_total = 0;
    }
    pthread_mutex_unlock(&state->lock);
    return created ? 0 : -1;
}

static void
cleanup_audio(void *data)
{
    audio_state_t *state = data;
    JNIEnv *env = get_env();
    if (state == NULL || env == NULL)
        return;

    pthread_mutex_lock(&state->lock);
    release_track(env, state);
    pthread_mutex_unlock(&state->lock);
}

static void
render_mode(int16_t left, int16_t right, int mode, float output[2])
{
    if (mode == ROUTE_STEREO) {
        output[0] = left;
        output[1] = right;
        return;
    }

    const float mono = (left + right) * 0.5f;
    output[0] = mode == ROUTE_LEFT ? mono : 0.f;
    output[1] = mode == ROUTE_RIGHT ? mono : 0.f;
}

static int16_t
clip_sample(float sample)
{
    if (sample > 32767.f)
        return 32767;
    if (sample < -32768.f)
        return -32768;
    return (int16_t)lrintf(sample);
}

static void
play_audio(void *data, const void *samples, unsigned count, int64_t pts)
{
    audio_state_t *state = data;
    JNIEnv *env = get_env();
    if (state == NULL || env == NULL || count == 0)
        return;

    pthread_mutex_lock(&state->lock);
    if (!ensure_track_playing(env, state)) {
        pthread_mutex_unlock(&state->lock);
        return;
    }

    const int requested_mode = atomic_load_explicit(
        &route_mode, memory_order_acquire);
    if (requested_mode != state->current_mode) {
        state->previous_mode = state->current_mode;
        state->current_mode = requested_mode;
        state->fade_total = state->rate * 20u / 1000u;
        if (state->fade_total == 0)
            state->fade_total = 1;
        state->fade_remaining = state->fade_total;
    }

    const size_t byte_count = (size_t)count * 2u * sizeof(int16_t);
    jbyteArray array = (*env)->NewByteArray(env, (jsize)byte_count);
    if (array == NULL) {
        (*env)->ExceptionClear(env);
        pthread_mutex_unlock(&state->lock);
        return;
    }

    jbyte *destination = (*env)->GetByteArrayElements(env, array, NULL);
    if (destination == NULL) {
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, array);
        pthread_mutex_unlock(&state->lock);
        return;
    }

    const int16_t *source = samples;
    int16_t *output = (int16_t *)destination;
    const uint64_t meter_generation = atomic_load_explicit(
        &output_meter_generation, memory_order_acquire);
    bool meter_enabled = atomic_load_explicit(
        &output_meter_enabled, memory_order_acquire);
    bool meter_locked = false;
    if (meter_enabled) {
        pthread_mutex_lock(&output_meter_lock);
        meter_locked = true;
        meter_enabled = atomic_load_explicit(
            &output_meter_enabled, memory_order_acquire)
            && meter_generation == atomic_load_explicit(
                &output_meter_generation, memory_order_acquire);
    }
    uint64_t energy_left = 0;
    uint64_t energy_right = 0;
    bool fading = state->fade_remaining > 0;
    for (unsigned frame = 0; frame < count; ++frame) {
        const int16_t left = source[frame * 2];
        const int16_t right = source[frame * 2 + 1];
        if (!fading) {
            float rendered[2];
            render_mode(left, right, state->current_mode, rendered);
            output[frame * 2] = clip_sample(rendered[0]);
            output[frame * 2 + 1] = clip_sample(rendered[1]);
        } else {
            float old_rendered[2];
            float new_rendered[2];
            render_mode(left, right, state->previous_mode, old_rendered);
            render_mode(left, right, state->current_mode, new_rendered);
            const float alpha = (state->fade_total
                               - state->fade_remaining + 1.f)
                              / state->fade_total;
            output[frame * 2] = clip_sample(
                old_rendered[0] + (new_rendered[0] - old_rendered[0]) * alpha);
            output[frame * 2 + 1] = clip_sample(
                old_rendered[1] + (new_rendered[1] - old_rendered[1]) * alpha);
            if (--state->fade_remaining == 0)
                state->previous_mode = state->current_mode;
        }
        if (meter_enabled) {
            const int32_t rendered_left = output[frame * 2];
            const int32_t rendered_right = output[frame * 2 + 1];
            energy_left += (uint64_t)(rendered_left * rendered_left);
            energy_right += (uint64_t)(rendered_right * rendered_right);
        }
    }

    (*env)->ReleaseByteArrayElements(env, array, destination, 0);
    (*env)->CallIntMethod(env, state->track, audio_track_write, array, 0,
                          (jint)byte_count);
    if ((*env)->ExceptionCheck(env))
        (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, array);
    pthread_mutex_unlock(&state->lock);
    if (meter_enabled) {
        atomic_fetch_add_explicit(
            &output_energy_left, energy_left, memory_order_relaxed);
        atomic_fetch_add_explicit(
            &output_energy_right, energy_right, memory_order_relaxed);
        atomic_fetch_add_explicit(
            &output_frames, count, memory_order_relaxed);
    }
    if (meter_locked) {
        pthread_mutex_unlock(&output_meter_lock);
    }
    atomic_fetch_add_explicit(&rendered_samples, count, memory_order_relaxed);
    (void)pts;
}

static void
pause_audio(void *data, int64_t pts)
{
    audio_state_t *state = data;
    JNIEnv *env = get_env();
    if (state != NULL && env != NULL) {
        pthread_mutex_lock(&state->lock);
        if (state->track != NULL) {
            (*env)->CallVoidMethod(env, state->track, audio_track_pause);
            state->playing = false;
        }
        pthread_mutex_unlock(&state->lock);
    }
    (void)pts;
}

static void
resume_audio(void *data, int64_t pts)
{
    audio_state_t *state = data;
    JNIEnv *env = get_env();
    if (state != NULL && env != NULL) {
        pthread_mutex_lock(&state->lock);
        if (state->track != NULL) {
            (*env)->CallVoidMethod(env, state->track, audio_track_play);
            state->playing = true;
        }
        pthread_mutex_unlock(&state->lock);
    }
    (void)pts;
}

static void
flush_audio(void *data, int64_t pts)
{
    audio_state_t *state = data;
    JNIEnv *env = get_env();
    if (state != NULL && env != NULL) {
        pthread_mutex_lock(&state->lock);
        if (state->track != NULL) {
            (*env)->CallVoidMethod(env, state->track, audio_track_pause);
            (*env)->CallVoidMethod(env, state->track, audio_track_flush);
            state->playing = false;
        }
        pthread_mutex_unlock(&state->lock);
    }
    (void)pts;
}

static void
drain_audio(void *data)
{
    audio_state_t *state = data;
    JNIEnv *env = get_env();
    if (state != NULL && env != NULL) {
        pthread_mutex_lock(&state->lock);
        if (state->track != NULL) {
            (*env)->CallVoidMethod(env, state->track, audio_track_stop);
            state->playing = false;
        }
        pthread_mutex_unlock(&state->lock);
    }
}

JNIEXPORT jboolean JNICALL
Java_org_videolan_vlc_audio_AudioRouting_nativePrepareAudioOutput(
    JNIEnv *env, jclass clazz, jlong media_player)
{
    (void)env;
    (void)clazz;
    if (media_player == 0 || !ensure_libvlc())
        return JNI_FALSE;

    pthread_once(&env_key_once, create_env_key);
    audio_state_t *state = get_audio_state((uintptr_t)media_player);
    if (state == NULL)
        return JNI_FALSE;

    vlc_set_callbacks((void *)(intptr_t)media_player, play_audio,
                      pause_audio, resume_audio, flush_audio, drain_audio,
                      state);
    vlc_set_format_callbacks((void *)(intptr_t)media_player,
                             setup_audio, cleanup_audio);
    if (vlc_set_output != NULL)
        vlc_set_output((void *)(intptr_t)media_player, "amem");
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_org_videolan_vlc_audio_AudioRouting_nativeSetRouteMode(
    JNIEnv *env, jclass clazz, jint mode)
{
    (void)env;
    (void)clazz;
    if (mode < ROUTE_STEREO || mode > ROUTE_RIGHT)
        return JNI_FALSE;
    atomic_store_explicit(&route_mode, mode, memory_order_release);
    return JNI_TRUE;
}

JNIEXPORT jint JNICALL
Java_org_videolan_vlc_audio_AudioRouting_nativeGetRouteMode(
    JNIEnv *env, jclass clazz)
{
    (void)env;
    (void)clazz;
    return atomic_load_explicit(&route_mode, memory_order_acquire);
}

JNIEXPORT jlong JNICALL
Java_org_videolan_vlc_audio_AudioRouting_nativeGetRenderedSamples(
    JNIEnv *env, jclass clazz)
{
    (void)env;
    (void)clazz;
    return (jlong)atomic_load_explicit(&rendered_samples,
                                       memory_order_acquire);
}

JNIEXPORT void JNICALL
Java_org_videolan_vlc_audio_AudioRouting_nativeResetOutputMeterForTest(
    JNIEnv *env, jclass clazz)
{
    (void)env;
    (void)clazz;
    pthread_mutex_lock(&output_meter_lock);
    atomic_store_explicit(&output_meter_enabled, false, memory_order_release);
    atomic_store_explicit(&output_energy_left, 0, memory_order_relaxed);
    atomic_store_explicit(&output_energy_right, 0, memory_order_relaxed);
    atomic_store_explicit(&output_frames, 0, memory_order_relaxed);
    atomic_fetch_add_explicit(
        &output_meter_generation, 1, memory_order_release);
    atomic_store_explicit(&output_meter_enabled, true, memory_order_release);
    pthread_mutex_unlock(&output_meter_lock);
}

JNIEXPORT jlong JNICALL
Java_org_videolan_vlc_audio_AudioRouting_nativeGetOutputEnergyLeftForTest(
    JNIEnv *env, jclass clazz)
{
    (void)env;
    (void)clazz;
    return (jlong)atomic_load_explicit(
        &output_energy_left, memory_order_relaxed);
}

JNIEXPORT jlong JNICALL
Java_org_videolan_vlc_audio_AudioRouting_nativeGetOutputEnergyRightForTest(
    JNIEnv *env, jclass clazz)
{
    (void)env;
    (void)clazz;
    return (jlong)atomic_load_explicit(
        &output_energy_right, memory_order_relaxed);
}

JNIEXPORT jlong JNICALL
Java_org_videolan_vlc_audio_AudioRouting_nativeGetOutputFramesForTest(
    JNIEnv *env, jclass clazz)
{
    (void)env;
    (void)clazz;
    return (jlong)atomic_load_explicit(
        &output_frames, memory_order_relaxed);
}

JNIEXPORT jboolean JNICALL
Java_org_videolan_vlc_audio_AudioRouting_nativeIsTrackPlayingForTest(
    JNIEnv *env, jclass clazz, jlong media_player)
{
    (void)env;
    (void)clazz;
    audio_state_t *state = get_audio_state((uintptr_t)media_player);
    pthread_mutex_lock(&state->lock);
    const jboolean playing = state->playing ? JNI_TRUE : JNI_FALSE;
    pthread_mutex_unlock(&state->lock);
    return playing;
}

JNIEXPORT jlong JNICALL
Java_org_videolan_vlc_audio_AudioRouting_nativeGetPlaybackHeadPositionForTest(
    JNIEnv *env, jclass clazz, jlong media_player)
{
    (void)clazz;
    audio_state_t *state = get_audio_state((uintptr_t)media_player);
    pthread_mutex_lock(&state->lock);
    if (state->track == NULL) {
        pthread_mutex_unlock(&state->lock);
        return -1;
    }
    const jint position = (*env)->CallIntMethod(
        env, state->track, audio_track_playback_head);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        pthread_mutex_unlock(&state->lock);
        return -1;
    }
    pthread_mutex_unlock(&state->lock);
    return (jlong)(uint32_t)position;
}

JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM *vm, void *reserved)
{
    (void)reserved;
    java_vm = vm;
    JNIEnv *env;
    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK)
        return JNI_ERR;

    jclass local_class = (*env)->FindClass(env, "android/media/AudioTrack");
    if (local_class == NULL)
        return JNI_ERR;
    audio_track_class = (*env)->NewGlobalRef(env, local_class);
    (*env)->DeleteLocalRef(env, local_class);

    audio_track_ctor = (*env)->GetMethodID(
        env, audio_track_class, "<init>", "(IIIIIII)V");
    audio_track_play = (*env)->GetMethodID(env, audio_track_class, "play", "()V");
    audio_track_pause = (*env)->GetMethodID(env, audio_track_class, "pause", "()V");
    audio_track_flush = (*env)->GetMethodID(env, audio_track_class, "flush", "()V");
    audio_track_stop = (*env)->GetMethodID(env, audio_track_class, "stop", "()V");
    audio_track_release = (*env)->GetMethodID(
        env, audio_track_class, "release", "()V");
    audio_track_write = (*env)->GetMethodID(
        env, audio_track_class, "write", "([BII)I");
    audio_track_min_buffer = (*env)->GetStaticMethodID(
        env, audio_track_class, "getMinBufferSize", "(III)I");
    audio_track_playback_head = (*env)->GetMethodID(
        env, audio_track_class, "getPlaybackHeadPosition", "()I");

    if (audio_track_ctor == NULL || audio_track_play == NULL
            || audio_track_pause == NULL || audio_track_flush == NULL
            || audio_track_stop == NULL || audio_track_release == NULL
            || audio_track_write == NULL || audio_track_min_buffer == NULL
            || audio_track_playback_head == NULL)
        return JNI_ERR;
    return JNI_VERSION_1_6;
}
