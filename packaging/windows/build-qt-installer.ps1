param(
    [string]$BuildDir = '',
    [string]$MakeNsis = '',
    [string]$OutputDir = '',
    [switch]$Development,
    [string]$SigningCertificateThumbprint = $env:SONY_WINDOWS_SIGNING_THUMBPRINT,
    [string]$TimestampServer = $env:SONY_WINDOWS_TIMESTAMP_SERVER,
    [string]$SignTool = ''
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $Development) {
    python (Join-Path $repoRoot 'packaging\verify-release-source.py')
    if ($LASTEXITCODE -ne 0) { throw 'Release source verification failed.' }
    if ([string]::IsNullOrWhiteSpace($SigningCertificateThumbprint)) {
        throw 'Production releases require Authenticode. Supply -SigningCertificateThumbprint and -TimestampServer.'
    }
}
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $repoRoot 'build-win-release'
}
$buildRoot = (Resolve-Path -LiteralPath $BuildDir).Path
$sourceMatch = Select-String -LiteralPath (Join-Path $buildRoot 'CMakeCache.txt') `
    -Pattern '^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$'
if ($null -eq $sourceMatch -or
    [IO.Path]::GetFullPath($sourceMatch.Matches[0].Groups[1].Value) -ne [IO.Path]::GetFullPath($repoRoot)) {
    throw 'Build directory belongs to a different source checkout.'
}
cmake -S $repoRoot -B $buildRoot
if ($LASTEXITCODE -ne 0) { throw 'CMake reconfiguration failed.' }
cmake --build $buildRoot --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }
if (-not [string]::IsNullOrWhiteSpace($SigningCertificateThumbprint)) {
    $applicationFiles = @(
        'apps\device-center\sony-device-center.exe',
        'apps\sonyctl\sonyctl.exe',
        'apps\sonyd\sonyd.exe'
    ) | ForEach-Object {
        $candidate = Join-Path $buildRoot $_
        if (-not (Test-Path -LiteralPath $candidate)) {
            $candidate = Join-Path (Split-Path -Parent $candidate) ('Release\' + (Split-Path -Leaf $candidate))
        }
        $candidate
    }
    & (Join-Path $PSScriptRoot 'sign-artifacts.ps1') -Files $applicationFiles `
        -CertificateThumbprint $SigningCertificateThumbprint -TimestampServer $TimestampServer -SignTool $SignTool
}
$stagingRoot = Join-Path $buildRoot 'qt-installer-payload'
$versionMatch = Select-String -LiteralPath (Join-Path $buildRoot 'CMakeCache.txt') `
    -Pattern '^CMAKE_PROJECT_VERSION:STATIC=(\d+\.\d+\.\d+)$'
if ($null -eq $versionMatch) {
    throw 'CMakeCache.txt does not contain a release version.'
}
$releaseVersion = $versionMatch.Matches[0].Groups[1].Value
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $buildRoot 'artifacts'
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$OutputDir = (Resolve-Path -LiteralPath $OutputDir).Path
$suffix = if ($Development) { '-dev' } else { '' }
$outputFile = Join-Path $OutputDir "SonyDeviceCenter-Qt2-$releaseVersion$suffix-Setup.exe"
$scriptPath = Join-Path $PSScriptRoot 'qt-installer.nsi'
$manifestPath = Join-Path $buildRoot 'qt-uninstall-files.nsh'

if ([string]::IsNullOrWhiteSpace($MakeNsis)) {
    $nsisCommand = Get-Command makensis.exe -ErrorAction SilentlyContinue
    if ($null -eq $nsisCommand) {
        throw 'makensis.exe was not found. Pass -MakeNsis or add it to PATH.'
    }
    $MakeNsis = $nsisCommand.Source
}
if (-not (Test-Path -LiteralPath $MakeNsis)) {
    throw "NSIS compiler not found: $MakeNsis"
}

$buildPrefix = [IO.Path]::GetFullPath($buildRoot).TrimEnd('\') + '\'
$stagingFull = [IO.Path]::GetFullPath($stagingRoot)
if (-not $stagingFull.StartsWith($buildPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Staging path escapes the build directory.'
}
if (Test-Path -LiteralPath $stagingRoot) {
    Remove-Item -LiteralPath $stagingRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $stagingRoot | Out-Null

cmake --install $buildRoot --config Release --prefix $stagingRoot
if ($LASTEXITCODE -ne 0) {
    throw "cmake --install failed with exit code $LASTEXITCODE."
}

$payloadExe = Join-Path $stagingRoot 'bin\sony-device-center.exe'
if (-not (Test-Path -LiteralPath $payloadExe)) {
    throw "Qt payload is missing: $payloadExe"
}

# A Windows loader reports the first missing Qt DLL it encounters. Check the
# complete runtime tree before invoking NSIS so a broken deployment cannot be
# distributed as a valid-looking installer.
$requiredQtFiles = @(
    'bin\Qt6Core.dll',
    'bin\Qt6Gui.dll',
    'bin\Qt6Qml.dll',
    'bin\Qt6Quick.dll',
    'bin\Qt6QuickControls2.dll',
    'bin\platforms\qwindows.dll',
    'bin\qml\QtQml\qmldir',
    'bin\qml\QtQuick\qmldir',
    'bin\qml\QtQuick\Controls\qmldir'
)
$cache = Get-Content -LiteralPath (Join-Path $buildRoot 'CMakeCache.txt') -Raw
if ($cache -match 'CMAKE_CXX_COMPILER:[^\r\n]*[\\/](g\+\+|c\+\+)\.exe') {
    $requiredQtFiles += @('bin\libgcc_s_seh-1.dll', 'bin\libstdc++-6.dll', 'bin\libwinpthread-1.dll')
} else {
    $requiredQtFiles += @('bin\MSVCP140.dll', 'bin\VCRUNTIME140.dll', 'bin\VCRUNTIME140_1.dll')
}
$missingQtFiles = @(
    $requiredQtFiles |
        ForEach-Object {
            $candidate = Join-Path $stagingRoot $_
            if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
                $_
            }
        }
)
if ($missingQtFiles.Count -gt 0) {
    throw ("Qt runtime deployment is incomplete. Missing: " +
        ($missingQtFiles -join ', ') +
        ". Check the Qt installation and windeployqt path.")
}

function Get-RelativePathCompat([string]$BasePath, [string]$TargetPath) {
    $base = (Resolve-Path -LiteralPath $BasePath).Path.TrimEnd('\') + '\'
    $target = (Resolve-Path -LiteralPath $TargetPath).Path
    $baseUri = New-Object System.Uri($base)
    $targetUri = New-Object System.Uri($target)
    return [System.Uri]::UnescapeDataString($baseUri.MakeRelativeUri($targetUri).ToString()).Replace('/', '\')
}

$files = @(Get-ChildItem -LiteralPath $stagingRoot -Recurse -File)
$lines = [Collections.Generic.List[string]]::new()
foreach ($file in $files) {
    $relative = Get-RelativePathCompat $stagingRoot $file.FullName
    $lines.Add('Delete "$INSTDIR\' + $relative.Replace('$', '$$') + '"')
}
Get-ChildItem -LiteralPath $stagingRoot -Recurse -Directory |
    Sort-Object { $_.FullName.Length } -Descending |
    ForEach-Object {
        $relative = Get-RelativePathCompat $stagingRoot $_.FullName
        $lines.Add('RMDir "$INSTDIR\' + $relative.Replace('$', '$$') + '"')
    }
[IO.File]::WriteAllLines($manifestPath, $lines, [Text.UTF8Encoding]::new($false))

& $MakeNsis '/V3' '/INPUTCHARSET' 'UTF8' `
    "/DPAYLOAD_DIR=$stagingRoot" `
    "/DOUTPUT_FILE=$outputFile" `
    "/DREPO_DIR=$repoRoot" `
    "/DVERSION=$releaseVersion" `
    "/DUNINSTALL_MANIFEST=$manifestPath" `
    $scriptPath
if ($LASTEXITCODE -ne 0) {
    throw "NSIS failed with exit code $LASTEXITCODE."
}
if (-not [string]::IsNullOrWhiteSpace($SigningCertificateThumbprint)) {
    & (Join-Path $PSScriptRoot 'sign-artifacts.ps1') -Files @($outputFile) `
        -CertificateThumbprint $SigningCertificateThumbprint -TimestampServer $TimestampServer -SignTool $SignTool
}

$sha256 = [Security.Cryptography.SHA256]::Create()
try {
    $stream = [IO.File]::OpenRead($outputFile)
    try {
        $digest = $sha256.ComputeHash($stream)
    } finally {
        $stream.Dispose()
    }
} finally {
    $sha256.Dispose()
}
[PSCustomObject]@{
    Algorithm = 'SHA256'
    Hash = ([BitConverter]::ToString($digest) -replace '-', '').ToLowerInvariant()
    Path = $outputFile
}
