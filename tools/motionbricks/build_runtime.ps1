param(
    [string]$SourceDir = "$PSScriptRoot\..\..\build_motionbricks_src",
    [string]$BuildDir = "$PSScriptRoot\..\..\build_motionbricks_runtime",
    [string]$AssetDir = "$PSScriptRoot\..\..\build_motionbricks_assets",
    [switch]$DownloadModels,
    [switch]$EnableVulkan
)

$ErrorActionPreference = "Stop"
$revision = "6fdb75e15ddb7f97dd1a4abb8017a57b936bc7a3"
$repository = "https://github.com/localai-org/motion-bricks.cpp.git"
$source = [System.IO.Path]::GetFullPath($SourceDir)
$build = [System.IO.Path]::GetFullPath($BuildDir)
$assets = [System.IO.Path]::GetFullPath($AssetDir)

if (-not (Test-Path -LiteralPath "$source\.git")) {
    git clone $repository $source
}
git -C $source checkout $revision
git -C $source submodule update --init --depth 1

$staticOutput = Select-String -LiteralPath "$source\CMakeLists.txt" -Pattern "OUTPUT_NAME motionbricks_static" -Quiet
if (-not $staticOutput) {
    git -C $source apply "$PSScriptRoot\windows-static-name.patch"
}

$cmake = Get-Command cmake.exe -ErrorAction SilentlyContinue
if (-not $cmake) {
    $cmakePath = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if (-not (Test-Path -LiteralPath $cmakePath)) { throw "cmake.exe was not found" }
    $cmake = Get-Item -LiteralPath $cmakePath
}

$vulkan = if ($EnableVulkan) { "ON" } else { "OFF" }
& $cmake.FullName -S $source -B $build -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DMOTIONBRICKS_USE_GGML=ON `
    -DMOTIONBRICKS_USE_VULKAN=$vulkan `
    -DMOTIONBRICKS_BUILD_TESTS=ON `
    -DMOTIONBRICKS_DOWNLOAD_MODELS=OFF
& $cmake.FullName --build $build --parallel 4

$runtime = Join-Path $build "runtime"
New-Item -ItemType Directory -Force $runtime | Out-Null
Copy-Item -LiteralPath (Join-Path $build "motionbricks.dll") -Destination $runtime -Force
Copy-Item -Path (Join-Path $build "bin\ggml*.dll") -Destination $runtime -Force

if ($DownloadModels) {
    python "$source\scripts\download_gguf_weights.py" --output $assets
}

Write-Host "MotionBricks ABI DLL: $runtime\motionbricks.dll"
Write-Host "Models/styles: $assets (downloaded only with -DownloadModels)"
