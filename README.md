# vlccustom

`vlccustom` 是基于 VLC for Android 3.6.3 的 Android 定制播放器。项目保留 VLC 原有播放、
媒体库、音轨选择和播放控制能力，并增加左右声道定向输出及与其他应用同时播放的选项。

> 本项目是非官方定制版本，与 VideoLAN 官方无隶属关系。源码继续遵循上游使用的
> GPLv2-or-later 许可证。

## 项目状态

- 上游基线：VLC for Android `3.6.3`，提交 `13b29e8`
- 定制包名：`org.videolan.vlccustom`
- Debug 包名：`org.videolan.vlccustom.debug`
- 应用显示名：`vlccustom`
- Android：`minSdk 17`、`targetSdk 34`
- 已验证环境：Android 15 模拟器和 arm64 实体手机
- 当前 APK：`vlccustom-3.6.3-release.apk`

APK 大小为 115,389,256 bytes，超过 GitHub 普通 Git 文件 100 MiB 限制，因此仓库不直接
跟踪 APK。请从 [GitHub Releases](https://github.com/Ch1RsD/Custom-Vlc/releases) 下载，
或按本文档自行构建。

## 功能

### 独立应用

- 使用独立 `applicationId`、数据目录、Provider authority、权限和 Intent action。
- 可以与官方 VLC 同时安装、升级和卸载，互不覆盖。

### 左右声道定向输出

播放页面“高级选项/工具”菜单提供三个互斥模式：

- 立体声
- 仅左声道
- 仅右声道

LibVLC 先完成解码、重采样、多声道下混和滤镜处理，应用再通过 `amem` PCM 回调执行：

```text
M = (L + R) / 2

STEREO: outL = L, outR = R
LEFT:   outL = M, outR = 0
RIGHT:  outL = 0, outR = M
```

模式在播放中生效，并通过 20 ms 交叉淡化减少切换爆音。设置保存在应用独立的
SharedPreferences 中。

### 与其他应用同时播放

入口：**设置 → 音频 → 与其他应用同时播放**。

- 默认关闭，保持 VLC 原有 AudioFocus 行为。
- 启用后不再请求 AudioFocus，并释放当前已持有的焦点。
- 忽略暂停、瞬时暂停和 duck 请求；之前被降低的音量会恢复。
- 切换开关会立即刷新当前播放状态，无需重启视频。
- 关闭后恢复 VLC 原有焦点抢占和暂停逻辑。

此功能只绕过应用层 Android AudioFocus。电话、系统级暂停和厂商音频策略仍可能影响播放。

## 已修复的播放生命周期问题

- 拖动进度条后 PCM 回调存在但扬声器无声。
- 返回已观看过的视频或停止后重播时音频输出未重新安装。
- 进入/退出小窗时音频输出重建导致无声。

修复集中在 `PlayerController`、`AudioRouting` 和 native `amem` 音频桥。自动化回归同时检查
PCM 渲染、AudioTrack 状态、播放头和 seek 后音频连续性。

## 安装

从 Releases 下载通用 arm64/x86 APK，然后执行：

```bash
adb install -r vlccustom-3.6.3-release.apk
```

当前交付 APK 使用 Android Debug 开发证书签名，适合本地安装和验收，不应用于公开商店发布。

## 构建

### 环境

| 组件 | 版本 |
|---|---|
| JDK | 17 |
| Gradle | 8.9 |
| Android Gradle Plugin | 8.5.0 |
| Kotlin | 1.9.22 |
| Android SDK | compile/target 34 |
| Android NDK | r27c，仅在重建 native 桥时需要 |
| LibVLC Android | 3.6.0 |

创建 `local.properties`，并指向本机 Android SDK：

```properties
sdk.dir=/path/to/android-sdk
```

Windows 路径需要按 Properties 文件规则转义。

### 重建 native 音频桥

仓库包含四种 ABI 的预编译 `libvlccustom_audio.so`。修改
`plugins/audio-routing/src/vlccustom_audio.c` 后，使用 PowerShell 重建：

```powershell
.\plugins\audio-routing\build.ps1 -NdkRoot <NDK路径>
```

输出会写入：

```text
application/app/src/main/jniLibs/<ABI>/libvlccustom_audio.so
```

### 构建 Debug APK

```bash
./gradlew :application:app:assembleDebug
```

Windows：

```powershell
.\gradlew.bat :application:app:assembleDebug
```

### 构建 Release

```bash
./gradlew :application:app:assembleRelease
```

Gradle 生成的是未签名 APK。发布前需要执行 `zipalign` 和 `apksigner`，完整签名流程见
[构建与测试](docs/构建与测试.md)。

## 自动化测试

测试工程位于：

```text
application/app/src/androidTest/java/org/videolan/vlc/audio/AudioRoutingInstrumentedTest.kt
```

执行步骤：

```bash
adb install -r VLC-Android-3.6.3-debug-all.apk
adb install -t -r app-debug-androidTest.apk
adb shell pm grant org.videolan.vlccustom.debug android.permission.READ_MEDIA_AUDIO
adb shell am instrument -w -r \
  -e class org.videolan.vlc.audio.AudioRoutingInstrumentedTest \
  org.videolan.vlccustom.debug.test/org.videolan.vlc.MultidexTestRunner
```

当前回归结果：

- Android 15 x86_64 模拟器：`OK (6 tests)`
- Android 15 arm64 实体手机：`OK (6 tests)`

测试覆盖声道能量、左右定向、连续切换、暂停恢复、seek、停止后重播、输出重建、单声道、
多声道、多音轨及常见编码格式。

## 项目结构

```text
application/app/                   应用壳、测试和预编译 native 库
application/vlc-android/           VLC Android 主模块与播放服务
application/resources/             共享资源与翻译
plugins/audio-routing/             自定义 native PCM/AudioTrack 桥
docs/                              架构、构建测试、修改清单和发布说明
.github/workflows/android-ci.yml   基础 CI 构建
```

关键实现：

- 声道模式入口：`application/vlc-android/src/org/videolan/vlc/gui/helpers/PlayerOptionsDelegate.kt`
- 声道模式与 JNI：`application/vlc-android/src/org/videolan/vlc/audio/AudioRouting.java`
- native 处理与 AudioTrack：`plugins/audio-routing/src/vlccustom_audio.c`
- 播放生命周期：`application/vlc-android/src/org/videolan/vlc/media/PlayerController.kt`
- AudioFocus 控制：`application/vlc-android/src/org/videolan/vlc/util/VLCAudioFocusHelper.kt`

## 当前发布

- APK：
  `vlccustom-3.6.3-release.apk`
- APK SHA-256：
  `78658FD7EFA34230073F0DDAEEC1DFE3A636048040673E5ACE1C793774851CC0`
- 源码包 SHA-256：
  `4C212B5B0D058199041160B111195674F50FFF6B0D2B57FC12DD30F217BF5766`
- APK 签名：v1、v2、v3
- Native 库：`arm64-v8a`、`armeabi-v7a`、`x86`、`x86_64`

详细发布和维护步骤见 [发布说明](docs/RELEASE.md)。

## 已知限制

- 未在当前环境完成有线耳机和蓝牙耳机的声学确认。
- 仅验证 Android 15 设备；更早版本和不同厂商音频策略需要在目标设备复测。
- “与其他应用同时播放”不能绕过电话、系统级暂停或厂商策略。
- 数字直通会绕过 PCM 定向，定向模式会临时关闭直通。

## 贡献

提交改动前请阅读 [CONTRIBUTING.md](CONTRIBUTING.md)，并至少执行一次 Debug 构建和音频路由
instrumentation 回归。涉及 VLC 内部或 GPL/LGPL 边界时，请保留上游版权和许可证声明。

## 许可证与上游

- VLC for Android：<https://code.videolan.org/videolan/vlc-android>
- 上游版本：3.6.3，提交 `13b29e8`
- 本项目许可证：[GPLv2-or-later](COPYING)
- LibVLC 仍遵循其上游 LGPLv2 许可证

本项目不声称获得 VideoLAN 官方支持或背书。
