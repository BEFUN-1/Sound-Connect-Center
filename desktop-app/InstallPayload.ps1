$ErrorActionPreference = 'Stop'

$payloadRoot = $PSScriptRoot
$installRoot = Join-Path $env:LOCALAPPDATA 'Programs\Sony Device Center'
$exe = Join-Path $installRoot 'SonyDeviceCenter.exe'
$desktopShortcutPath = Join-Path ([Environment]::GetFolderPath('Desktop')) 'Sony Device Center.lnk'
$startMenuShortcutPath = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Sony Device Center.lnk'

Get-Process -Name 'SonyDeviceCenter' -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $installRoot | Out-Null
Copy-Item (Join-Path $payloadRoot '*') $installRoot -Recurse -Force

$uninstaller = Join-Path $installRoot 'Uninstall-SonyDeviceCenter.ps1'
$uninstallScript = @'
$ErrorActionPreference = 'Stop'
$installRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$desktop = Join-Path ([Environment]::GetFolderPath('Desktop')) 'Sony Device Center.lnk'
$startMenu = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Sony Device Center.lnk'
Remove-Item -LiteralPath $desktop -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $startMenu -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\SonyDeviceCenter' -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $installRoot -Recurse -Force
'@
Set-Content -LiteralPath $uninstaller -Value $uninstallScript -Encoding UTF8

$shell = New-Object -ComObject WScript.Shell
$startMenuDir = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs'
New-Item -ItemType Directory -Force $startMenuDir | Out-Null
foreach ($shortcutPath in @($startMenuShortcutPath, $desktopShortcutPath)) {
    $shortcut = $shell.CreateShortcut($shortcutPath)
    $shortcut.TargetPath = $exe
    $shortcut.Arguments = ''
    $shortcut.WorkingDirectory = $installRoot
    $shortcut.Description = 'Sony Device Center'
    $shortcut.IconLocation = "$exe,0"
    try { $shortcut.Save() } catch { Write-Warning "无法创建快捷方式 $shortcutPath：$($_.Exception.Message)" }
}

$uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\SonyDeviceCenter'
try {
    New-Item -Path $uninstallKey -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name DisplayName -Value 'Sony Device Center' -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name DisplayVersion -Value '1.0.0' -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name InstallLocation -Value $installRoot -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name UninstallString -Value "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$uninstaller`"" -PropertyType String -Force | Out-Null
} catch { Write-Warning "无法写入卸载信息：$($_.Exception.Message)" }

Start-Process -FilePath $exe -WorkingDirectory $installRoot
