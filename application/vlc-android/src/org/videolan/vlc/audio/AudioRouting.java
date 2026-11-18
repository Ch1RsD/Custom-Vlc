/*
 * vlccustom audio routing bridge.
 *
 * LibVLC supplies post-processed PCM through its public audio callbacks.
 * The native bridge mixes and routes those samples into an application-owned
 * Android AudioTrack. Mode changes take effect on the next PCM block.
 */
package org.videolan.vlc.audio;

import android.content.Context;
import android.util.Log;

import androidx.annotation.NonNull;
import androidx.annotation.StringRes;
import androidx.annotation.VisibleForTesting;

import org.videolan.tools.Settings;
import org.videolan.libvlc.MediaPlayer;
import org.videolan.vlc.R;

public final class AudioRouting {
    private static final String TAG = "VLC/AudioRouting";
    public static final String KEY_AUDIO_ROUTE = "audio_route_mode";

    public enum Route {
        STEREO(0, R.string.audio_route_stereo),
        LEFT(1, R.string.audio_route_left),
        RIGHT(2, R.string.audio_route_right);

        public final int nativeMode;
        @StringRes public final int labelRes;

        Route(int nativeMode, @StringRes int labelRes) {
            this.nativeMode = nativeMode;
            this.labelRes = labelRes;
        }

        @NonNull
        public static Route fromNative(int mode) {
            for (Route route : values()) {
                if (route.nativeMode == mode) return route;
            }
            return STEREO;
        }
    }

    static {
        System.loadLibrary("vlccustom_audio");
    }

    private AudioRouting() {}

    @NonNull
    public static Route getSavedRoute(@NonNull Context context) {
        int mode = Settings.INSTANCE.getInstance(context)
                .getInt(KEY_AUDIO_ROUTE, Route.STEREO.nativeMode);
        return Route.fromNative(mode);
    }

    public static synchronized boolean preparePlayer(@NonNull MediaPlayer player) {
        boolean prepared = nativePrepareAudioOutput(player.getInstance());
        boolean outputSelected = player.setAudioOutput("amem");
        Log.d(TAG, "Audio callback output prepared=" + prepared
                + ", amem selected=" + outputSelected);
        return prepared && outputSelected;
    }

    public static boolean applyRoute(@NonNull Context context,
                                     @NonNull MediaPlayer player,
                                     @NonNull Route route) {
        return nativeSetRouteMode(route.nativeMode);
    }

    public static void saveRoute(@NonNull Context context, @NonNull Route route) {
        Settings.INSTANCE.getInstance(context).edit()
                .putInt(KEY_AUDIO_ROUTE, route.nativeMode)
                .apply();
    }

    public static void restore(@NonNull Context context) {
        nativeSetRouteMode(getSavedRoute(context).nativeMode);
    }

    @NonNull
    public static Route getNativeRoute() {
        return Route.fromNative(nativeGetRouteMode());
    }

    public static long getRenderedSamples() {
        return nativeGetRenderedSamples();
    }

    @VisibleForTesting
    static void resetOutputMeterForTest() {
        nativeResetOutputMeterForTest();
    }

    @VisibleForTesting
    static long getOutputEnergyLeftForTest() {
        return nativeGetOutputEnergyLeftForTest();
    }

    @VisibleForTesting
    static long getOutputEnergyRightForTest() {
        return nativeGetOutputEnergyRightForTest();
    }

    @VisibleForTesting
    static long getOutputFramesForTest() {
        return nativeGetOutputFramesForTest();
    }

    @VisibleForTesting
    static boolean isTrackPlayingForTest(@NonNull MediaPlayer player) {
        return nativeIsTrackPlayingForTest(player.getInstance());
    }

    @VisibleForTesting
    static long getPlaybackHeadPositionForTest(@NonNull MediaPlayer player) {
        return nativeGetPlaybackHeadPositionForTest(player.getInstance());
    }

    private static native boolean nativeSetRouteMode(int mode);
    private static native int nativeGetRouteMode();
    private static native long nativeGetRenderedSamples();
    private static native boolean nativePrepareAudioOutput(long mediaPlayer);
    private static native void nativeResetOutputMeterForTest();
    private static native long nativeGetOutputEnergyLeftForTest();
    private static native long nativeGetOutputEnergyRightForTest();
    private static native long nativeGetOutputFramesForTest();
    private static native boolean nativeIsTrackPlayingForTest(long mediaPlayer);
    private static native long nativeGetPlaybackHeadPositionForTest(long mediaPlayer);
}
