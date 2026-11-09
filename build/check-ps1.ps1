# Parse every PowerShell script in build/ without running any of them, so a
# syntax error is caught here rather than in the middle of a device operation.
#   powershell -NoProfile -ExecutionPolicy Bypass -File build/check-ps1.ps1
$bad = 0
Get-ChildItem -Path (Join-Path $PSScriptRoot '*.ps1') | ForEach-Object {
    $errs = $null
    [System.Management.Automation.Language.Parser]::ParseFile(
        $_.FullName, [ref]$null, [ref]$errs) | Out-Null
    if ($errs -and $errs.Count) {
        $bad++
        Write-Output "FAIL $($_.Name)"
        $errs | ForEach-Object { Write-Output ("     line {0}: {1}" -f $_.Extent.StartLineNumber, $_.Message) }
    } else {
        Write-Output "ok   $($_.Name)"
    }
}
if ($bad) { Write-Output "PS1 PARSE FAILED ($bad file(s))"; exit 1 }
Write-Output "PS1 PARSE OK"
