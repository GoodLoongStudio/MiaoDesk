# RC known limitations and pending sign-off

This document separates **known product limitations** from **release evidence that still requires physical hardware**. A missing sign-off is not reported as a passed test.

## Known product limitations

### Local AI model weights are not bundled

Release packages intentionally reject unreviewed local model weights during staging. MiaoDesk can use configured OpenAI-compatible local or remote endpoints, including DGX-hosted services, but model files themselves are not shipped until distribution/license evidence is explicitly reviewed.

### Local-model selection is not final

The repository contains a reproducible DGX A/B evaluator and L1 fast→primary routing, but the two real candidate endpoints/models have not yet been run through the final A/B matrix. The RC therefore must not claim a final default local model selection until that result exists.

### D2D textured-sprite tint remains constrained

The current D2D textured-sprite path does not support arbitrary non-white texture tint in the same way as the programmable D3D11 path. Content validation treats this as an explicit backend capability difference rather than silently rendering a mismatched result.

## Pending physical Windows release evidence

The following are RC sign-off items, not automatic CI claims:

- real multi-monitor mixed-DPI visual inspection;
- portrait/landscape clipping check;
- widget alpha, z-order and interaction on Explorer desktop;
- Explorer restart recovery;
- wallpaper disabled while widgets remain visible and interactive;
- final screenshots and logs from the Windows visual-acceptance collector;
- four repeatable reference-machine performance captures: `desktop-only`, `wallpaper`, `widgets-3`, `ai-idle`;
- clean-machine x64 installer smoke test.

## RC rule

Do not promote the version or label a build RC-complete until Issue #60's blocking P0 items are signed off and the exact-SHA release gate confirms that x64 Build, x64 Package, x64 MSIX, Repo Hygiene and ARM64 Package all succeeded for the same commit.

## RC evidence bundle

The final physical sign-off is collected under one evidence root so the release cannot accidentally mix screenshots from one machine with performance captures from another.

Expected layout:

```text
<evidence-root>/
  visual/
    initial/
      acceptance.json
      desktop.png
    explorer-restart/
      acceptance.json
      desktop.png
    sleep-resume/
      acceptance.json
      desktop.png
  performance/
    performance-desktop-only.json
    performance-wallpaper.json
    performance-widgets-3.json
    performance-ai-idle.json
  manual-signoff.json
```

Use the existing collectors with explicit output folders, then copy `packaging/windows/rc-manual-signoff.template.json` to `manual-signoff.json`. Only flip a manual check to `true` after it was actually performed.

The verifier checks that:

- all three visual captures exist and have screenshots;
- the initial capture is multi-monitor, mixed-DPI and includes a portrait monitor;
- no visual collector runtime warnings remain;
- all four performance scenarios exist, contain at least 20 seconds of measurements, and share one hardware/OS fingerprint;
- visual and performance evidence identify the same reference machine;
- no stored performance comparison reports a regression;
- the manual sign-off binds the evidence to a full candidate commit SHA and explicitly confirms every human-only check.

Run:

```powershell
powershell -ExecutionPolicy Bypass -File packaging\windows\verify-rc-evidence.ps1 -EvidenceRoot C:\MiaoDesk-RC-Evidence
```

The verifier does not turn telemetry into a visual judgment. It only prevents missing, mixed-machine, regressed, or unsigned evidence from being treated as complete.

