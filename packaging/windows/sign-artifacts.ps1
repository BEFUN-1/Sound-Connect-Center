param(
    [Parameter(Mandatory)][string[]]$Files,
    [Parameter(Mandatory)][string]$CertificateThumbprint,
    [Parameter(Mandatory)][string]$TimestampServer,
    [string]$SignTool = ''
)
$ErrorActionPreference = 'Stop'
if ($CertificateThumbprint -notmatch '^[A-Fa-f0-9]{40}$') {
    throw 'Expected a SHA-1 certificate thumbprint from the current-user certificate store.'
}
if ($TimestampServer -notmatch '^https?://') {
    throw 'Provide the RFC 3161 timestamp endpoint required by your certificate provider.'
}
if ([string]::IsNullOrWhiteSpace($SignTool)) {
    $command = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($command) {
        $SignTool = $command.Source
    } else {
        $sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
        $candidate = Get-ChildItem -Path "$sdkRoot\*\x64\signtool.exe" -File |
            Sort-Object FullName -Descending | Select-Object -First 1
        if ($candidate) { $SignTool = $candidate.FullName }
    }
}
if (-not $SignTool -or -not (Test-Path -LiteralPath $SignTool -PathType Leaf)) {
    throw 'Windows SDK signtool.exe was not found.'
}
foreach ($file in $Files) {
    $path = (Resolve-Path -LiteralPath $file).Path
    & $SignTool sign /fd SHA256 /td SHA256 /tr $TimestampServer /sha1 $CertificateThumbprint $path
    if ($LASTEXITCODE -ne 0) { throw "Authenticode signing failed: $path" }
    & $SignTool verify /pa /all /tw $path
    if ($LASTEXITCODE -ne 0) { throw "Authenticode verification failed: $path" }
}
