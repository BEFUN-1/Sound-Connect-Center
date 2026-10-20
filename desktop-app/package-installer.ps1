param(
    [string]$MakeNsis = ''
)
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($MakeNsis)) {
    $nsisCommand = Get-Command makensis.exe -ErrorAction SilentlyContinue
    if ($null -eq $nsisCommand) { throw 'makensis.exe was not found. Pass -MakeNsis or add it to PATH.' }
    $MakeNsis = $nsisCommand.Source
}
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputRoot = Join-Path $PSScriptRoot 'bin\Release\net8.0-windows'
$stagingRoot = Join-Path $PSScriptRoot 'installer\payload'
$target = Join-Path $repoRoot 'SonyDeviceCenter-Setup.exe'
$zipTarget = Join-Path $repoRoot 'SonyDeviceCenter-Windows-x64.zip'
$scriptPath = Join-Path $repoRoot 'packaging\windows\desktop-installer.nsi'
function Get-RelativePathCompat([string]$BasePath, [string]$TargetPath) {
    $base = (Resolve-Path -LiteralPath $BasePath).Path.TrimEnd('\') + '\'
    $target = (Resolve-Path -LiteralPath $TargetPath).Path
    $baseUri = New-Object System.Uri($base)
    $targetUri = New-Object System.Uri($target)
    return [System.Uri]::UnescapeDataString($baseUri.MakeRelativeUri($targetUri).ToString()).Replace('/','\')
}
foreach ($file in @('SonyDeviceCenter.exe','SonyDeviceCenter.dll','sonyctl.exe','runtime\dotnet.exe','runtime\shared\Microsoft.WindowsDesktop.App\8.0.32\System.Windows.Forms.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $outputRoot $file))) { throw "Missing package file: $file. Run build-desktop.ps1 first." }
}
if (-not (Test-Path -LiteralPath $MakeNsis)) { throw "NSIS compiler not found: $MakeNsis" }
if (-not ([IO.Path]::GetFullPath($stagingRoot)).StartsWith([IO.Path]::GetFullPath($PSScriptRoot) + [IO.Path]::DirectorySeparatorChar)) { throw 'Staging path escapes desktop source' }
if (Test-Path -LiteralPath $stagingRoot) { Remove-Item -LiteralPath $stagingRoot -Recurse -Force }
New-Item -ItemType Directory -Force $stagingRoot | Out-Null
Copy-Item -Path (Join-Path $outputRoot '*') -Destination $stagingRoot -Recurse -Force
Get-ChildItem -LiteralPath $stagingRoot -Recurse -File | Where-Object {
    $_.Name -like '*.pdb' -or $_.Name -like 'hardware-*.log' -or $_.Name -like 'frontend-*.png' -or $_.Name -like '*preview*.png' -or $_.Name -like 'test*.png'
} | Remove-Item -Force
$obsolete = Join-Path $stagingRoot 'win-x64'
if (Test-Path -LiteralPath $obsolete) { Remove-Item -LiteralPath $obsolete -Recurse -Force }
Set-Content -LiteralPath (Join-Path $stagingRoot 'instance-control-v1') -Value '1' -Encoding utf8
$manifestDirectory = Join-Path $PSScriptRoot 'installer'
$manifest = Join-Path $manifestDirectory 'uninstall-files.nsh'
$files = @(Get-ChildItem -LiteralPath $stagingRoot -Recurse -File)
$lines = [Collections.Generic.List[string]]::new()
foreach ($file in $files) {
    $relative = Get-RelativePathCompat $stagingRoot $file.FullName
    $lines.Add('Delete "$INSTDIR\' + $relative.Replace('$','$$') + '"')
}
Get-ChildItem -LiteralPath $stagingRoot -Recurse -Directory | Sort-Object { $_.FullName.Length } -Descending | ForEach-Object {
    $lines.Add('RMDir "$INSTDIR\' + (Get-RelativePathCompat $stagingRoot $_.FullName).Replace('$','$$') + '"')
}
[IO.File]::WriteAllLines($manifest,$lines,[Text.UTF8Encoding]::new($false))
& $MakeNsis '/V3' '/INPUTCHARSET' 'UTF8' "/DPAYLOAD_DIR=$stagingRoot" "/DOUTPUT_FILE=$target" "/DREPO_DIR=$repoRoot" "/DUNINSTALL_MANIFEST=$manifest" $scriptPath
if ($LASTEXITCODE -ne 0) { throw "NSIS failed: $LASTEXITCODE" }
if (Test-Path -LiteralPath $zipTarget) { Remove-Item -LiteralPath $zipTarget -Force }
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::Open($zipTarget,[IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in $files) {
        $relative = (Get-RelativePathCompat $stagingRoot $file.FullName).Replace('\','/')
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive,$file.FullName,$relative,[IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
} finally { $archive.Dispose() }
Get-Item -LiteralPath $target,$zipTarget | Select-Object FullName,Length
Get-FileHash -LiteralPath $target,$zipTarget -Algorithm SHA256
