$ErrorActionPreference = 'Stop'

$testRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent (Split-Path -Parent $testRoot)
$gcc = $env:IAP_GCC
if (-not $gcc) {
    $gccCommand = Get-Command 'gcc.exe' -ErrorAction SilentlyContinue
    if ($gccCommand) { $gcc = $gccCommand.Source }
}
$buildRoot = Join-Path $testRoot 'build'
$executable = Join-Path $buildRoot 'iap_pc_sim_tests.exe'

if (-not $gcc -or -not (Test-Path -LiteralPath $gcc -PathType Leaf)) {
    throw 'GCC not found. Set IAP_GCC or add gcc.exe to PATH.'
}
New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null

$arguments = @(
    '-std=c11', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
    "-I$(Join-Path $projectRoot 'common\include')",
    "-I$(Join-Path $projectRoot 'firmware\bootloader\Core\Inc')",
    "-I$testRoot",
    (Join-Path $projectRoot 'common\src\iap_crc.c'),
    (Join-Path $projectRoot 'common\src\iap_protocol.c'),
    (Join-Path $projectRoot 'firmware\bootloader\Core\Src\boot_service.c'),
    (Join-Path $testRoot 'mock_platform.c'),
    (Join-Path $testRoot 'sim_tests.c'),
    '-o', $executable
)

& $gcc @arguments
if ($LASTEXITCODE -ne 0) {
    throw "PC simulation compile failed with exit code $LASTEXITCODE"
}
& $executable
if ($LASTEXITCODE -ne 0) {
    throw "PC simulation failed with exit code $LASTEXITCODE"
}

Write-Host "PC simulation passed: $executable"
