[CmdletBinding()]
param(
    [ValidateSet('All', 'Bootloader', 'App')]
    [string]$Target = 'All'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectRoot = Split-Path -Parent $PSScriptRoot
$uv4Path = 'E:\keil\UV4\UV4.exe'
$fromElfPath = 'E:\keil\ARM\ARMCC\bin\fromelf.exe'
$logDirectory = Join-Path $PSScriptRoot 'logs'

if (-not (Test-Path -LiteralPath $uv4Path)) {
    throw "Keil uVision was not found at: $uv4Path"
}

if (-not (Test-Path -LiteralPath $fromElfPath)) {
    throw "ARMCC5 fromelf was not found at: $fromElfPath"
}

New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null

$builds = @(
    [PSCustomObject]@{
        Name = 'Bootloader'
        Project = Join-Path $projectRoot 'firmware\bootloader\MDK-ARM\Bootloader.uvprojx'
        OutputDirectory = Join-Path $projectRoot 'firmware\bootloader\MDK-ARM\Objects'
        AxF = 'Bootloader.axf'
        Bin = 'Bootloader.bin'
        MaximumBytes = 0x20000
        LinkBase = '0x08000000'
    },
    [PSCustomObject]@{
        Name = 'App'
        Project = Join-Path $projectRoot 'firmware\app\MDK-ARM\App.uvprojx'
        OutputDirectory = Join-Path $projectRoot 'firmware\app\MDK-ARM\Objects'
        AxF = 'App.axf'
        Bin = 'App.bin'
        MaximumBytes = 0x40000
        LinkBase = '0x08040000'
    }
)

if ($Target -ne 'All') {
    $builds = @($builds | Where-Object Name -EQ $Target)
}

function Invoke-KeilBuild {
    param(
        [Parameter(Mandatory)]
        [PSCustomObject]$Build
    )

    if (-not (Test-Path -LiteralPath $Build.Project)) {
        throw "$($Build.Name) project was not found: $($Build.Project)"
    }

    $mdkDirectory = Split-Path -Parent $Build.Project
    $logPath = Join-Path $logDirectory "$($Build.Name).build.log"
    New-Item -ItemType Directory -Path $Build.OutputDirectory -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $mdkDirectory 'Listings') -Force | Out-Null
    if (Test-Path -LiteralPath $logPath) {
        Remove-Item -LiteralPath $logPath -Force
    }

    Write-Host "Building $($Build.Name) at link base $($Build.LinkBase)..."
    $process = Start-Process -FilePath $uv4Path `
        -ArgumentList @('-b', $Build.Project, '-j0', '-o', $logPath) `
        -WorkingDirectory $mdkDirectory `
        -Wait `
        -PassThru `
        -WindowStyle Hidden

    if (-not (Test-Path -LiteralPath $logPath)) {
        throw "$($Build.Name) did not produce a Keil build log. uVision exit code: $($process.ExitCode)"
    }

    $buildText = Get-Content -Raw -LiteralPath $logPath -Encoding Default
    Get-Content -LiteralPath $logPath -Encoding Default

    if (($process.ExitCode -ne 0) -or ($buildText -notmatch '0 Error\(s\)')) {
        throw "$($Build.Name) Keil build failed. Read: $logPath"
    }

    $axfPath = Join-Path $Build.OutputDirectory $Build.AxF
    $binPath = Join-Path $Build.OutputDirectory $Build.Bin
    if (-not (Test-Path -LiteralPath $axfPath)) {
        throw "$($Build.Name) passed without the expected AXF file: $axfPath"
    }

    if (Test-Path -LiteralPath $binPath) {
        Remove-Item -LiteralPath $binPath -Force
    }

    & $fromElfPath --bin --output $binPath $axfPath
    if ($LASTEXITCODE -ne 0) {
        throw "fromelf failed for $($Build.Name) with exit code $LASTEXITCODE"
    }

    $binary = Get-Item -LiteralPath $binPath
    if ($binary.Length -gt $Build.MaximumBytes) {
        throw ('{0} binary is {1} bytes, larger than its {2} byte Flash partition.' -f `
            $Build.Name, $binary.Length, $Build.MaximumBytes)
    }

    $hash = (Get-FileHash -LiteralPath $binPath -Algorithm SHA256).Hash
    [PSCustomObject]@{
        Target = $Build.Name
        LinkBase = $Build.LinkBase
        BinBytes = $binary.Length
        PartitionBytes = $Build.MaximumBytes
        FreeBytes = $Build.MaximumBytes - $binary.Length
        Sha256 = $hash
        BuildLog = $logPath
        Binary = $binPath
    }
}

$results = foreach ($build in $builds) {
    Invoke-KeilBuild -Build $build
}

Write-Host ''
Write-Host 'Keil build summary'
$results | Format-Table Target, LinkBase, BinBytes, PartitionBytes, FreeBytes -AutoSize
$results | Format-List Target, Sha256, BuildLog, Binary
