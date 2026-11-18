# vlccustom 构建与测试

## 1. 构建环境

| 项目 | 版本/位置 |
|---|---|
| 上游源码 | VLC Android 3.6.3，提交 `13b29e8` |
| JDK | 17.0.14+7 |
| Gradle | 8.9 |
| AGP | 8.5.0 |
| Kotlin | 1.9.22 |
| Android SDK | compile/target 34 |
| Android NDK | r27c |
| LibVLC Android | 3.6.0 |

Android Gradle Plugin 不接受工作目录中的部分非 ASCII 字符。本次实际构建使用 ASCII 工作树，并将 `local.properties` 指向本机 Android SDK。复现时应使用纯 ASCII 的源码路径。

阶段一还在未修改源码的 3.6.3 基线上完成了独立构建：

- 结果：`BUILD SUCCESSFUL in 6m 20s`
- 原始 Debug 通用 APK：`VLC-Android-3.6.3-debug-all.apk`
- 原始 APK 大小：126,753,657 bytes
- 原始 APK SHA-256：`E6DB698DA218AF6A03C686C8F1B1B805FD6E4AC19305C704D36CAD1495CE3EC8`

## 2. 构建 native 桥

```powershell
.\plugins\audio-routing\build.ps1 -NdkRoot <NDK路径>
```

脚本为以下 ABI 生成 `libvlccustom_audio.so`：

- `arm64-v8a`
- `armeabi-v7a`
- `x86`
- `x86_64`

输出目录：

```text
application/app/src/main/jniLibs/<ABI>/
```

## 3. 构建 APK

环境变量示例：

```powershell
$env:JAVA_HOME = '<JDK17>'
$env:Path = "$env:JAVA_HOME\bin;<Gradle8.9>\bin;$env:Path"
```

Debug 与测试 APK：

```powershell
gradle `
  --gradle-user-home <Gradle缓存目录> `
  --no-daemon --console=plain `
  :application:app:assembleDebug `
  :application:app:assembleDebugAndroidTest
```

Release：

```powershell
gradle `
  --gradle-user-home <Gradle缓存目录> `
  --no-daemon --console=plain `
  :application:app:assembleRelease
```

Release 构建成功，Gradle 报告：

```text
BUILD SUCCESSFUL in 12m 9s
367 actionable tasks: 367 executed
```

加入最终直通恢复和低延迟缓冲修正后的增量重建结果为：

```text
BUILD SUCCESSFUL in 6m 19s
624 actionable tasks: 80 executed, 544 up-to-date
```

加入“与其他应用同时播放”设置后的最终 Release 构建结果为：

```text
BUILD SUCCESSFUL in 3m 44s
367 actionable tasks: 96 executed, 271 up-to-date
```

## 4. 签名

Release 初次产物为未签名 APK。交付前执行：

```powershell
zipalign.exe -f -p 4 VLC-Android-3.6.3-all.apk aligned.apk
apksigner.bat sign `
  --ks <开发密钥库> `
  --ks-key-alias <别名> `
  --ks-pass pass:<密码> `
  --key-pass pass:<密码> `
  --out vlccustom-3.6.3-release.apk aligned.apk
apksigner.bat verify --verbose --print-certs vlccustom-3.6.3-release.apk
```

当前交付 APK 使用 Android Debug 开发证书签名，仅用于本地安装和验收，不应作为应用商店生产签名。签名验证结果：v1、v2、v3 均通过。

最终交付文件：

- `dist/vlccustom-3.6.3-release.apk`
- 大小：115,389,256 bytes
- 包名：`org.videolan.vlccustom`
- 显示名：`vlccustom`
- SHA-256：`78658FD7EFA34230073F0DDAEEC1DFE3A636048040673E5ACE1C793774851CC0`

## 5. 自动化测试

执行：

```powershell
adb install -r VLC-Android-3.6.3-debug-all.apk
adb install -t -r app-debug-androidTest.apk
adb shell pm grant org.videolan.vlccustom.debug android.permission.READ_MEDIA_AUDIO
adb shell am instrument -w -r `
  -e class org.videolan.vlc.audio.AudioRoutingInstrumentedTest `
  org.videolan.vlccustom.debug.test/org.videolan.vlc.MultidexTestRunner
```

Test APK 带有 `testOnly` 标记，部分设备必须使用 `adb install -t` 安装。

测试覆盖：

- 播放前安装 LibVLC PCM 回调，确认 `amem` 输出和实际 PCM 渲染。
- 直接检查处理后的左右 PCM 能量；目标声道有信号、非目标声道严格为零。
- STEREO、LEFT、RIGHT 三种模式及互相切换。
- 播放中连续快速切换 20 次。
- 暂停、恢复和 seek 后模式保持。
- 单声道、WAV、FLAC、MP3、Opus、AAC、5.1、7.1。
- 双音轨文件切轨后模式保持。
- SharedPreferences 保存和恢复。

结果：

- Android 15 x86_64 模拟器：`OK (6 tests)`，耗时 40.113 秒。
- Android 15 arm64 实体手机：`OK (6 tests)`，耗时 34.181 秒。
- 分阶段系统音频采集用例：模拟器和实体手机分别通过。

调试期间测试还发现并修复了 native 播放器地址复用导致的回调未安装问题，以及测试计量与原 PCM 包并发导致的时间窗口误差。

## 6. 系统音频端到端验证

测试源：

```text
左声道：440 Hz
右声道：880 Hz
采样率：48 kHz
格式：16-bit PCM
```

