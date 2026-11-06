param(
    [Parameter(Mandatory = $true)]
    [string]$ExtCfg
)

if (-not (Test-Path $ExtCfg)) {
    Write-Error "Missing $ExtCfg"
    exit 1
}

$lines = Get-Content -LiteralPath $ExtCfg -Encoding UTF8
$out = New-Object System.Collections.Generic.List[string]
$exts = @('epub', 'fb2', 'fb2.zip', 'txt', 'html', 'htm', 'rtf', 'pdf')

foreach ($line in $lines) {
    $matched = $false
    foreach ($ext in $exts) {
        if ($line -match ("^" + [regex]::Escape($ext) + ":")) {
            $matched = $true
            if ($line -notmatch 'Reader\.app') {
                # Insert Reader.app as first handler after the type descriptor fields.
                # PocketBook format: ext:@TYPE:flags:handler1,handler2,...:ICON
                $parts = $line.Split(':')
                if ($parts.Length -ge 4) {
                    $handlers = $parts[3]
                    if ($handlers -and $handlers.Length -gt 0) {
                        $parts[3] = "Reader.app,$handlers"
                    } else {
                        $parts[3] = "Reader.app"
                    }
                    $out.Add(($parts -join ':')) | Out-Null
                } else {
                    $out.Add($line) | Out-Null
                }
            } else {
                # Already registered — move Reader.app to front of handler list
                $parts = $line.Split(':')
                if ($parts.Length -ge 4) {
                    $handlers = @($parts[3].Split(',') | Where-Object { $_ -and $_.Trim() -ne '' })
                    $handlers = @('Reader.app') + @($handlers | Where-Object { $_ -ne 'Reader.app' })
                    $parts[3] = ($handlers -join ',')
                    $out.Add(($parts -join ':')) | Out-Null
                } else {
                    $out.Add($line) | Out-Null
                }
            }
            break
        }
    }
    if (-not $matched) {
        $out.Add($line) | Out-Null
    }
}

Set-Content -LiteralPath $ExtCfg -Value $out -Encoding UTF8
exit 0
