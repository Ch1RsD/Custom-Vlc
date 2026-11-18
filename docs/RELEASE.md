# 发布说明

## 当前版本

- 版本基线：VLC for Android 3.6.3
- 定制版本：`vlccustom-3.6.3-release.apk`
- 大小：115,389,256 bytes
- 包名：`org.videolan.vlccustom`
- 显示名：`vlccustom`
- SHA-256：`78658FD7EFA34230073F0DDAEEC1DFE3A636048040673E5ACE1C793774851CC0`
- 签名：Android Debug 开发证书，v1/v2/v3

## GitHub Release 文件

GitHub 普通 Git 文件上限为 100 MiB，因此 APK 不进入 Git 历史。发布时在 GitHub Releases
上传：

- `vlccustom-3.6.3-release.apk`
- `vlccustom-3.6.3-release.apk.idsig`
- `SHA256SUMS.txt`

源码由 GitHub 根据发布标签自动生成 `Source code (zip)` 和
`Source code (tar.gz)` 归档，避免维护一份可能与 `main` 不一致的重复压缩包。

## 发布前检查

1. 更新 `build.gradle` 中的版本号和版本代码。
2. 更新 `CHANGELOG.md`。
3. 重建 native 桥。
4. 完成 Debug 构建、Release 构建和 instrumentation 回归。
5. 在模拟器和至少一台实体设备验证 seek、重播、返回已看视频、小窗和 AudioFocus。
6. 对齐、签名并验证 APK：

```powershell
zipalign.exe -f -p 4 VLC-Android-3.6.3-all.apk aligned.apk
apksigner.bat sign `
  --ks <keystore> `
  --ks-key-alias <alias> `
  --ks-pass pass:<password> `
  --key-pass pass:<password> `
  --out vlccustom-3.6.3-release.apk aligned.apk
apksigner.bat verify --verbose --print-certs vlccustom-3.6.3-release.apk
```

7. 计算并写入 SHA-256：

```powershell
Get-FileHash .\vlccustom-3.6.3-release.apk -Algorithm SHA256
```

8. 创建 Git 标签并上传 Release 资产。

## 生产签名

当前开发证书只适合本地验收。正式分发应使用独立生产密钥，并通过安全存储或 CI Secret
注入密码，不得把密钥库或密码提交到仓库。
