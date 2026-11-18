# vlccustom 架构分析

## 1. 基线

- 上游项目：VLC for Android 3.6.3
- 上游标签：`3.6.3`
- 上游提交：`13b29e8`（`Bump the app to 3.6.3`）
- Android `applicationId`：`org.videolan.vlccustom`
- Debug `applicationId`：`org.videolan.vlccustom.debug`
- 应用显示名：`vlccustom`
- `compileSdk` / `targetSdk`：34
- `minSdk`：17
- AGP：8.5.0
- Gradle：8.9
- Kotlin：1.9.22
- JDK：17
- LibVLC Android 依赖：`org.videolan.android:libvlc-all:3.6.0`

应用版本是 VLC Android 3.6.3，LibVLC Android AAR 版本是 3.6.0。运行时日志中底层 `libvlc` 自报的核心引擎版本为 3.0.21，这是该 LibVLC 3.6.0 AAR 所携带的 VLC 3.x 引擎。

## 2. 总体架构

VLC Android 仍保留原有分层：

1. Java/Kotlin UI 层：`MainActivity`、`VideoPlayerActivity`、`PlayerOptionsDelegate` 等。
2. `PlaybackService`：MediaSession、播放队列、音轨与播放控制。
3. `PlayerController` / `PlaylistManager`：创建、持有和驱动 `org.videolan.libvlc.MediaPlayer`。
4. LibVLC JNI 与原生 `libvlc.so`：解复用、解码、视频输出、音频滤镜和音频输出。
5. Android 系统音频：AudioFlinger、AudioTrack、系统音量与输出设备路由。

视频解码和渲染链路未修改。定制工作集中在 LibVLC 音频输出边界和播放器工具菜单。

## 3. 原始音频链路

原始链路为：

```text
媒体输入
  -> demux
  -> audio decoder
  -> aout_filters（重采样、格式转换、倍速、音效等）
  -> VLC 声道布局与下混
  -> 原始 Android AudioTrack 输出
```

多声道输入会在 LibVLC 的音频输出前按声道布局完成下混。对于 5.1/7.1，VLC 会使用其已有混音矩阵处理前置、中置、环绕和 LFE，而不是只截取前两个原始声道。

## 4. 采用的定制切入点

LibVLC Android AAR 以 `--disable-shared` 构建，原生模块是静态链接的，因此不能通过向 APK 添加新的 `_plugin.so` 动态加载自定义 VLC 音频输出插件。

本实现使用 LibVLC 公开 API：

- `libvlc_audio_set_format_callbacks()`
- `libvlc_audio_set_callbacks()`

在 `player.play()` 之前安装回调。LibVLC 继续执行原有解码、滤镜、重采样和声道下混，随后把处理后的 PCM 交给应用：

```text
VLC 解码/滤镜/下混
  -> 回调格式请求 S16N、48 kHz 或原建议采样率、2 channels
  -> native 混音与定向
  -> 应用自有 Android AudioTrack
  -> AudioFlinger / 当前系统输出设备
```

日志中对应音频输出模块为 `amem`，说明 LibVLC 的原生内存音频输出已被应用回调接管。

## 5. PCM 处理

回调收到的数据为交错双声道 S16N：

```text
M = (L + R) / 2

STEREO: outL = L, outR = R
LEFT:   outL = M, outR = 0
RIGHT:  outL = 0, outR = M
```

对 LibVLC 已经下混后的双声道信号执行 `(L+R)/2`，等价于把最终立体声混音中的全部可听内容合并为单声道。使用平均而不是直接相加可避免常见满幅信号产生额外削波。

模式值通过 C11 原子变量传递。播放中的模式切换在下一个 PCM 包生效，并执行 20 ms 线性交叉淡化。切换不重建播放器、不暂停视频、不修改时间轴和播放速率。

AudioTrack 使用双声道 16-bit PCM 和 `STREAM_MUSIC`，因此继续遵循 Android 媒体音量和系统输出设备选择。

## 6. 生命周期

- `PlayerController.newMediaPlayer()` 在播放前调用 `AudioRouting.preparePlayer()`。
- 每个新的播放会话开始前都会重新安装回调；LibVLC 在 `stop()` 后会重置音频输出。
- Java 包装层与 native 层的音频输出会同步设置为 `amem`，避免包装层在下次 `play()` 时覆盖回调配置。
- native 层按播放器地址保存独立状态，避免 Java 对象释放后 native 地址复用导致误判。
- `setup_audio()` 创建或替换 AudioTrack；`pause`、`resume`、`flush`、`drain` 分别映射到音轨控制。
- seek/`flush` 后，下一个 PCM 包会自动重新调用 AudioTrack `play()`；音轨已被释放时按记住的采样率重建。
- 若 AudioTrack 创建失败，LibVLC 会跳过该音频输出，视频链路仍可继续。

## 7. UI 与状态

入口位于播放器“高级选项/工具”列表：

- `Audio output: Stereo`
- `Audio output: Left only`
- `Audio output: Right only`

对话框为互斥单选。当前模式显示在菜单项标题中，并在每次打开时重新读取。

设置键为 `audio_route_mode`，保存于应用自己的 SharedPreferences：

- `0`：立体声
- `1`：仅左声道
- `2`：仅右声道

定向模式会临时禁用数字直通，以确保音频以 PCM 形式进入混音回调；原数字直通状态会备份，切回立体声后恢复。定向模式期间直通菜单项隐藏，避免再次开启后绕过回调。

## 8. 独立应用标识

- Gradle `namespace` 仍为 `org.videolan.vlc`，源码包名和 LibVLC 命名空间没有做无意义的全局替换。
- `applicationId` 改为 `org.videolan.vlccustom` 后，所有以 `${applicationId}` 为基础的 Provider authority、权限、Intent action 和 Deep Link 自动隔离。
- 自定义扩展权限也改为 `${applicationId}.permission.*`，Debug 与 Release 各自使用独立包名前缀。
- 原版 VLC 使用 `org.videolan.vlc`，与本项目安装、数据目录、设置和权限均独立。

## 9. 与其他应用同时播放

该功能由设置项“设置 → 音频 → 与其他应用同时播放”控制，设置键为
`allow_parallel_audio_playback`，默认关闭。

启用后，`VLCAudioFocusHelper` 不再请求 Android AudioFocus。若播放器当前仍持有焦点，
切换设置时会立即释放焦点，并忽略随后的 `AUDIOFOCUS_LOSS`、
`AUDIOFOCUS_LOSS_TRANSIENT` 和 duck 请求；如果此前已经因 duck 降低了音量，则先恢复原音量。
因此其他音视频应用启动并获得 AudioFocus 时，不会通过焦点回调暂停 vlccustom。

设置变化由 `PreferencesAudio` 监听，并调用 `PlaybackService.refreshAudioFocus()`，
无需重新打开视频即可刷新当前焦点状态。关闭该设置后恢复 VLC 原有的焦点申请、暂停、恢复和
duck 逻辑。

此功能只绕过应用层 Android AudioFocus。电话、系统级暂停、厂商音频策略以及其他应用自身的
暂停策略仍可能影响最终表现。
