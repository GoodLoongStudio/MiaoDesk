param(
    [Parameter(Mandatory = $true)]
    [string]$Root
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if (-not (Test-Path $Root -PathType Container)) {
    throw "Package root does not exist: $Root"
}

# Current MiaoDesk releases may connect to user-managed local inference servers, but
# they do not distribute model weights. This is a packaging invariant, not a license
# interpretation. If product policy changes, legal/license review must happen before
# changing this gate.
$weightExtensions = @(
    '.safetensors',
    '.gguf',
    '.ggml',
    '.ckpt',
    '.pth',
    '.pt',
    '.onnx',
    '.mlmodel',
    '.mlpackage'
)

$blockedNamePatterns = @(
    'DeepSeek-R1-0528-Qwen3-8B',
    'deepseek-r1-0528-qwen3-8b',
    'glm-4',
    'glm4',
    'z-image-turbo',
    'qwen-image'
)

$hits = @()
foreach ($file in @(Get-ChildItem -LiteralPath $Root -Recurse -File -Force -ErrorAction Stop)) {
    $extension = [IO.Path]::GetExtension($file.Name).ToLowerInvariant()
    $relative = $file.FullName.Substring((Resolve-Path $Root).Path.Length).TrimStart('\','/')
    $nameHit = $false
    foreach ($pattern in $blockedNamePatterns) {
        if ($relative.IndexOf($pattern, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            $nameHit = $true
            break
        }
    }

    if ($extension -in $weightExtensions -or $nameHit) {
        $hits += [pscustomobject]@{
            path = $relative
            bytes = $file.Length
            extension = $extension
            namePatternHit = $nameHit
        }
    }
}

if ($hits.Count -gt 0) {
    Write-Host '::error title=Bundled local AI model detected::MiaoDesk release packages must not contain local model weights until distribution terms are explicitly approved.'
    foreach ($hit in $hits) {
        Write-Host "::error::$($hit.path) ($($hit.bytes) bytes)"
    }
    throw "Found $($hits.Count) forbidden local-model artifact(s) in staged package."
}

Write-Host 'No local AI model weights are bundled in the staged package.' -ForegroundColor Cyan
