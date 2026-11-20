param(
    [string]$Version = "0.1.0",
    [string]$QtRoot = "E:\QT\6.8.2\mingw_64",
    [string]$MinGWRoot = "E:\QT\Tools\mingw1310_64",
    [string]$BuildDir = "build-release",
    [string]$FfmpegDir = "vendor\ffmpeg",
    [string]$InnoCompiler = ""
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildPath = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $root $BuildDir }
$ffmpegPath = if ([System.IO.Path]::IsPathRooted($FfmpegDir)) { $FfmpegDir } else { Join-Path $root $FfmpegDir }
$distRoot = Join-Path $root "dist"
$distPath = Join-Path $distRoot "ScreenRecorder-$Version"

# 构建时使用 Qt 自带的 MinGW，避免误用系统里的其他 windeployqt。
$env:Path = (Join-Path $QtRoot "bin") + ";" + (Join-Path $MinGWRoot "bin") + ";" + $env:Path

Write-Host "== Configure Release =="
cmake -S $root -B $buildPath -G "MinGW Makefiles" `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_PREFIX_PATH=$QtRoot
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

Write-Host "== Build =="
cmake --build $buildPath --target screenRecorder -j 4
if ($LASTEXITCODE -ne 0) { throw "build failed" }

Write-Host "== Install + deploy Qt =="
if (Test-Path $distPath) { Remove-Item $distPath -Recurse -Force }
cmake --install $buildPath --prefix $distPath
if ($LASTEXITCODE -ne 0) { throw "cmake install failed" }

$binPath = Join-Path $distPath "bin"
foreach ($tool in @("ffmpeg.exe", "ffprobe.exe")) {
    $source = Join-Path $ffmpegPath $tool
    if (-not (Test-Path $source)) {
        throw "缺少 $source；请把随包的 ffmpeg.exe/ffprobe.exe 放到 $ffmpegPath"
    }
    Copy-Item $source (Join-Path $binPath $tool) -Force
}

$licenseSource = Join-Path $root "licenses"
$licenseTarget = Join-Path $distPath "licenses"
New-Item -ItemType Directory -Force $licenseTarget | Out-Null
Copy-Item (Join-Path $licenseSource "*") $licenseTarget -Recurse -Force

foreach ($required in @(
    (Join-Path $binPath "screenRecorder.exe"),
    (Join-Path $binPath "ffmpeg.exe"),
    (Join-Path $binPath "ffprobe.exe"),
    (Join-Path $licenseTarget "LICENSE.txt"),
    (Join-Path $licenseTarget "THIRD_PARTY_NOTICES.txt")
)) {
    if (-not (Test-Path $required)) { throw "打包目录缺少 $required" }
}

if (-not $InnoCompiler) {
    $candidates = @(
        (Join-Path $env:ProgramFiles "Inno Setup 6\ISCC.exe"),
        (Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\ISCC.exe"),
        (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe")
    )
    $InnoCompiler = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}

if (-not $InnoCompiler -or -not (Test-Path $InnoCompiler)) {
    Write-Host ""
    Write-Host "未找到 Inno Setup 6 的 ISCC.exe。"
    Write-Host "安装后重跑本脚本，或手动执行："
    Write-Host "  ISCC.exe /DAppVersion=$Version installer\ScreenRecorder.iss"
    exit 2
}

Write-Host "== Build installer =="
& $InnoCompiler "/DAppVersion=$Version" (Join-Path $root "installer\ScreenRecorder.iss")
if ($LASTEXITCODE -ne 0) { throw "Inno Setup compile failed" }

$setup = Join-Path $distRoot "ScreenRecorder-$Version-setup.exe"
if (-not (Test-Path $setup)) { throw "没有找到安装包：$setup" }

$hash = Get-FileHash $setup -Algorithm SHA256
$sumFile = Join-Path $distRoot "SHA256SUMS.txt"
"$($hash.Hash.ToLower())  $(Split-Path $setup -Leaf)" | Set-Content -Encoding ASCII $sumFile

Write-Host ""
Write-Host "完成："
Write-Host "  安装包: $setup"
Write-Host "  校验和: $sumFile"