使用 scrcpy 5.0.1 的 `--audio-source=output` 从 Android 系统音频输出采集 28 秒 FLAC 文件，同时由 instrumentation 依次输出：

1. 立体声 3 秒
2. 仅左 3 秒
3. 仅右 3 秒
4. 立体声 1.5 秒

逐秒 RMS 结果的关键窗口：

| 时间窗口 | 左声道 | 右声道 | 结论 |
|---|---:|---:|---|
| 14–16 秒 | -29.4 ~ -28.1 dB | -29.4 ~ -28.1 dB | 立体声，双声道有信号 |
| 18–19 秒 | -31.1 dB | -91.0 dB | 仅左声道 |
| 21–22 秒 | -91.0 dB | -31.1 dB | 仅右声道 |
| 23–24 秒 | -33.1 ~ -28.9 dB | -29.9 ~ -28.9 dB | 切回立体声 |

边界秒存在预期的 20 ms 交叉淡化。稳定窗口的非目标声道为数字静音，证明最终进入 Android 音频混合器的是经过应用处理的双声道 PCM。

## 7. 兼容性与共存

- 官方 VLC APK：来自 VideoLAN 官方 3.6.3 发布目录。
- 官方包名：`org.videolan.vlc`，显示名 `VLC`。
- 定制版包名：`org.videolan.vlccustom`，显示名 `vlccustom`。
- 模拟器中两个 APK 同时安装成功，UID 和数据目录不同。
- 两个应用分别启动并分别播放同一测试视频成功。
- 卸载并重装定制版后，官方 VLC 仍存在。
- 重复覆盖安装/升级定制版后，官方 VLC 仍存在。
- 实体手机同时存在 `org.videolan.vlc` 与 `org.videolan.vlccustom`。
- 最终 Release 在实体手机成功启动视频 Activity，LibVLC 使用 `amem` 后端，native 库从 Release APK 的 `lib/arm64` 加载。

## 8. 资源占用

实体手机 Release 播放 640×360 H.264/AAC 视频时的一次采样：

- 进程：`org.videolan.vlccustom`
- CPU：约 14.2%（单次采样，系统整体为 8 核统计）
- TOTAL PSS：约 185 MB
- TOTAL RSS：约 375 MB
- 音轨：48 kHz、双声道、S16 输出

该数值包含 VLC 视频解码、UI、媒体库与原有组件，不是仅音频桥的开销。AudioTrack 使用系统建议最小缓冲区大小的 2 倍，以兼顾低延迟和抗欠载。

最终 Release 还完成了 3 分钟连续播放：

- 播放结束后进程仍存活，Activity 保持正常。
- MediaSession 已到达播放结束状态，未见 FATAL EXCEPTION 或 ANR。
- 结束后采样：CPU 0%，累计 CPU 时间约 11.07 秒，TOTAL PSS 约 143 MB，TOTAL RSS 约 317 MB。
- 与播放中的采样相比未观察到持续增长的内存趋势。

## 9. 最终回归结果

- 模拟器最终 instrumentation：`OK (6 tests)`，耗时 40.113 秒。
- 实体手机最终 instrumentation：`OK (6 tests)`，耗时 34.181 秒。
- 最终 Release 已覆盖安装到模拟器和实体手机；实体手机完成回归后已移除临时 Debug/Test 包。
- APK 仅包含 4 个 ABI 的 `libvlccustom_audio.so`，不包含已废弃的 `libaudiotrack_custom_plugin.so`。

### 并行播放 / AudioFocus 验证

- 设置页实测显示“Keep playing with other apps / 与其他应用同时播放”，默认关闭。
- 关闭时保持原版行为：第二播放器获得 AudioFocus 后，vlccustom MediaSession 暂停。
- 开启后，vlccustom 播放 3 分钟测试视频，再启动官方 VLC 并使其获得 AudioFocus；
  vlccustom 的 MediaSession 保持 `PLAYING`，MediaFocusControl 日志中只出现外部应用请求焦点，
  未出现 vlccustom 再次申请焦点。
- 设置值成功持久化到
  `allow_parallel_audio_playback=true`，开关变化会立即调用
  `PlaybackService.refreshAudioFocus()`。

### 播放生命周期回归

用户反馈的三个问题均已定位到同一个音频生命周期边界：

1. seek 引发 `flush` 后，AudioTrack 保持暂停，虽然 PCM 回调仍在继续，但扬声器无声。
2. LibVLC `stop()` 后重播时，Java 包装层用缓存的 `android_audiotrack` 覆盖了 native 回调配置。
3. 小窗切换会经历停止/重启或音频输出重建，因此同样会进入上述状态。

修复内容：

- PCM 回调发现音轨暂停时立即恢复 `AudioTrack.play()`。
- `flush`/`drain` 后保留可重建状态，下一条 PCM 自动恢复或重建音轨。
- 每个播放会话开始前重新安装 LibVLC 回调。
- 同时将 Java `MediaPlayer.setAudioOutput()` 和 native `amem` 输出保持一致。
- 新增播放头断言：seek 后和停止重播后，不仅要求 PCM 回调增长，还要求实际 AudioTrack playback head 持续前进。

## 10. 未覆盖项

- 本次环境没有连接有线耳机或蓝牙耳机，未完成对应设备的声学确认。
- 实机为 Android 15，模拟器为 API 35，未覆盖多个 Android 大版本。
- 系统输出采集和 PCM 能量已客观验证，但人耳主观听感、实际扬声器左右位置和蓝牙延迟仍需在对应硬件上抽检。
- 强烈削波、异常音频驱动、播放中热切换音频输出设备等极端场景仍建议列入后续实验室测试。
