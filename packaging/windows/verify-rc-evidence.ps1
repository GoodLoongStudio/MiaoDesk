param(
    [string]$EvidenceRoot = "",
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Get-PropertyValue($Object, [string]$Name) {
    if ($null -eq $Object) { return $null }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) { return $null }
    return $property.Value
}

function Read-JsonFile([string]$Path, [System.Collections.Generic.List[string]]$Errors) {
    if (-not (Test-Path $Path -PathType Leaf)) {
        $Errors.Add("Missing file: $Path")
        return $null
    }
    try {
        return Get-Content $Path -Raw | ConvertFrom-Json
    }
    catch {
        $Errors.Add("Invalid JSON: $Path -> $($_.Exception.Message)")
        return $null
    }
}

function Add-VisualErrors(
    [string]$Label,
    [string]$ReportPath,
    [bool]$RequireTopology,
    [System.Collections.Generic.List[string]]$Errors
) {
    $report = Read-JsonFile $ReportPath $Errors
    if ($null -eq $report) { return $null }

    if ((Get-PropertyValue $report 'schema') -ne 1) { $Errors.Add("${Label}: acceptance schema must be 1.") }
    $summary = Get-PropertyValue $report 'summary'
    if ($null -eq $summary) { $Errors.Add("${Label}: acceptance summary is missing.") }
    else {
        if ([int](Get-PropertyValue $summary 'warningCount') -ne 0) { $Errors.Add("${Label}: acceptance report contains runtime warnings.") }
        if ([int](Get-PropertyValue $summary 'widgetSurfaceCount') -lt 1) { $Errors.Add("${Label}: no widget surface was captured.") }
        if ($RequireTopology) {
            if ([int](Get-PropertyValue $summary 'monitorCount') -lt 2) { $Errors.Add("${Label}: RC visual sign-off requires at least two monitors.") }
            if ((Get-PropertyValue $summary 'mixedDpi') -ne $true) { $Errors.Add("${Label}: RC visual sign-off requires a mixed-DPI topology.") }
            if ([int](Get-PropertyValue $summary 'portraitMonitorCount') -lt 1) { $Errors.Add("${Label}: RC visual sign-off requires at least one portrait monitor.") }
        }
    }
    $screenshotName = [string](Get-PropertyValue $report 'screenshot')
    if ([string]::IsNullOrWhiteSpace($screenshotName)) { $Errors.Add("${Label}: screenshot field is missing.") }
    else {
        $screenshotPath = Join-Path (Split-Path -Parent $ReportPath) $screenshotName
        if (-not (Test-Path $screenshotPath -PathType Leaf)) { $Errors.Add("${Label}: screenshot file is missing: $screenshotPath") }
        elseif ((Get-Item $screenshotPath).Length -le 0) { $Errors.Add("${Label}: screenshot file is empty: $screenshotPath") }
    }
    $machine = [string](Get-PropertyValue $report 'machine')
    if ([string]::IsNullOrWhiteSpace($machine)) { $Errors.Add("${Label}: machine name is missing.") }
    return $report
}

function Get-MachineFingerprint($Report) {
    $machine = Get-PropertyValue $Report 'machine'
    if ($null -eq $machine) { return '' }
    $cpu = @((Get-PropertyValue $machine 'cpu')) -join '|'
    $gpu = @((Get-PropertyValue $machine 'gpu')) -join '|'
    $parts = @([string](Get-PropertyValue $machine 'computerName'),[string](Get-PropertyValue $machine 'windowsBuild'),[string](Get-PropertyValue $machine 'totalPhysicalMemoryBytes'),$cpu,$gpu,[string](Get-PropertyValue $Report 'logicalProcessors'))
    return ($parts -join '::')
}

function Add-PerformanceErrors([string]$Scenario,[string]$Path,[System.Collections.Generic.List[string]]$Errors) {
    $report = Read-JsonFile $Path $Errors
    if ($null -eq $report) { return $null }
    if ((Get-PropertyValue $report 'schema') -ne 1) { $Errors.Add("${Scenario}: performance schema must be 1.") }
    if ([string](Get-PropertyValue $report 'scenario') -ne $Scenario) { $Errors.Add("${Scenario}: scenario field does not match the file slot.") }
    if ([int](Get-PropertyValue $report 'durationSeconds') -lt 20) { $Errors.Add("${Scenario}: durationSeconds must be at least 20 for RC evidence.") }
    $samples = @(Get-PropertyValue $report 'samples')
    if ($samples.Count -lt 5) { $Errors.Add("${Scenario}: fewer than 5 performance samples were captured.") }
    $machine = Get-PropertyValue $report 'machine'
    if ($null -eq $machine -or [string]::IsNullOrWhiteSpace([string](Get-PropertyValue $machine 'computerName'))) { $Errors.Add("${Scenario}: machine identity is missing.") }
    $metrics = Get-PropertyValue $report 'metrics'
    if ($null -eq $metrics) { $Errors.Add("${Scenario}: metrics are missing.") }
    else {
        foreach ($name in @('averageWorkingSetBytes','averagePrivateMemoryBytes','averageHandles','averageThreads','averageProcessCount')) {
            $value = Get-PropertyValue $metrics $name
            if ($null -eq $value -or [double]$value -le 0) { $Errors.Add("${Scenario}: metric '$name' must be present and greater than zero.") }
        }
        foreach ($name in @('averageCpuPercent','p95CpuPercent','peakCpuPercent')) { if ($null -eq (Get-PropertyValue $metrics $name)) { $Errors.Add("${Scenario}: metric '$name' is missing.") } }
    }
    $comparison = Get-PropertyValue $report 'comparison'
    if ($null -ne $comparison -and (Get-PropertyValue $comparison 'passed') -eq $false) { $Errors.Add("${Scenario}: performance comparison reports a regression.") }
    return $report
}

