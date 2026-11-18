# 修改文件清单

## 核心音频

- `application/vlc-android/src/org/videolan/vlc/audio/AudioRouting.java`
  - 三模式枚举、SharedPreferences 持久化、JNI 桥接。
  - 按 `MediaPlayer` 实例防止重复安装回调。
- `plugins/audio-routing/src/vlccustom_audio.c`
  - LibVLC PCM 回调、下混信号定向、20 ms 淡变。
  - Android AudioTrack 创建和控制。
  - 每播放器 native 状态、原子模式切换。
- `plugins/audio-routing/build.ps1`
  - 从源码独立构建四种 ABI 的 `libvlccustom_audio.so`。
- `application/app/src/main/jniLibs/{arm64-v8a,armeabi-v7a,x86,x86_64}/libvlccustom_audio.so`
  - native 构建产物。

## 播放器集成

- `application/vlc-android/src/org/videolan/vlc/media/PlayerController.kt`
  - 新建播放器时在 `play()` 前安装回调。
  - 每个新的播放会话开始前重新安装回调，修复停止后重播和小窗恢复无声。
  - 播放事件中恢复持久化模式；失败时回退立体声。
- `application/app/src/main/java/org/videolan/mobile/app/AppSetupDelegate.kt`
  - 应用启动时恢复 native 模式值。
- `application/vlc-android/src/org/videolan/vlc/gui/helpers/PlayerOptionsDelegate.kt`
  - 高级播放工具菜单入口和三模式单选框。
  - 数字直通兼容处理及原设置恢复。

## 并行播放设置

- `application/tools/src/main/java/org/videolan/tools/Settings.kt`
  - 新增 `ALLOW_PARALLEL_AUDIO_PLAYBACK` 设置键。
- `application/vlc-android/res/xml/preferences_audio.xml`
  - 在“设置 → 音频”增加默认关闭的复选框。
- `application/resources/src/main/res/values/strings.xml`
  - 新增英文标题和说明。
- `application/resources/src/main/res/values-zh-rCN/strings.xml`
  - 新增中文标题和说明。
- `application/vlc-android/src/org/videolan/vlc/util/VLCAudioFocusHelper.kt`
  - 启用后跳过 AudioFocus 请求并释放已有焦点。
  - 忽略 loss、transient loss 和 duck 回调，恢复可能已降低的音量。
- `application/vlc-android/src/org/videolan/vlc/PlaybackService.kt`
  - 新增 `refreshAudioFocus()`。
- `application/vlc-android/src/org/videolan/vlc/gui/preferences/PreferencesAudio.kt`
  - 监听开关变化并立即刷新当前播放的焦点状态。

## 应用标识

- `build.gradle`
  - `appId = "org.videolan.vlccustom"`。
- `application/vlc-android/AndroidManifest.xml`
  - 扩展数据权限使用 `${applicationId}`。
- `application/app/flavors/release/AndroidManifest.xml`
  - Release 自定义权限使用 `${applicationId}`。
- `application/resources/src/main/res/values*/strings.xml`
  - `app_name_full` 改为 `vlccustom`。
  - 默认资源和中文资源的 `app_name` 改为 `vlccustom`。
  - 新增三种音频输出模式、标题和失败提示字符串。

## 测试

- `application/app/src/androidTest/java/org/videolan/vlc/audio/AudioRoutingInstrumentedTest.kt`
  - 播放中切换、左右输出能量、暂停/恢复、seek、快速连续切换。
  - 单声道、WAV、FLAC、MP3、Opus、AAC、5.1、7.1。
  - 多音轨切换、设置持久化、供系统输出采集的分阶段用例。

## 文档

- `docs/架构分析.md`
- `docs/构建与测试.md`
- `docs/修改清单.md`
- `docs/RELEASE.md`
- `README.md`
- `CHANGELOG.md`
- `CONTRIBUTING.md`
- `.github/workflows/android-ci.yml`
