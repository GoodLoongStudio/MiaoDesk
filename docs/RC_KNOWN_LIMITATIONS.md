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
