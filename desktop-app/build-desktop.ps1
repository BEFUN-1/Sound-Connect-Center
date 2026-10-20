param(
    [string]$NativeBuildDirectory = 'build-win-verified',
    [string]$CompilerBin = '',
    [string]$DotnetRoot = '',
    [string]$CMake = ''
)
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($CompilerBin)) {
    $compiler = Get-Command g++.exe -ErrorAction SilentlyContinue
    if ($null -eq $compiler) { throw 'MinGW g++ was not found. Pass -CompilerBin or add g++.exe to PATH.' }
    $CompilerBin = Split-Path -Parent $compiler.Source
}
$env:PATH = $CompilerBin + [IO.Path]::PathSeparator + $env:PATH

$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($DotnetRoot)) {
    $dotnetCommand = Get-Command dotnet.exe -ErrorAction SilentlyContinue
    if ($null -eq $dotnetCommand) { throw 'dotnet.exe was not found. Pass -DotnetRoot or add dotnet.exe to PATH.' }
    $dotnet = $dotnetCommand.Source
    $dotnetRoot = Split-Path -Parent $dotnet
} else {
    $dotnet = Join-Path $DotnetRoot 'dotnet.exe'
}
$env:APPDATA = Join-Path $repoRoot '..\.appdata'
$env:DOTNET_CLI_HOME = Join-Path $repoRoot '..\.dotnet-home'
$env:DOTNET_SKIP_FIRST_TIME_EXPERIENCE = '1'
$env:NUGET_PACKAGES = Join-Path $repoRoot '..\.nuget-packages'

$project = Join-Path $PSScriptRoot 'SonyDeviceCenter.Desktop.csproj'
$config = Join-Path $PSScriptRoot 'NuGet.Config'
$sdkOverride = Join-Path $repoRoot '..\.windows-sdk'
$dotnet = Join-Path $dotnetRoot 'dotnet.exe'
$runtimeRoot = Join-Path $dotnetRoot 'runtime'
$outputRoot = Join-Path $PSScriptRoot 'bin\Release\net8.0-windows'
$runtimeOutput = Join-Path $outputRoot 'runtime'
$mingwBin = $CompilerBin
$gxx = Join-Path $mingwBin 'g++.exe'
$backend = Join-Path $repoRoot "$NativeBuildDirectory\apps\sonyctl\sonyctl.exe"
if ([string]::IsNullOrWhiteSpace($CMake)) {
    $cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($null -eq $cmakeCommand) { throw 'cmake.exe was not found. Pass -CMake or add cmake.exe to PATH.' }
    $cmake = $cmakeCommand.Source
} else {
    $cmake = $CMake
}
& $cmake --build (Join-Path $repoRoot $NativeBuildDirectory) --target sonyctl --parallel 4
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $backend)) { throw 'Native backend build failed' }
& $backend --help *> $null
if ($LASTEXITCODE -ne 0) { throw 'Native backend smoke test failed; check its Windows linker configuration' }

New-Item -ItemType Directory -Force $env:APPDATA, $env:DOTNET_CLI_HOME, $env:NUGET_PACKAGES | Out-Null

& $dotnet restore $project --configfile $config --ignore-failed-sources `
  -p:TargetPlatformSdkRootOverride=$sdkOverride `
  -p:TargetPlatformDisplayName=Windows
if ($LASTEXITCODE -ne 0) { throw "dotnet restore failed with exit code $LASTEXITCODE" }

& $dotnet build $project -c Release --no-restore -p:UseAppHost=true "-p:SonyCtlPath=$backend" `
  -p:TargetPlatformSdkRootOverride=$sdkOverride `
  -p:TargetPlatformDisplayName=Windows
if ($LASTEXITCODE -ne 0) { throw "dotnet build failed with exit code $LASTEXITCODE" }

if (-not (Test-Path $gxx)) { throw "找不到 MinGW g++：$gxx" }
if (-not (Test-Path (Join-Path $dotnetRoot 'shared\Microsoft.NETCore.App\8.0.32'))) {
    throw "找不到 .NET 8 runtime：$dotnetRoot"
}

if (-not ([IO.Path]::GetFullPath($runtimeOutput)).StartsWith([IO.Path]::GetFullPath($outputRoot) + [IO.Path]::DirectorySeparatorChar)) { throw 'Runtime path escapes output directory' }
if (Test-Path $runtimeOutput) { Remove-Item -LiteralPath $runtimeOutput -Recurse -Force }
New-Item -ItemType Directory -Force $runtimeOutput | Out-Null
Copy-Item (Join-Path $dotnetRoot 'dotnet.exe') $runtimeOutput -Force
$runtimeHost = Join-Path $runtimeOutput 'host\fxr\8.0.32'
$runtimeCore = Join-Path $runtimeOutput 'shared\Microsoft.NETCore.App\8.0.32'
$runtimeDesktop = Join-Path $runtimeOutput 'shared\Microsoft.WindowsDesktop.App\8.0.32'
New-Item -ItemType Directory -Force $runtimeHost, $runtimeCore, $runtimeDesktop | Out-Null
Copy-Item (Join-Path $dotnetRoot 'host\fxr\8.0.32\*') $runtimeHost -Recurse -Force
Copy-Item (Join-Path $dotnetRoot 'shared\Microsoft.NETCore.App\8.0.32\*') $runtimeCore -Recurse -Force
Copy-Item (Join-Path $dotnetRoot 'shared\Microsoft.WindowsDesktop.App\8.0.32\*') $runtimeDesktop -Recurse -Force

foreach ($runtimeDll in @('libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll')) {
    if (Test-Path -LiteralPath (Join-Path $mingwBin $runtimeDll)) {
        Copy-Item (Join-Path $mingwBin $runtimeDll) $outputRoot -Force
    } else {
        Remove-Item -LiteralPath (Join-Path $outputRoot $runtimeDll) -Force -ErrorAction SilentlyContinue
    }
}

$launcher = Join-Path $PSScriptRoot 'Launcher.cpp'
$windres = Join-Path $mingwBin 'windres.exe'
if (-not (Test-Path $windres)) { throw "找不到 MinGW windres：$windres" }
$launcherResource = Join-Path $PSScriptRoot 'Launcher.rc'
$launcherObject = Join-Path $outputRoot 'Launcher-res.o'
& $windres $launcherResource $launcherObject
if ($LASTEXITCODE -ne 0) { throw "桌面启动器资源编译失败，退出码 $LASTEXITCODE" }
& $gxx -std=c++17 -municode -mwindows -static -static-libgcc -static-libstdc++ $launcher $launcherObject -o (Join-Path $outputRoot 'SonyDeviceCenter.exe') -lshell32
if ($LASTEXITCODE -ne 0) { throw "桌面启动器编译失败，退出码 $LASTEXITCODE" }
Remove-Item -LiteralPath $launcherObject -Force -ErrorAction SilentlyContinue

Write-Host "Built: $PSScriptRoot\bin\Release\net8.0-windows\SonyDeviceCenter.exe"
