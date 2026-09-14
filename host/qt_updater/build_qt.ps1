$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$qtRoot = 'D:\Qt\6.8.3\mingw_64'
$cmake = 'D:\Qt\Tools\CMake_64\bin\cmake.exe'
$ninja = 'D:\Qt\Tools\Ninja\ninja.exe'
$cxxCompiler = 'D:\Qt\Tools\mingw1310_64\bin\g++.exe'
$deployQt = 'D:\Qt\6.8.3\mingw_64\bin\windeployqt.exe'
$buildRoot = Join-Path $projectRoot 'build-qt-6.8.3'
$executable = Join-Path $buildRoot 'stm32f407_iap_qt_updater.exe'

$requiredTools = @($cmake, $ninja, $cxxCompiler, $deployQt)
foreach ($tool in $requiredTools) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
        throw "Required Qt build tool is missing: $tool"
    }
}

$configureArguments = @(
    '-S', $projectRoot,
    '-B', $buildRoot,
    '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release',
    "-DCMAKE_PREFIX_PATH=$qtRoot",
    "-DCMAKE_MAKE_PROGRAM=$ninja",
    "-DCMAKE_CXX_COMPILER=$cxxCompiler"
)

& $cmake @configureArguments
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed with exit code $LASTEXITCODE"
}

& $cmake --build $buildRoot --config Release
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed with exit code $LASTEXITCODE"
}
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "Expected executable was not created: $executable"
}

& $deployQt --release --no-translations --compiler-runtime $executable
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE"
}

$process = Start-Process -FilePath $executable `
    -ArgumentList '--smoke-test' `
    -WorkingDirectory $buildRoot `
    -WindowStyle Hidden `
    -Wait `
    -PassThru
if ($process.ExitCode -ne 0) {
    throw "Qt startup smoke test failed with exit code $($process.ExitCode)"
}

Write-Host "Built and smoke-tested: $executable"
