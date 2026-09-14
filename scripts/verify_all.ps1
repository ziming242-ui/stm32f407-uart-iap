$ErrorActionPreference = 'Stop'

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptRoot
$logRoot = Join-Path $scriptRoot 'logs'
$summaryPath = Join-Path $logRoot 'verification_summary.txt'

& (Join-Path $scriptRoot 'build_keil.ps1') -Target All
if ($LASTEXITCODE -ne 0) {
    throw "Keil build verification failed with exit code $LASTEXITCODE"
}

& (Join-Path $projectRoot 'tests\pc_sim\run_tests.ps1')
if ($LASTEXITCODE -ne 0) {
    throw "PC simulation failed with exit code $LASTEXITCODE"
}

& (Join-Path $projectRoot 'host\qt_updater\build_qt.ps1')
if ($LASTEXITCODE -ne 0) {
    throw "Qt build verification failed with exit code $LASTEXITCODE"
}

$bootBin = Join-Path $projectRoot 'firmware\bootloader\MDK-ARM\Objects\Bootloader.bin'
$appBin = Join-Path $projectRoot 'firmware\app\MDK-ARM\Objects\App.bin'
$qtExe = Join-Path $projectRoot 'host\qt_updater\build-qt-6.8.3\stm32f407_iap_qt_updater.exe'

$bootInfo = Get-Item -LiteralPath $bootBin
$appInfo = Get-Item -LiteralPath $appBin
if ($bootInfo.Length -gt 0x20000) {
    throw "Bootloader.bin exceeds the 128 KiB Boot partition"
}
if ($appInfo.Length -gt 0x40000) {
    throw "App.bin exceeds the 256 KiB Run partition"
}

$appBytes = [System.IO.File]::ReadAllBytes($appBin)
if ($appBytes.Length -lt 8) {
    throw "App.bin is too short to contain a vector table"
}
$initialMsp = [BitConverter]::ToUInt32($appBytes, 0)
$resetHandler = [BitConverter]::ToUInt32($appBytes, 4)
$resetCode = $resetHandler -band 0xFFFFFFFE
if ($initialMsp -lt 0x20000000 -or $initialMsp -gt 0x20020000 -or
    ($initialMsp -band 7) -ne 0) {
    throw ('Invalid APP initial MSP: 0x{0:X8}' -f $initialMsp)
}
if (($resetHandler -band 1) -eq 0 -or $resetCode -lt 0x08040000 -or
    $resetCode -ge (0x08040000 + $appInfo.Length)) {
    throw ('Invalid APP Reset_Handler: 0x{0:X8}' -f $resetHandler)
}

$qtVersion = & 'D:\Qt\6.8.3\mingw_64\bin\qmake.exe' -query QT_VERSION
$lines = @(
    'STM32F407 UART IAP verification summary',
    ('Generated: {0}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz')),
    'Evidence level: PC simulation / compile and launch checks only',
    'Hardware evidence: NOT TESTED',
    '',
    ('Keil Bootloader: {0} bytes, SHA256={1}' -f $bootInfo.Length,
        (Get-FileHash -LiteralPath $bootBin -Algorithm SHA256).Hash),
    ('Keil App: {0} bytes, SHA256={1}' -f $appInfo.Length,
        (Get-FileHash -LiteralPath $appBin -Algorithm SHA256).Hash),
    ('APP vector[0] initial MSP: 0x{0:X8}' -f $initialMsp),
    ('APP vector[1] Reset_Handler: 0x{0:X8}' -f $resetHandler),
    'PC simulation: PASS (6 groups)',
    ('Qt version: {0}' -f $qtVersion),
    ('Qt updater: SHA256={0}' -f
        (Get-FileHash -LiteralPath $qtExe -Algorithm SHA256).Hash),
    '',
    'This report does not prove UART, Flash, reset, VTOR/MSP jump, FreeRTOS, or power-loss behavior on STM32 hardware.'
)

New-Item -ItemType Directory -Path $logRoot -Force | Out-Null
$lines | Set-Content -LiteralPath $summaryPath -Encoding UTF8
$lines
Write-Host "Verification summary: $summaryPath"