function Test-ManualSignoff([string]$Path,[System.Collections.Generic.List[string]]$Errors) {
    $signoff = Read-JsonFile $Path $Errors
    if ($null -eq $signoff) { return $null }
    if ((Get-PropertyValue $signoff 'schema') -ne 1) { $Errors.Add('manual-signoff.json: schema must be 1.') }
    $candidateSha = [string](Get-PropertyValue $signoff 'candidateSha')
    if ($candidateSha -notmatch '^[0-9a-fA-F]{40}$') { $Errors.Add('manual-signoff.json: candidateSha must be a full 40-character Git commit SHA.') }
    if ([string]::IsNullOrWhiteSpace([string](Get-PropertyValue $signoff 'reviewer'))) { $Errors.Add('manual-signoff.json: reviewer is required.') }
    $reviewedAt = [string](Get-PropertyValue $signoff 'reviewedAt')
    $parsedDate = [DateTimeOffset]::MinValue
    if ([string]::IsNullOrWhiteSpace($reviewedAt) -or -not [DateTimeOffset]::TryParse($reviewedAt, [ref]$parsedDate)) { $Errors.Add('manual-signoff.json: reviewedAt must be a valid timestamp.') }
    $checks = Get-PropertyValue $signoff 'checks'
    foreach ($name in @('wallpaperComposition','widgetAlphaZOrderInteraction','noClippingLandscapePortrait','mixedDpiDrag','wallpaperOffWidgetsAlive','explorerRestartRecovery','sleepResumeRecovery','cleanInstallerSmoke')) {
        if ($null -eq $checks -or (Get-PropertyValue $checks $name) -ne $true) { $Errors.Add("manual-signoff.json: '$name' must be explicitly true after human verification.") }
    }
    return $signoff
}

function Test-EvidenceRoot([string]$Root) {
    $errors = New-Object 'System.Collections.Generic.List[string]'
    if ([string]::IsNullOrWhiteSpace($Root) -or -not (Test-Path $Root -PathType Container)) { $errors.Add("Evidence root does not exist: $Root"); return $errors }
    $initial = Add-VisualErrors 'visual/initial' (Join-Path $Root 'visual/initial/acceptance.json') $true $errors
    $explorer = Add-VisualErrors 'visual/explorer-restart' (Join-Path $Root 'visual/explorer-restart/acceptance.json') $false $errors
    $sleep = Add-VisualErrors 'visual/sleep-resume' (Join-Path $Root 'visual/sleep-resume/acceptance.json') $false $errors
    $visualMachines = @(@($initial,$explorer,$sleep) | Where-Object { $null -ne $_ } | ForEach-Object { [string](Get-PropertyValue $_ 'machine') } | Select-Object -Unique)
    if ($visualMachines.Count -gt 1) { $errors.Add('Visual acceptance captures came from different machines.') }
    $performanceReports = @{}
    foreach ($scenario in @('desktop-only','wallpaper','widgets-3','ai-idle')) { $performanceReports[$scenario] = Add-PerformanceErrors $scenario (Join-Path $Root "performance/performance-$scenario.json") $errors }
    $fingerprints = @($performanceReports.Values | Where-Object { $null -ne $_ } | ForEach-Object { Get-MachineFingerprint $_ } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique)
    if ($fingerprints.Count -gt 1) { $errors.Add('Performance scenarios do not share one hardware/OS fingerprint.') }
    if ($null -ne $initial) {
        $visualMachine = [string](Get-PropertyValue $initial 'machine')
        foreach ($scenario in $performanceReports.Keys) {
            $report = $performanceReports[$scenario]; if ($null -eq $report) { continue }
            $computerName = [string](Get-PropertyValue (Get-PropertyValue $report 'machine') 'computerName')
            if (-not [string]::Equals($visualMachine,$computerName,[StringComparison]::OrdinalIgnoreCase)) { $errors.Add("${scenario}: performance machine '$computerName' does not match visual machine '$visualMachine'.") }
        }
    }
    [void](Test-ManualSignoff (Join-Path $Root 'manual-signoff.json') $errors)
    return $errors
}

