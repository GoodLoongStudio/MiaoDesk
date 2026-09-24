param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('desktop-only','wallpaper','widgets-3','ai-idle','custom')]
    [string]$Scenario,
    [int]$DurationSeconds = 20,
    [double]$SampleIntervalSeconds = 1.0,
    [string]$OutputDirectory = "",
    [string]$CompareTo = "",
    [double]$MaxRegressionPercent = 30.0
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($env:OS -ne 'Windows_NT') { throw 'This collector must run on Windows.' }
if ($DurationSeconds -lt 5) { throw 'DurationSeconds must be at least 5.' }
if ($SampleIntervalSeconds -lt 0.25) { throw 'SampleIntervalSeconds must be at least 0.25.' }

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $OutputDirectory = Join-Path ([Environment]::GetFolderPath('Desktop')) "MiaoDesk-Perf-$Scenario-$stamp"
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$logicalProcessors = [Math]::Max(1, [Environment]::ProcessorCount)

function Get-OwnedProcessGraph {
    $rows = @(Get-CimInstance Win32_Process -ErrorAction Stop)
    $byPid = @{}
    $children = @{}
    foreach ($row in $rows) {
        $pidValue = [int]$row.ProcessId
        $parentValue = [int]$row.ParentProcessId
        $byPid[$pidValue] = $row
        if (-not $children.ContainsKey($parentValue)) {
            $children[$parentValue] = New-Object System.Collections.ArrayList
        }
        [void]$children[$parentValue].Add($pidValue)
    }

    $roots = @($rows | Where-Object {
        $_.Name -in @('MiaoDesk.exe','MiaoDeskWallpaper.exe','MiaoDeskHarness.exe')
    } | ForEach-Object { [int]$_.ProcessId })

    $owned = New-Object 'System.Collections.Generic.HashSet[int]'
    $queue = New-Object System.Collections.Queue
    foreach ($root in $roots) {
        if ($owned.Add($root)) { $queue.Enqueue($root) }
    }
    while ($queue.Count -gt 0) {
        $parent = [int]$queue.Dequeue()
        if (-not $children.ContainsKey($parent)) { continue }
        foreach ($child in @($children[$parent])) {
            $childValue = [int]$child
            if ($owned.Add($childValue)) { $queue.Enqueue($childValue) }
        }
    }

    [pscustomobject]@{ Rows=$rows; ByPid=$byPid; RootPids=$roots; OwnedPids=@($owned) }
}

function Get-GpuValues {
    $values = @{}
    try {
        foreach ($engine in @(Get-CimInstance Win32_PerfFormattedData_GPUPerformanceCounters_GPUEngine -ErrorAction Stop)) {
            if ([string]$engine.Name -notmatch 'pid_(\d+)_') { continue }
            $pidValue = [int]$Matches[1]
            $util = [double]$engine.UtilizationPercentage
            if (-not $values.ContainsKey($pidValue)) { $values[$pidValue] = 0.0 }
            $values[$pidValue] += $util
        }
        foreach ($pidValue in @($values.Keys)) {
            $values[$pidValue] = [Math]::Min(100.0, [double]$values[$pidValue])
        }
        [pscustomobject]@{ Available=$true; Error=''; Values=$values }
    }
    catch {
        [pscustomobject]@{ Available=$false; Error=$_.Exception.Message; Values=@{} }
    }
}

function Average([double[]]$Values) {
    if (-not $Values -or $Values.Count -eq 0) { return 0.0 }
    [double](($Values | Measure-Object -Average).Average)
}
function Maximum([double[]]$Values) {
    if (-not $Values -or $Values.Count -eq 0) { return 0.0 }
    [double](($Values | Measure-Object -Maximum).Maximum)
}
function Percentile([double[]]$Values, [double]$P) {
    if (-not $Values -or $Values.Count -eq 0) { return 0.0 }
    $sorted = @($Values | Sort-Object)
    if ($sorted.Count -eq 1) { return [double]$sorted[0] }
    $index = ($sorted.Count - 1) * $P
    $lo = [int][Math]::Floor($index)
    $hi = [int][Math]::Ceiling($index)
    if ($lo -eq $hi) { return [double]$sorted[$lo] }
    $w = $index - $lo
    ([double]$sorted[$lo] * (1.0 - $w)) + ([double]$sorted[$hi] * $w)
}

