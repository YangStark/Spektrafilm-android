# SPDX-FileCopyrightText: 2026 Spektrafilm Android contributors
# SPDX-License-Identifier: GPL-3.0-only
# Native arm64 device regressions; does not install or operate the app.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$NdkRoot,
    [Parameter(Mandatory=$true)][string]$CMakeBin,
    [string]$Adb = 'adb',
    [string]$Serial,
    [switch]$LargeFrame,
    [string]$BuildDir
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path "$PSScriptRoot/../..").Path
if (-not $BuildDir) { $BuildDir = Join-Path $repo 'build/gpu-device-gates' }
$cpp = Join-Path $repo 'engine/spektra-core/src/main/cpp'
$cmake = Join-Path $CMakeBin 'cmake.exe'
$ninja = Join-Path $CMakeBin 'ninja.exe'
$toolchain = Join-Path $NdkRoot 'build/cmake/android.toolchain.cmake'
$compiler = Join-Path $NdkRoot 'toolchains/llvm/prebuilt/windows-x86_64/bin/aarch64-linux-android24-clang++.cmd'
$runtime = Join-Path $NdkRoot 'toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so'
function CheckExit([string]$step) {
    if ($LASTEXITCODE -ne 0) { throw "$step failed: exit $LASTEXITCODE" }
}
function AdbRun {
    if ($Serial) { & $Adb -s $Serial @args } else { & $Adb @args }
    CheckExit 'adb'
}
& $cmake -S $cpp -B $BuildDir -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" `
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" -DANDROID_ABI=arm64-v8a `
    -DANDROID_PLATFORM=android-24 -DANDROID_STL=c++_shared `
    -DCMAKE_BUILD_TYPE=Release -DSPK_ENABLE_VULKAN=ON
CheckExit 'configure'
& $cmake --build $BuildDir --parallel 4
CheckExit 'build'
$remote = '/data/local/tmp/spektrafilm-upstream-gates'
AdbRun shell mkdir -p $remote
AdbRun push (Join-Path $BuildDir 'libspektra.so') "$remote/libspektra.so"
AdbRun push $runtime "$remote/libc++_shared.so"
$names = @('test_halation_gpu', 'test_grain_gpu')
if ($LargeFrame) { $names += 'test_large_frame_gpu' }
foreach ($name in $names) {
    $binary = Join-Path $BuildDir $name
    & $compiler -std=c++17 -O2 -DSPK_ENABLE_VULKAN=1 "-I$cpp" `
        (Join-Path $cpp "gpu/tests/$name.cpp") "-L$BuildDir" -lspektra -o $binary
    CheckExit "compile $name"
    AdbRun push $binary "$remote/$name"
    AdbRun shell chmod 755 "$remote/$name"
    # adb shell propagates the test's exit status. Require engagement as well:
    # the older small-frame gates can otherwise pass without a Vulkan device.
    $capture = @(AdbRun shell "LD_LIBRARY_PATH=$remote $remote/$name" 2>&1)
    $capture | Tee-Object -FilePath (Join-Path $BuildDir "$name.log")
    $output = $capture -join "`n"
    if ($output -notmatch 'engaged=1' -or $output -notmatch "$name`: ALL OK") {
        throw "$name did not pass with an engaged GPU"
    }
}
