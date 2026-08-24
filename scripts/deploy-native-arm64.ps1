param(
    [string]$Repo = "GoodLoongStudio/TuringDesk"
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$RepoRoot = Split-Path $PSScriptRoot -Parent
$Workflow = "native-search-windows.yml"
$Updater = Join-Path $PSScriptRoot "update-turingdesk-arm64.ps1"
$Guard = Join-Path $PSScriptRoot "verify-l3-runtime-contract.ps1"

function Step([string]$Text) {
    Write-Host "`n==> $Text" -ForegroundColor Cyan
}

function Get-MainSha {
    for ($attempt = 1; $attempt -le 5; $attempt++) {
        $output = @(& gh api "repos/$Repo/commits/main" --jq ".sha" 2>$null)
        $sha = [string]($output | Select-Object -First 1)
        if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($sha)) { return $sha.Trim() }
        if ($attempt -lt 5) { Start-Sleep -Seconds ([Math]::Min($attempt * 2, 6)) }
    }
    throw "Unable to resolve main commit SHA."
}

function Get-RunsForCommit([string]$Sha) {
    for ($attempt = 1; $attempt -le 5; $attempt++) {
        $json = @(& gh run list --repo $Repo --workflow $Workflow --commit $Sha --limit 20 --json databaseId,headSha,status,conclusion,event,createdAt 2>$null)
        if ($LASTEXITCODE -eq 0) {
            try {
                if ($json.Count -eq 0) { return @() }
                return @(($json -join "`n") | ConvertFrom-Json)
            } catch { }
        }
        if ($attempt -lt 5) { Start-Sleep -Seconds ([Math]::Min($attempt * 2, 6)) }
    }
    throw "Unable to query ARM64 validation runs."
}

function Wait-ForRun([long]$RunId) {
    Step ("Waiting for ARM64 validation run {0}" -f $RunId)
    & gh run watch $RunId --repo $Repo --exit-status | Out-Host
    if ($LASTEXITCODE -ne 0) {
        & gh run view $RunId --repo $Repo --log-failed | Out-Host
        throw ("ARM64 validation failed: run {0}." -f $RunId)
    }
}

function Ensure-ValidatedCurrentMain([string]$Sha) {
    $runs = @(Get-RunsForCommit $Sha)
    $success = $runs | Where-Object { $_.status -eq "completed" -and $_.conclusion -eq "success" } |
        Sort-Object createdAt -Descending | Select-Object -First 1
    if ($success) {
        Step ("Current main already passed ARM64 validation: run {0}" -f $success.databaseId)
        return
    }

    $running = $runs | Where-Object { $_.status -ne "completed" } |
        Sort-Object createdAt -Descending | Select-Object -First 1
    if ($running) {
        Wait-ForRun ([long]$running.databaseId)
        return
    }

    $before = @($runs | ForEach-Object { [long]$_.databaseId })
    Step "Starting ARM64 validation for current main"
    & gh workflow run $Workflow --repo $Repo --ref main | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Unable to start ARM64 validation workflow." }

    for ($i = 0; $i -lt 45; $i++) {
        Start-Sleep -Seconds 2
        $run = (Get-RunsForCommit $Sha) |
            Where-Object { ([long]$_.databaseId -notin $before) -and $_.event -eq "workflow_dispatch" } |
            Sort-Object createdAt -Descending | Select-Object -First 1
        if ($run) {
            Wait-ForRun ([long]$run.databaseId)
            return
        }
    }
    throw "ARM64 validation was started but its workflow run could not be found."
}

if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { throw "GitHub CLI (gh) was not found in PATH." }
if (-not (Get-Command git -ErrorAction SilentlyContinue)) { throw "Git was not found in PATH." }
if (-not (Test-Path $Updater -PathType Leaf)) { throw "Updater script is missing: $Updater" }
if (-not (Test-Path $Guard -PathType Leaf)) { throw "Runtime contract guard is missing: $Guard" }

Step "Checking GitHub authentication"
& gh auth status | Out-Host
if ($LASTEXITCODE -ne 0) { throw "GitHub CLI is not authenticated. Run: gh auth login" }

Step "Verifying TuringDesk runtime contract"
& powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $Guard | Out-Host
if ($LASTEXITCODE -ne 0) { throw "TuringDesk runtime contract failed." }

Step "Resolving current main"
$mainSha = Get-MainSha
Write-Host ("main: {0}" -f $mainSha) -ForegroundColor DarkGray
Ensure-ValidatedCurrentMain $mainSha

Step "Installing validated TuringDesk package"
& powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $Updater -Repo $Repo | Out-Host
if ($LASTEXITCODE -ne 0) { throw "TuringDesk installation failed." }

Write-Host "`nTuringDesk ARM64 deployment completed successfully." -ForegroundColor Green
Write-Host ("Validated main: {0}" -f $mainSha) -ForegroundColor DarkGray
