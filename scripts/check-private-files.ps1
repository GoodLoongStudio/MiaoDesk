[CmdletBinding()]
param(
    [switch]$StagedOnly
)

$ErrorActionPreference = 'Stop'

function Get-RepositoryRoot {
    $root = (& git rev-parse --show-toplevel 2>$null)
    if ($LASTEXITCODE -ne 0 -or -not $root) {
        throw 'This script must be run inside the MiaoDesk Git repository.'
    }
    return $root.Trim()
}

function Get-CandidatePaths {
    if ($StagedOnly) {
        $items = & git diff --cached --name-only --diff-filter=ACMR
    } else {
        $items = & git ls-files
    }
    if ($LASTEXITCODE -ne 0) {
        throw 'Unable to enumerate Git paths.'
    }
    return @($items | Where-Object { $_ -and $_.Trim() } | ForEach-Object { $_.Replace('\\', '/') })
}

$repoRoot = Get-RepositoryRoot
Push-Location $repoRoot
try {
    $paths = Get-CandidatePaths
    $violations = [System.Collections.Generic.List[string]]::new()

    $blockedPathPatterns = @(
        '^(?:\.local|\.private|secrets|local-secrets|userdata|user-data)/',
        '(?i)(?:^|/)\.env(?:\..+)?$',
        '(?i)\.(?:pem|key|pfx|p12|jks|keystore|pvk|spc|snk|publishsettings)$',
        '(?i)\.pubxml\.user$',
        '(?i)(?:^|/)(?:credentials(?:-[^/]+)?|secrets(?:-[^/]+)?|client_secret[^/]*|service-account[^/]*|oauth[^/]*)\.json$',
        '(?i)\.(?:local|private)\.(?:ini|json|ya?ml|toml)$'
    )

    foreach ($path in $paths) {
        if ($path -eq '.env.example') { continue }
        foreach ($pattern in $blockedPathPatterns) {
            if ($path -match $pattern) {
                $violations.Add("blocked tracked path: $path")
                break
            }
        }
    }

    $privateKeyPattern = '-----BEGIN ' + '(?:RSA |EC |OPENSSH )?' + 'PRIVATE KEY-----'
    $awsKeyPattern = '\b(?:AKIA|ASIA)[0-9A-Z]{16}\b'
    $githubClassicPattern = '\bgh' + '[pousr]_[A-Za-z0-9]{20,}\b'
    $githubFineGrainedPattern = '\bgithub_' + 'pat_[A-Za-z0-9_]{20,}\b'
    $genericSkPattern = '\bs' + 'k-[A-Za-z0-9_-]{20,}\b'
    $contentPatterns = @(
        @{ Name = 'private key material'; Regex = $privateKeyPattern },
        @{ Name = 'AWS access key'; Regex = $awsKeyPattern },
        @{ Name = 'GitHub token'; Regex = $githubClassicPattern },
        @{ Name = 'GitHub fine-grained token'; Regex = $githubFineGrainedPattern },
        @{ Name = 'API token-like secret'; Regex = $genericSkPattern }
    )

    $selfPath = 'scripts/check-private-files.ps1'
    foreach ($path in $paths) {
        if ($path -eq $selfPath) { continue }
        $fullPath = Join-Path $repoRoot $path
        if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) { continue }

        $item = Get-Item -LiteralPath $fullPath
        if ($item.Length -gt 1MB) { continue }

        try {
            $bytes = [System.IO.File]::ReadAllBytes($fullPath)
            if ($bytes.IndexOf([byte]0) -ge 0) { continue }
            $text = [System.Text.Encoding]::UTF8.GetString($bytes)
        } catch {
            continue
        }

        foreach ($entry in $contentPatterns) {
            if ($text -match $entry.Regex) {
                $violations.Add("$($entry.Name) detected in tracked file: $path")
            }
        }
    }

    if ($violations.Count -gt 0) {
        Write-Host 'Private-data check FAILED:' -ForegroundColor Red
        $violations | Sort-Object -Unique | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
        Write-Host ''
        Write-Host 'Move private data under .local/ (or another ignored local-only path), remove it from Git tracking, and rotate any real credential that was committed.' -ForegroundColor Yellow
        exit 1
    }

    $scope = if ($StagedOnly) { 'staged changes' } else { 'tracked repository files' }
    Write-Host "Private-data check passed for $scope." -ForegroundColor Green
} finally {
    Pop-Location
}