$firstGraph = Get-OwnedProcessGraph
if ($firstGraph.RootPids.Count -eq 0) {
    throw 'No running MiaoDesk root process was found.'
}

$deadline = (Get-Date).AddSeconds($DurationSeconds)
$lastTime = $null
$lastCpu = @{}
$samples = @()
$gpuAvailableAny = $false
$gpuErrors = New-Object 'System.Collections.Generic.HashSet[string]'

while ((Get-Date) -lt $deadline) {
    $now = Get-Date
    $graph = Get-OwnedProcessGraph
    $gpu = Get-GpuValues
    if ($gpu.Available) { $gpuAvailableAny = $true }
    elseif (-not [string]::IsNullOrWhiteSpace($gpu.Error)) { [void]$gpuErrors.Add($gpu.Error) }

    $wallSeconds = if ($lastTime) { [Math]::Max(0.001, ($now - $lastTime).TotalSeconds) } else { 0.0 }
    $rows = @()

    foreach ($pidValue in $graph.OwnedPids) {
        $process = Get-Process -Id $pidValue -ErrorAction SilentlyContinue
        if (-not $process) { continue }

        $cpuSeconds = try { [double]$process.CPU } catch { 0.0 }
        $cpuPercent = $null
        if ($wallSeconds -gt 0 -and $lastCpu.ContainsKey($pidValue)) {
            $delta = [Math]::Max(0.0, $cpuSeconds - [double]$lastCpu[$pidValue])
            $cpuPercent = [Math]::Min(100.0, ($delta / $wallSeconds / $logicalProcessors) * 100.0)
        }
        $lastCpu[$pidValue] = $cpuSeconds

        $cim = if ($graph.ByPid.ContainsKey($pidValue)) { $graph.ByPid[$pidValue] } else { $null }
        $gpuPercent = if ($gpu.Available -and $gpu.Values.ContainsKey($pidValue)) {
            [double]$gpu.Values[$pidValue]
        } else { $null }

        $rows += [pscustomobject][ordered]@{
            pid=$pidValue
            parentPid=if ($cim) { [int]$cim.ParentProcessId } else { 0 }
            name=$process.ProcessName
            cpuPercent=$cpuPercent
            workingSetBytes=[int64]$process.WorkingSet64
            privateMemoryBytes=[int64]$process.PrivateMemorySize64
            handles=[int]$process.HandleCount
            threads=@($process.Threads).Count
            gpuPercent=$gpuPercent
        }
    }

    $cpuValues = @($rows | Where-Object { $null -ne $_.cpuPercent } | ForEach-Object { [double]$_.cpuPercent })
    $gpuValues = @($rows | Where-Object { $null -ne $_.gpuPercent } | ForEach-Object { [double]$_.gpuPercent })

    $samples += [pscustomobject][ordered]@{
        at=$now.ToString('o')
        processCount=$rows.Count
        totalCpuPercent=if ($cpuValues.Count) { [double](($cpuValues | Measure-Object -Sum).Sum) } else { $null }
        totalWorkingSetBytes=[int64](($rows | Measure-Object -Property workingSetBytes -Sum).Sum)
        totalPrivateMemoryBytes=[int64](($rows | Measure-Object -Property privateMemoryBytes -Sum).Sum)
        totalHandles=[int](($rows | Measure-Object -Property handles -Sum).Sum)
        totalThreads=[int](($rows | Measure-Object -Property threads -Sum).Sum)
        totalGpuPercent=if ($gpuValues.Count) { [Math]::Min(100.0,[double](($gpuValues | Measure-Object -Sum).Sum)) } else { $null }
        processes=$rows
    }

    $lastTime = $now
    Start-Sleep -Milliseconds ([int][Math]::Round($SampleIntervalSeconds * 1000.0))
}

