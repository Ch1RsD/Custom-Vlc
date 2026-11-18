# Changelog

## 3.6.3-custom.1 - 2026-11-18

首个公开源码版本，基于 VLC for Android 3.6.3。

### Added

- 独立的 `vlccustom` 应用标识，可与官方 VLC 共存。
- 播放页面立体声、仅左声道、仅右声道快捷切换。
- LibVLC `amem` PCM 定向输出和 20 ms 切换淡变。
- 设置页“与其他应用同时播放”AudioFocus 开关。
- 四 ABI 的 `libvlccustom_audio.so` 预编译 native 桥。
- 音频路由 instrumentation 测试、构建文档、架构文档和发布说明。

### Fixed

- seek 后 PCM 回调继续但 AudioTrack 未恢复播放。
- 停止后重播未重新安装音频输出回调。
- 返回已观看视频和进入/退出小窗时音频输出重建导致无声。
- AudioFocus 并行模式下可能残留 duck 音量。

### Verified

- 模拟器：`OK (6 tests)`。
- 实体手机：`OK (6 tests)`。
- 官方 VLC 与 vlccustom 可同时播放，外部应用获得 AudioFocus 时启用并行播放的
  vlccustom 保持 `PLAYING`。
- Release APK v1、v2、v3 签名验证通过。
