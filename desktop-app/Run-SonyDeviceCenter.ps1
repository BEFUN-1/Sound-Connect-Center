$ErrorActionPreference = 'Stop'

$appDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$dotnet = Join-Path $appDir 'runtime\dotnet.exe'
$dll = Join-Path $appDir 'SonyDeviceCenter.dll'

if (-not (Test-Path $dotnet)) {
    throw "找不到 .NET 8 运行时：$dotnet"
}
if (-not (Test-Path $dll)) {
    throw "找不到桌面程序：$dll"
}

& $dotnet $dll
