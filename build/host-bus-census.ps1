$ErrorActionPreference = 'Continue'
Write-Output ("now = " + (Get-Date).ToString('HH:mm:ss'))
Write-Output "--- present: anything on a USB port that isn't a keyboard/mouse/hub ---"
Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -match '^USB\\' } |
    Sort-Object Status, FriendlyName |
    Format-Table Status, Class, ConfigManagerErrorCode, FriendlyName -AutoSize |
    Out-String -Width 200 | Write-Output
Write-Output "--- USBSTOR / WPDBUSENUM nodes, present or not ---"
Get-PnpDevice | Where-Object { $_.InstanceId -match 'USBSTOR|WPDBUSENUM|QEAF' } |
    Format-Table Status, Class, ConfigManagerErrorCode, Name -AutoSize |
    Out-String -Width 220 | Write-Output
Write-Output "--- disks and volumes right now ---"
Get-Disk | Format-Table Number, FriendlyName, Size, HealthStatus, PartitionStyle -AutoSize | Out-String -Width 200 | Write-Output
Get-Volume | Format-Table DriveLetter, FileSystemLabel, HealthStatus, Size -AutoSize | Out-String -Width 200 | Write-Output
