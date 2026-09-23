[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^(?i:COM)[0-9]+$')]
    [string]$Port,

    [Parameter(Mandatory = $true)]
    [string]$Firmware
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$EsptoolVersion = '5.1.0'
$EsptoolUrl = 'https://github.com/espressif/esptool/releases/download/v5.1.0/esptool-v5.1.0-windows-amd64.zip'
$ExpectedZipSha256 = 'f68a8f7728adfc59cd60f9424928199e76eac66372c7bdc23898aa32753a437a'
$CacheRoot = Join-Path $env:LOCALAPPDATA 'RM-Handheld\tools'
$ZipPath = Join-Path $CacheRoot "esptool-v$EsptoolVersion-windows-amd64.zip"
$ExtractPath = Join-Path $CacheRoot "esptool-v$EsptoolVersion-windows-amd64"

function Test-OfficialArchive {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $false
    }

    $Actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    return $Actual -eq $ExpectedZipSha256
}

try {
    $Firmware = (Resolve-Path -LiteralPath $Firmware).Path
    New-Item -ItemType Directory -Path $CacheRoot -Force | Out-Null

    if (-not (Test-OfficialArchive -Path $ZipPath)) {
        if (Test-Path -LiteralPath $ZipPath) {
            Remove-Item -LiteralPath $ZipPath -Force
        }

        $DownloadPath = "$ZipPath.download"
        if (Test-Path -LiteralPath $DownloadPath) {
            Remove-Item -LiteralPath $DownloadPath -Force
        }

        Write-Host "Downloading official Espressif esptool v$EsptoolVersion (about 60 MB)..." -ForegroundColor Cyan
        Invoke-WebRequest -UseBasicParsing -Uri $EsptoolUrl -OutFile $DownloadPath

        if (-not (Test-OfficialArchive -Path $DownloadPath)) {
            throw 'The downloaded Espressif ZIP failed its official SHA-256 check. It will not be run.'
        }

        Move-Item -LiteralPath $DownloadPath -Destination $ZipPath -Force
    }

    $Esptool = $null
    if (Test-Path -LiteralPath $ExtractPath -PathType Container) {
        $Esptool = Get-ChildItem -LiteralPath $ExtractPath -Filter 'esptool.exe' -File -Recurse | Select-Object -First 1
    }

    if ($null -eq $Esptool) {
        if (Test-Path -LiteralPath $ExtractPath) {
            Remove-Item -LiteralPath $ExtractPath -Recurse -Force
        }
        New-Item -ItemType Directory -Path $ExtractPath -Force | Out-Null
        Write-Host 'Extracting the verified Espressif tool...' -ForegroundColor Cyan
        Expand-Archive -LiteralPath $ZipPath -DestinationPath $ExtractPath -Force
        $Esptool = Get-ChildItem -LiteralPath $ExtractPath -Filter 'esptool.exe' -File -Recurse | Select-Object -First 1
    }

    if ($null -eq $Esptool) {
        throw 'The verified Espressif package did not contain esptool.exe.'
    }

    Write-Host 'Checking the flashing tool...' -ForegroundColor Cyan
    & $Esptool.FullName version
    if ($LASTEXITCODE -ne 0) {
        throw "esptool could not start (exit code $LASTEXITCODE)."
    }

    Write-Host "Connecting to ESP32-S3 on $Port and flashing v0.1.5..." -ForegroundColor Cyan
    & $Esptool.FullName --chip esp32s3 --port $Port --baud 460800 --before default-reset --after hard-reset write-flash 0x0 $Firmware
    if ($LASTEXITCODE -ne 0) {
        throw "Flashing failed (esptool exit code $LASTEXITCODE)."
    }

    Write-Host 'Flash command completed successfully.' -ForegroundColor Green
    exit 0
}
catch {
    Write-Host ''
    Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