$cpu = @($samples | Where-Object { $null -ne $_.totalCpuPercent } | ForEach-Object { [double]$_.totalCpuPercent })
$working = @($samples | ForEach-Object { [double]$_.totalWorkingSetBytes })
$private = @($samples | ForEach-Object { [double]$_.totalPrivateMemoryBytes })
$handles = @($samples | ForEach-Object { [double]$_.totalHandles })
$threads = @($samples | ForEach-Object { [double]$_.totalThreads })
$gpuSeries = @($samples | Where-Object { $null -ne $_.totalGpuPercent } | ForEach-Object { [double]$_.totalGpuPercent })
$procCounts = @($samples | ForEach-Object { [double]$_.processCount })

$metrics = [ordered]@{
    averageCpuPercent=[Math]::Round((Average $cpu),3)
    p95CpuPercent=[Math]::Round((Percentile $cpu 0.95),3)
    peakCpuPercent=[Math]::Round((Maximum $cpu),3)
    averageWorkingSetBytes=[int64][Math]::Round((Average $working))
    p95WorkingSetBytes=[int64][Math]::Round((Percentile $working 0.95))
    peakWorkingSetBytes=[int64][Math]::Round((Maximum $working))
    averagePrivateMemoryBytes=[int64][Math]::Round((Average $private))
    p95PrivateMemoryBytes=[int64][Math]::Round((Percentile $private 0.95))
    peakPrivateMemoryBytes=[int64][Math]::Round((Maximum $private))
    averageHandles=[int][Math]::Round((Average $handles))
    p95Handles=[int][Math]::Round((Percentile $handles 0.95))
    peakHandles=[int][Math]::Round((Maximum $handles))
    averageThreads=[int][Math]::Round((Average $threads))
    peakThreads=[int][Math]::Round((Maximum $threads))
    averageGpuPercent=if ($gpuSeries.Count) { [Math]::Round((Average $gpuSeries),3) } else { $null }
    p95GpuPercent=if ($gpuSeries.Count) { [Math]::Round((Percentile $gpuSeries 0.95),3) } else { $null }
    peakGpuPercent=if ($gpuSeries.Count) { [Math]::Round((Maximum $gpuSeries),3) } else { $null }
    averageProcessCount=[Math]::Round((Average $procCounts),2)
    peakProcessCount=[int][Math]::Round((Maximum $procCounts))
}

$regressions = @()
$comparison = $null
if (-not [string]::IsNullOrWhiteSpace($CompareTo)) {
    if (-not (Test-Path $CompareTo -PathType Leaf)) { throw "Baseline file does not exist: $CompareTo" }
    $baseline = Get-Content $CompareTo -Raw | ConvertFrom-Json
    if ([string]$baseline.scenario -ne $Scenario) {
        throw "Baseline scenario '$($baseline.scenario)' does not match '$Scenario'."
    }

    $ratio = 1.0 + ($MaxRegressionPercent / 100.0)
    $rules = @(
        [pscustomobject]@{Name='averageCpuPercent';Floor=0.50},
        [pscustomobject]@{Name='p95CpuPercent';Floor=1.00},
        [pscustomobject]@{Name='peakPrivateMemoryBytes';Floor=16777216.0},
        [pscustomobject]@{Name='peakWorkingSetBytes';Floor=16777216.0},
        [pscustomobject]@{Name='peakHandles';Floor=50.0},
        [pscustomobject]@{Name='peakThreads';Floor=8.0},
        [pscustomobject]@{Name='averageGpuPercent';Floor=1.00}
    )
    $details = @()
    foreach ($rule in $rules) {
        $current = $metrics[$rule.Name]
        $prop = $baseline.metrics.PSObject.Properties[$rule.Name]
        $baseValue = if ($prop) { $prop.Value } else { $null }

        if ($null -eq $current -or $null -eq $baseValue) {
            $details += [pscustomobject]@{metric=$rule.Name;baseline=$baseValue;current=$current;allowed=$null;regressed=$false;note='not comparable'}
            continue
        }

        $base = [double]$baseValue
        $nowValue = [double]$current
        $allowed = [Math]::Max($base * $ratio, $base + [double]$rule.Floor)
        $bad = $nowValue -gt $allowed
        if ($bad) {
            $regressions += "$($rule.Name): baseline=$base current=$nowValue allowed=$([Math]::Round($allowed,3))"
        }
        $details += [pscustomobject]@{metric=$rule.Name;baseline=$base;current=$nowValue;allowed=[Math]::Round($allowed,3);regressed=$bad;note=''}
    }
    $comparison = [pscustomobject][ordered]@{
        baselinePath=(Resolve-Path $CompareTo).Path
        maxRegressionPercent=$MaxRegressionPercent
        passed=($regressions.Count -eq 0)
        details=$details
    }
}