function Write-SyntheticEvidence([string]$Root) {
    foreach ($dir in @('visual/initial','visual/explorer-restart','visual/sleep-resume','performance')) { New-Item -ItemType Directory -Path (Join-Path $Root $dir) -Force | Out-Null }
    foreach ($slot in @('initial','explorer-restart','sleep-resume')) {
        $report=[ordered]@{schema=1;generatedAt=(Get-Date).ToString('o');machine='RC-MACHINE';screenshot='desktop.png';summary=[ordered]@{monitorCount=2;mixedDpi=$true;portraitMonitorCount=1;surfaceCount=3;widgetSurfaceCount=3;wallpaperSurfaceCount=0;processCount=2;warningCount=0};warnings=@()}
        $dir=Join-Path $Root "visual/$slot"; $report|ConvertTo-Json -Depth 5|Set-Content (Join-Path $dir 'acceptance.json') -Encoding UTF8; [IO.File]::WriteAllBytes((Join-Path $dir 'desktop.png'),[byte[]](1,2,3,4))
    }
    foreach ($scenario in @('desktop-only','wallpaper','widgets-3','ai-idle')) {
        $report=[ordered]@{schema=1;scenario=$scenario;generatedAt=(Get-Date).ToString('o');machine=[ordered]@{computerName='RC-MACHINE';windowsBuild='26100';totalPhysicalMemoryBytes=34359738368;cpu=@('Synthetic CPU');gpu=@('Synthetic GPU')};durationSeconds=30;sampleIntervalSeconds=1;logicalProcessors=8;metrics=[ordered]@{averageCpuPercent=0.5;p95CpuPercent=1.0;peakCpuPercent=2.0;averageWorkingSetBytes=104857600;averagePrivateMemoryBytes=94371840;averageHandles=120;averageThreads=12;averageProcessCount=3};comparison=$null;samples=@(1,2,3,4,5,6)}
        $report|ConvertTo-Json -Depth 6|Set-Content (Join-Path $Root "performance/performance-$scenario.json") -Encoding UTF8
    }
    $signoff=[ordered]@{schema=1;candidateSha='0123456789abcdef0123456789abcdef01234567';reviewer='Self Test';reviewedAt=(Get-Date).ToString('o');checks=[ordered]@{wallpaperComposition=$true;widgetAlphaZOrderInteraction=$true;noClippingLandscapePortrait=$true;mixedDpiDrag=$true;wallpaperOffWidgetsAlive=$true;explorerRestartRecovery=$true;sleepResumeRecovery=$true;cleanInstallerSmoke=$true}}
    $signoff|ConvertTo-Json -Depth 5|Set-Content (Join-Path $Root 'manual-signoff.json') -Encoding UTF8
}

if ($SelfTest) {
    $root=Join-Path ([IO.Path]::GetTempPath()) ("MiaoDesk-RcEvidenceSelfTest-"+[Guid]::NewGuid().ToString('N'))
    try {
        Write-SyntheticEvidence $root
        $errors=@(Test-EvidenceRoot $root); if ($errors.Count -ne 0) { throw ("Expected valid synthetic evidence to pass:"+[Environment]::NewLine+($errors -join [Environment]::NewLine)) }
        $badPath=Join-Path $root 'performance/performance-ai-idle.json'; $bad=Get-Content $badPath -Raw|ConvertFrom-Json; $bad.machine.computerName='OTHER-MACHINE'; $bad|ConvertTo-Json -Depth 6|Set-Content $badPath -Encoding UTF8
        $errors=@(Test-EvidenceRoot $root); if (-not ($errors -match 'hardware/OS fingerprint|does not match visual machine')) { throw 'Expected mixed-machine evidence injection to fail.' }
        Write-SyntheticEvidence $root
        $signoffPath=Join-Path $root 'manual-signoff.json'; $signoff=Get-Content $signoffPath -Raw|ConvertFrom-Json; $signoff.checks.cleanInstallerSmoke=$false; $signoff|ConvertTo-Json -Depth 5|Set-Content $signoffPath -Encoding UTF8
        $errors=@(Test-EvidenceRoot $root); if (-not ($errors -match 'cleanInstallerSmoke')) { throw 'Expected missing human sign-off injection to fail.' }
        Write-Host 'RC evidence verifier self-test passed.'
    } finally { Remove-Item $root -Recurse -Force -ErrorAction SilentlyContinue }
    exit 0
}

if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) { throw 'Pass -EvidenceRoot <path> or use -SelfTest.' }
$errors=@(Test-EvidenceRoot (Resolve-Path $EvidenceRoot).Path)
if ($errors.Count -gt 0) { Write-Host 'RC evidence verification FAILED:'; $errors|ForEach-Object { Write-Host " - $_" }; exit 1 }
Write-Host 'RC evidence verification passed.'
