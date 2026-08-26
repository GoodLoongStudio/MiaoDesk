param(
    [string]$DiagnosticsDir = ''
)

$ErrorActionPreference = 'Stop'

if (-not $DiagnosticsDir) {
    $base = if ($env:LOCALAPPDATA) { $env:LOCALAPPDATA } else { [IO.Path]::GetTempPath() }
    $DiagnosticsDir = Join-Path (Join-Path $base 'TuringDesk') 'Diagnostics'
}

function Read-ReportMap([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "M3 phase report is missing: $Path"
    }
    $map = @{}
    foreach ($line in Get-Content -LiteralPath $Path -ErrorAction Stop) {
        $trimmed = $line.Trim()
        if (-not $trimmed -or $trimmed.StartsWith('[')) { continue }
        $parts = $trimmed.Split('=', 2)
        if ($parts.Count -eq 2 -and -not $map.ContainsKey($parts[0])) {
            $map[$parts[0]] = $parts[1]
        }
    }
    $map
}

$baselineSessionPath = Join-Path $DiagnosticsDir 'widget-acceptance-baseline.session'
if (-not (Test-Path -LiteralPath $baselineSessionPath -PathType Leaf)) {
    throw 'M3 baseline Windows session checkpoint is missing.'
}
$baselineSessionText = (Get-Content -LiteralPath $baselineSessionPath -Raw -ErrorAction Stop).Trim()
$baselineSessionId = 0
if (-not [int]::TryParse($baselineSessionText, [ref]$baselineSessionId) -or $baselineSessionId -lt 0) {
    throw "M3 baseline Windows session checkpoint is malformed: '$baselineSessionText'"
}

$phaseOrder = @('baseline','settings','search','explorer','monitor')
foreach ($phase in $phaseOrder) {
    $reportPath = Join-Path $DiagnosticsDir "widget-acceptance-$phase.txt"
    $report = Read-ReportMap -Path $reportPath
    foreach ($required in @('phase','baselineStatus','sessionStatus','sessionId','sequenceStatus','enabledWebCount','runtimeReported','runtimeHealthy')) {
        if (-not $report.ContainsKey($required)) {
            throw "M3 $phase report is missing required field: $required"
        }
    }
    if ([string]$report['phase'] -ne $phase) {
        throw "M3 phase report label mismatch: expected=$phase actual=$($report['phase'])"
    }
    if ([int]$report['sessionId'] -ne $baselineSessionId) {
        throw "M3 $phase report came from a different Windows session. baselineSession=$baselineSessionId reportSession=$($report['sessionId'])"
    }
    $expectedBaselineStatus = if ($phase -eq 'baseline') { 'recorded' } else { 'matched' }
    $expectedSessionStatus = if ($phase -eq 'baseline') { 'recorded' } else { 'matched' }
    if ([string]$report['baselineStatus'] -ne $expectedBaselineStatus) {
        throw "M3 $phase report baselineStatus is not successful: '$($report['baselineStatus'])'"
    }
    if ([string]$report['sessionStatus'] -ne $expectedSessionStatus) {
        throw "M3 $phase report sessionStatus is not successful: '$($report['sessionStatus'])'"
    }
    if ([int]$report['enabledWebCount'] -le 0) {
        throw "M3 $phase report contains no enabled Web Widget."
    }
    if ([string]$report['runtimeReported'] -ne 'true' -or [string]$report['runtimeHealthy'] -ne 'true') {
        throw "M3 $phase report runtime health is not successful. runtimeReported=$($report['runtimeReported']) runtimeHealthy=$($report['runtimeHealthy'])"
    }

    $raw = Get-Content -LiteralPath $reportPath -Raw -ErrorAction Stop
    $widgetCount = ([regex]::Matches($raw, '(?m)^\[widget\s+')).Count
    $healthyCount = ([regex]::Matches($raw, '(?m)^renderingHealthy=true\s*$')).Count
    if ($widgetCount -ne [int]$report['enabledWebCount'] -or $healthyCount -ne $widgetCount) {
        throw "M3 $phase report does not prove every enabled Widget surface rendered healthy. enabled=$($report['enabledWebCount']) sections=$widgetCount renderingHealthy=$healthyCount"
    }
}

$attestationPath = Join-Path $DiagnosticsDir 'widget-acceptance-human-visual.json'
if (-not (Test-Path -LiteralPath $attestationPath -PathType Leaf)) {
    throw 'M3 human visual acceptance attestation is missing.'
}
$attestation = Get-Content -LiteralPath $attestationPath -Raw -ErrorAction Stop | ConvertFrom-Json
if ($attestation.schema -ne 'turingdesk.widget-visual-acceptance.v1') {
    throw "M3 human visual acceptance schema is unexpected: '$($attestation.schema)'"
}
if ([int]$attestation.sessionId -ne $baselineSessionId) {
    throw "M3 human visual acceptance came from a different Windows session. baselineSession=$baselineSessionId reviewSession=$($attestation.sessionId)"
}

Write-Host "Verified M3 phase reports and human visual review are healthy and bound to one Windows session: $baselineSessionId"