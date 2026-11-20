# Release checklist (v0.1.0)

## 0. 准备

- 安装 **Inno Setup 6**（需要 `ISCC.exe`，通常为 `C:\Program Files (x86)\Inno Setup 6\ISCC.exe`）。
  - 若 Inno 的语言目录没有 `ChineseSimplified.isl`，删除 `installer/ScreenRecorder.iss` 里的 `chinesesimplified` 行。
- 准备发布用 FFmpeg：
  - `vendor\ffmpeg\ffmpeg.exe`
  - `vendor\ffmpeg\ffprobe.exe`
  - 建议使用较小的 essentials/最小构建；当前 full build 每个约 211 MB。
  - 验证：`ffmpeg -devices` 有 `gdigrab`，`ffmpeg -encoders` 有 `libx264` 和 `aac`。
- FFmpeg 源码/许可：
  - `licenses\GPL-3.0.txt`、`licenses\LGPL-3.0.txt` 已存在；
  - 在 GitHub Release 附上 FFmpeg 对应源码压缩包，或准备有效的书面源码提供方式；
  - 检查 `licenses\THIRD_PARTY_NOTICES.txt` 里的版本/源码地址与实际二进制一致。

## 1. 构建安装包

```powershell
# 在项目根目录
powershell -ExecutionPolicy Bypass -File .\scripts\package.ps1 -Version 0.1.0
```

脚本会：

1. 用 `build-release` 做 Release 构建；
2. `cmake --install` 到 `dist\ScreenRecorder-0.1.0`，Qt 由 `qt_generate_deploy_app_script` 部署；
3. 把 `vendor\ffmpeg` 的 ffmpeg/ffprobe 复制到 `dist\...\bin`；
4. 复制 `licenses`；
5. 调 Inno Setup 生成 `dist\ScreenRecorder-0.1.0-setup.exe`；
6. 生成 `dist\SHA256SUMS.txt`。

如果 `ISCC.exe` 不在默认路径：

```powershell
.\scripts\package.ps1 -Version 0.1.0 -InnoCompiler "C:\Path\To\ISCC.exe"
```

## 2. 安装包自检

- 在**没有 Qt、没有 FFmpeg PATH** 的机器/虚拟机上运行 setup。
- 检查安装目录：
  - `bin\screenRecorder.exe`
  - `bin\ffmpeg.exe`、`bin\ffprobe.exe`
  - `plugins\platforms\qwindows.dll`
  - `licenses\`
- 启动后版本徽标应为 `V0.1.0`，exe 属性版本 `0.1.0.0`。
- 录制一段 10～15 秒全屏视频，停止，确认能播放且有声音；再测区域/窗口。
- 检查开始菜单快捷方式、卸载入口；卸载后安装目录和快捷方式应消失，用户录像不应被删除。

## 3. 干净环境验收记录

建议记录：

```text
环境：Windows 11 x64，全新 VM，无 Qt/FFmpeg
安装：setup 退出码 0，SmartScreen：是/否
启动：成功/失败；版本 V0.1.0
录制：全屏 15s → 文件路径 → ffprobe 编码/分辨率/时长
音频：系统声/麦克风/双路
播放：Windows 自带播放器 / VLC
卸载：成功/失败；残留文件与快捷方式
问题：
```

## 4. 打 tag 与 GitHub Release

```powershell
git tag -a v0.1.0 -m "ScreenRecorder v0.1.0"
git push origin main
git push origin v0.1.0
```

在 GitHub 创建 Release：

- Tag：`v0.1.0`
- 附件：
  - `ScreenRecorder-0.1.0-setup.exe`
  - `SHA256SUMS.txt`
  - FFmpeg 对应源码压缩包（GPL 合规；建议附上）
- Release notes 使用 `CHANGELOG.md` 的 v0.1.0 内容。
- README 的下载链接指向 Releases；发布后确认链接可用。

校验和：

```powershell
Get-FileHash .\dist\ScreenRecorder-0.1.0-setup.exe -Algorithm SHA256
```

## 5. 发布后

- 用另一台机器从 Release 下载安装包，按第 2 节再走一遍。
- 把“安装→录制→停止→播放→卸载”的实际结果补到 [学习记录.md](学习记录.md)。
- 如果 FFmpeg 换成更小构建，重新生成安装包和校验和，并在 Release 里替换附件。