$report = [pscustomobject][ordered]@{
    schema=1
    scenario=$Scenario
    generatedAt=(Get-Date).ToString('o')
    durationSeconds=$DurationSeconds
    sampleIntervalSeconds=$SampleIntervalSeconds
    logicalProcessors=$logicalProcessors
    gpuAvailable=$gpuAvailableAny
    gpuErrors=@($gpuErrors)
    metrics=$metrics
    comparison=$comparison
    samples=$samples
}

$reportPath = Join-Path $OutputDirectory "performance-$Scenario.json"
$report | ConvertTo-Json -Depth 9 | Set-Content -Path $reportPath -Encoding UTF8

$mb = 1MB
$summaryPath = Join-Path $OutputDirectory "performance-$Scenario.txt"
$lines = @(
    "MiaoDesk performance baseline: $Scenario"
    "Generated: $($report.generatedAt)"
    "Duration: $DurationSeconds s @ $SampleIntervalSeconds s"
    ('CPU avg / p95 / peak: {0:N2}% / {1:N2}% / {2:N2}%' -f $metrics.averageCpuPercent,$metrics.p95CpuPercent,$metrics.peakCpuPercent)
    ('Working set avg / peak: {0:N1} MB / {1:N1} MB' -f ($metrics.averageWorkingSetBytes/$mb),($metrics.peakWorkingSetBytes/$mb))
    ('Private memory avg / peak: {0:N1} MB / {1:N1} MB' -f ($metrics.averagePrivateMemoryBytes/$mb),($metrics.peakPrivateMemoryBytes/$mb))
    "Handles avg / peak: $($metrics.averageHandles) / $($metrics.peakHandles)"
    "Threads avg / peak: $($metrics.averageThreads) / $($metrics.peakThreads)"
    $(if ($null -ne $metrics.averageGpuPercent) {
        ('GPU avg / p95 / peak: {0:N2}% / {1:N2}% / {2:N2}%' -f $metrics.averageGpuPercent,$metrics.p95GpuPercent,$metrics.peakGpuPercent)
    } else { 'GPU: unavailable on this machine/session' })
    "Process count avg / peak: $($metrics.averageProcessCount) / $($metrics.peakProcessCount)"
)
if ($comparison) {
    $lines += ''
    $lines += "Comparison: $(if ($comparison.passed) {'PASS'} else {'FAIL'})"
    $lines += "Baseline: $($comparison.baselinePath)"
    if ($regressions.Count) {
        $lines += 'Regressions:'
        $lines += @($regressions | ForEach-Object { "  - $_" })
    }
}
$lines | Set-Content -Path $summaryPath -Encoding UTF8

Write-Host "MiaoDesk performance report:" -ForegroundColor Green
Write-Host "  $reportPath"
Write-Host "  $summaryPath"

if ($regressions.Count -gt 0) {
    Write-Error ("Performance regression detected:" + [Environment]::NewLine + ($regressions -join [Environment]::NewLine))
    exit 2
}
