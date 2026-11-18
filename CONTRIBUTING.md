# Contributing

## 开发前检查

1. 使用 JDK 17、Android SDK 34 和 Gradle 8.9。
2. 创建本机 `local.properties`，不要提交 SDK 路径、签名密钥或访问令牌。
3. 修改 native 音频桥后，使用 `plugins/audio-routing/build.ps1` 重建四种 ABI。
4. 不要全局替换 `org.videolan.vlc`。VLC namespace、applicationId 和源码包路径是不同概念。

## 必须执行的检查

```bash
./gradlew :application:app:assembleDebug
```

连接设备后执行音频路由 instrumentation 测试：

```bash
adb shell am instrument -w -r \
  -e class org.videolan.vlc.audio.AudioRoutingInstrumentedTest \
  org.videolan.vlccustom.debug.test/org.videolan.vlc.MultidexTestRunner
```

涉及播放生命周期时，还应手动检查：

- 拖动进度条后仍有声音。
- 停止后重播仍有声音。
- 返回已观看视频仍有声音。
- 进入和退出小窗仍有声音。
- 音轨、声道模式和 AudioFocus 设置切换后状态一致。

## 代码约定

- 优先复用 VLC/LibVLC 已有能力。
- Java/Kotlin 与 native 之间共享状态必须明确线程和生命周期。
- AudioTrack 的 stop、flush、drain 和重建路径需要保持可恢复。
- PCM 定向保持目标声道包含完整下混内容，非目标声道严格静音。
- 不提交构建目录、APK、`.idsig`、`local.properties`、密钥或设备数据。

## 提交与发布

- 一个提交解决一个主题，提交信息描述行为变化。
- 用户可见变化同步更新 `CHANGELOG.md`。
- 发布前更新 README 中的哈希和已知限制。
- Release APK 使用 GitHub Releases 分发，不直接提交到普通 Git 历史。

## 许可证

贡献代码即表示同意按本项目的 GPLv2-or-later 许可证发布，并保留上游与第三方许可证声明。
