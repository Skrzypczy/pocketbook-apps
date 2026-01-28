param(
    [string]$DriveLetter
)

$ErrorActionPreference = 'SilentlyContinue'

if (-not $DriveLetter) {
    Write-Host "Usage: eject_usb.ps1 -DriveLetter 'D:'" -ForegroundColor Yellow
    exit 2
}

$driveLetterNormalized = $DriveLetter.TrimEnd('\')
if ($driveLetterNormalized -notmatch '^[A-Za-z]:$') {
    Write-Host "Invalid drive letter: $DriveLetter" -ForegroundColor Red
    exit 3
}

Write-Host "Attempting safe eject for $driveLetterNormalized" -ForegroundColor Yellow

# Flush any cached file buffers (best-effort)
[void][System.IO.File]::Open("$driveLetterNormalized\\$null", 'Open', 'Read') 2>$null
Start-Sleep -Milliseconds 500

$driveEject = New-Object -ComObject Shell.Application

# Try to dismount the volume first (requires admin, may fail without elevation)
try {
    $vol = Get-CimInstance -ClassName Win32_Volume -Filter "DriveLetter = '$driveLetterNormalized'"
    if ($vol) {
        Write-Host "Found Win32_Volume for $driveLetterNormalized - attempting Dismount()" -ForegroundColor Cyan
        $dismountResult = $vol | Invoke-CimMethod -MethodName Dismount -Arguments @{Force=$false; Permanent=$false}
        Write-Host "Dismount return: $($dismountResult.ReturnValue)" -ForegroundColor Gray
        Start-Sleep -Seconds 1
    } else {
        Write-Host "Win32_Volume for $driveLetterNormalized not found (may be OK)." -ForegroundColor Gray
    }
} catch {
    Write-Host "Dismount attempt failed (continuing): $($_.Exception.Message)" -ForegroundColor DarkYellow
}

# Try Shell COM eject with retries
$maxRetries = 4
$attempt = 0
$ejected = $false
while ($attempt -lt $maxRetries -and -not $ejected) {
    $attempt++
    Write-Host "Eject attempt $attempt for $driveLetterNormalized" -ForegroundColor Yellow
    try {
        $driveEject.Namespace(17).ParseName("$driveLetterNormalized\\").InvokeVerb('Eject')
    } catch {
        Write-Host "InvokeVerb failed: $($_.Exception.Message)" -ForegroundColor DarkYellow
    }
    Start-Sleep -Seconds 2
    # Check whether the drive path still exists
    if (-not (Test-Path "$driveLetterNormalized\\system\config")) {
        $ejected = $true
        break
    }
}

if ($ejected) {
    Write-Host "Drive $driveLetterNormalized appears ejected." -ForegroundColor Green
    exit 0
} else {
    Write-Host "Eject did not succeed for $driveLetterNormalized." -ForegroundColor Red
    exit 1
}
