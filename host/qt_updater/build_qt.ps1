param(
    [string]$QtRoot = $env:QT_ROOT,
    [string]$CMake = $env:CMAKE_EXE,
    [string]$Ninja = $env:NINJA_EXE,
    [string]$CxxCompiler = $env:CXX_COMPILER,
    [string]$DeployQt = $env:WINDEPLOYQT_EXE
)

$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
foreach ($tool in @(@('CMake', 'cmake.exe'), @('Ninja', 'ninja.exe'), @('CxxCompiler', 'g++.exe'), @('DeployQt', 'windeployqt.exe'))) {
    $value = Get-Variable -Name $tool[0] -ValueOnly
    if (-not $value) {
        $command = Get-Command $tool[1] -ErrorAction SilentlyContinue
        if ($command) { Set-Variable -Name $tool[0] -Value $command.Source }
    }
}
if (-not $QtRoot -and $DeployQt) {
    $QtRoot = Split-Path -Parent (Split-Path -Parent $DeployQt)
}
$qtRoot = $QtRoot
$buildRoot = Join-Path $projectRoot 'build-qt-6.8.3'
$executable = Join-Path $buildRoot 'stm32f407_iap_qt_updater.exe'

$requiredTools = @($CMake, $Ninja, $CxxCompiler, $DeployQt, $QtRoot)
foreach ($tool in $requiredTools) {
    if (-not $tool -or -not (Test-Path -LiteralPath $tool)) {
        throw "Required Qt build tool is missing: $tool"
    }
}

$configureArguments = @(
    '-S', $projectRoot,
    '-B', $buildRoot,
    '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release',
    "-DCMAKE_PREFIX_PATH=$QtRoot",
    "-DCMAKE_MAKE_PROGRAM=$Ninja",
    "-DCMAKE_CXX_COMPILER=$CxxCompiler"
)

& $CMake @configureArguments
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed with exit code $LASTEXITCODE"
}

& $CMake --build $buildRoot --config Release
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed with exit code $LASTEXITCODE"
}
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "Expected executable was not created: $executable"
}

& $DeployQt --release --no-translations --compiler-runtime $executable
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
