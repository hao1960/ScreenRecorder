# 随包 FFmpeg

把用于发布的 `ffmpeg.exe` 和 `ffprobe.exe` 放在这个目录，`scripts/package.ps1` 会把它们复制到安装目录的 `bin\` 下。

**不要把这些二进制提交到 Git**；它们通过 `.gitignore` 排除，只作为本地发布输入。

## 建议的构建

当前开发机上 `ffmpeg.exe` / `ffprobe.exe` 来自 gyan.dev 的 **full build**，单个约 200 MB，两个一起会让安装包非常大。v0.1.0 发布前建议换成更小的构建：

- gyan.dev 的 **essentials** 构建；或
- 自己编译一个最小 FFmpeg，只开启：
  - `gdigrab` 输入
  - `libx264` / `libx265` 编码（GPL）
  - `aac` 编码、`mp4` 封装
  - 需要的音频输入（dshow、pipe）

## 放入前检查

```powershell
ffmpeg -hide_banner -version
ffmpeg -hide_banner -devices | Select-String gdigrab
ffmpeg -hide_banner -encoders | Select-String "libx264|aac"
ffprobe -hide_banner -version
```

应用启动时也会检查 `gdigrab` 和所选编码器。

## 许可证

发布用构建如果是 `--enable-gpl`（包含 x264/x265），需要按 GPLv3 分发：许可证文本、对应源码或有效的源码获取方式。详见 [../licenses/THIRD_PARTY_NOTICES.txt](../licenses/THIRD_PARTY_NOTICES.txt)。
