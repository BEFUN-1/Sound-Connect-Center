param(
    [string]$OutputPath = (Join-Path $PSScriptRoot 'xm6-power-observations.json'),
    [switch]$IncludeRemoval
)

# Windows adaptation of diagnosing-bugs/scripts/hitl-loop.template.sh.
# This records human observations; it sends no Bluetooth commands.
$ErrorActionPreference = 'Stop'

function Step([string]$Instruction) {
    Write-Host "`n>>> $Instruction"
    [void](Read-Host 'Press Enter when ready')
}

function Capture([string]$Question, [string[]]$Choices) {
    do {
        $answer = (Read-Host "$Question [$($Choices -join '/')]").Trim().ToLowerInvariant()
    } while ($answer -notin $Choices)
    return $answer
}

$result = [ordered]@{
    recordedAt = (Get-Date).ToString('o')
    model = 'WH-1000XM6'
    firmware = (Read-Host 'Headset firmware version')
    applicationVersion = (Read-Host 'Desktop app version being tested')
    methods = [ordered]@{}
}

foreach ($method in @('physicalButton', 'officialSoundConnect', 'desktopApp')) {
    $instruction = switch ($method) {
        'physicalButton' { 'Wear the powered-on headset. Hold its physical power button to turn it off.' }
        'officialSoundConnect' { 'Power on and wear the headset. Turn it off from Sony Sound Connect, if available.' }
        'desktopApp' { 'Power on and wear the headset. Use Power Off in the desktop app under test.' }
    }
    Step $instruction
    $poweredOff = Capture 'Did the headset actually power off?' @('yes', 'no', 'unavailable')
    $sound = if ($poweredOff -eq 'yes') {
        Capture 'Did you hear the headset shutdown sound?' @('yes', 'no', 'uncertain')
    } else { 'unobserved' }
    $result.methods[$method] = [ordered]@{ poweredOff = $poweredOff; sound = $sound }
}

$official = $result.methods.officialSoundConnect
$desktop = $result.methods.desktopApp
$result.soundComparison = if ($official.poweredOff -eq 'yes' -and $desktop.poweredOff -eq 'yes') {
    if ($official.sound -eq 'yes' -and $desktop.sound -eq 'no') {
        'FAIL: desktop shutdown is silent while official remote shutdown has sound'
    } elseif ($official.sound -eq 'no' -and $desktop.sound -eq 'no') {
        'Both remote shutdown methods were silent; this does not explain the firmware behavior'
    } elseif ($official.sound -eq 'yes' -and $desktop.sound -eq 'yes') {
        'PASS: both remote shutdown methods produced sound'
    } else { 'Inconclusive: repeat the listening comparison' }
} else { 'Inconclusive: no successful official/desktop shutdown comparison' }

if ($IncludeRemoval) {
    Step 'Power on and wear the headset. Enable Auto Power-Off in the desktop app. Stop playback.'
    Step 'Remove the headset and place it with the inner wearing sensor uncovered. Start observing now.'
    $started = Get-Date
    [void](Read-Host 'After about 15 minutes, check whether it powered off; then press Enter')
    $result.removal = [ordered]@{
        startedAt = $started.ToString('o')
        observationRecordedAfterSeconds = [math]::Round(((Get-Date) - $started).TotalSeconds)
        poweredOff = Capture 'Did the headset actually power off (not merely pause/disconnect)?' @('yes', 'no', 'uncertain')
    }
}

$result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $OutputPath -Encoding utf8
Write-Host "`n$($result.soundComparison)"
Write-Host "Observations: $OutputPath"
