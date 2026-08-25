# Wallpaper library apply service contract

Status: implemented service slice on `main`.
Date: 2026-08-25

## Purpose

Wallpaper library selection must no longer require UI, Pi, or a future editor to write `wallpaper.ini` directly. The caller-facing path is:

```text
UI / Pi / future Editor
        ↓
DesktopControlService::ApplyLibraryItem
        ↓
WallpaperService::ApplyLibraryItem
        ↓
validated wallpaper selection persistence
        ↓
DesktopControlService::EnsureRuntime
```

## Supported library items

- Scene: `aurora`, `neon`, `grid`.
- Image: validates that the source resolves to an existing regular file, then persists `Scene=image` and clears video state.
- Video: validates that the source resolves to an existing regular file, then persists `Scene=video` and clears image/Web state.
- Web: continues to require the validated `.tdwall` package path through `ApplyWebPackage`; arbitrary Web library state is deliberately not promoted into the service contract.
- Unknown kinds are rejected.

## Boundary rule

`DesktopControlService` coordinates intent and runtime activation only. It must not write INI state or validate wallpaper packages itself. `WallpaperService` owns wallpaper persistence/package transitions.

This slice is a prerequisite for moving the production Library and the Lively-style Library V2 onto the same control plane used by Pi. Monitor assignment remains a later WallpaperService operation.

## Acceptance

This implementation is not considered final product completion until the real Windows flow proves that selecting Scene/Image/Video from the library updates the visible desktop and that Pi/UI read the same resulting `DesktopSnapshot`.
