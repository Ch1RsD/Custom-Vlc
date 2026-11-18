param(
    [string]$NdkRoot = ""
)

$ErrorActionPreference = "Stop"

$pluginRoot = $PSScriptRoot
$repositoryRoot = Resolve-Path (Join-Path $pluginRoot "..\..")
$source = Join-Path $pluginRoot "src\vlccustom_audio.c"
$outputRoot = Join-Path $repositoryRoot "application\app\src\main\jniLibs"

if ([string]::IsNullOrWhiteSpace($NdkRoot)) {
    $NdkRoot = $env:ANDROID_NDK_HOME
}
if ([string]::IsNullOrWhiteSpace($NdkRoot)) {
    $NdkRoot = $env:ANDROID_NDK_ROOT
}
if ([string]::IsNullOrWhiteSpace($NdkRoot) -and -not [string]::IsNullOrWhiteSpace($env:ANDROID_HOME)) {
    $defaultNdk = Join-Path $env:ANDROID_HOME "ndk"
    if (Test-Path -LiteralPath $defaultNdk) {
        $latestNdk = Get-ChildItem -LiteralPath $defaultNdk -Directory |
            Sort-Object Name -Descending |
            Select-Object -First 1
        if ($null -ne $latestNdk) {
            $NdkRoot = $latestNdk.FullName
        }
    }
}

if ([string]::IsNullOrWhiteSpace($NdkRoot) -or
        -not (Test-Path -LiteralPath $NdkRoot)) {
    throw "Android NDK not found. Set ANDROID_NDK_HOME or pass -NdkRoot."
}

$clang = Join-Path $NdkRoot "toolchains\llvm\prebuilt\windows-x86_64\bin\clang.exe"
if (-not (Test-Path -LiteralPath $clang)) {
    throw "NDK clang not found at $clang"
}
if (-not (Test-Path -LiteralPath $source)) {
    throw "Native source not found at $source"
}

$abis = @(
    @{ Name = "arm64-v8a"; Target = "aarch64-linux-android21" },
    @{ Name = "armeabi-v7a"; Target = "armv7a-linux-androideabi21" },
    @{ Name = "x86"; Target = "i686-linux-android21" },
    @{ Name = "x86_64"; Target = "x86_64-linux-android21" }
)

$common = @(
    "-fPIC",
    "-O2",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-Wno-unused-parameter",
    "-fvisibility=hidden"
)

foreach ($abi in $abis) {
    $outputDir = Join-Path $outputRoot $abi.Name
    New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
    $output = Join-Path $outputDir "libvlccustom_audio.so"

    & $clang @common "--target=$($abi.Target)" -shared `
        "-Wl,--no-undefined" $source -lm -o $output
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to build libvlccustom_audio.so for $($abi.Name)"
    }
}

Write-Output "Built libvlccustom_audio.so for: $($abis.Name -join ', ')"
Write-Output "Output: $outputRoot"
