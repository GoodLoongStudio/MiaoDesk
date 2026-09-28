# Multi-API Routing TODO — 2026-09-28

Goal: centralize API profiles in the API Configuration Center while allowing each AI window to choose a profile independently.

## P0 — Runtime policy
- [ ] Add read-only runtime helpers to list profiles and load one by profile id.
- [ ] Change fallback/default policy to the first configured profile in the profile list.
- [ ] Keep legacy `default=1` data readable but stop letting it override the first-profile policy.
- [ ] Make `L3Agent` profile-aware so each AI surface can select a profile without changing the global profile database.
- [ ] Make Pi Runtime read the API key from the agent's selected profile, not from a separate global default lookup.
- [ ] Include profile identity in model/session identity so two profiles sharing the same model cannot leak conversation/session state.

## P0 — UI
- [ ] API Configuration Center remains the single place to add/edit/delete multiple profiles.
- [ ] Remove the separate “设为默认” action; the first configured profile is the default fallback.
- [ ] Mark the first configured profile as “默认” in the profile list.
- [ ] Add an API profile selector to the general AI conversation window.
- [ ] Add an API profile selector to AI Wallpaper Creator.
- [ ] Add an API profile selector to AI Widget Creator.
- [ ] New AI windows select the first configured API profile by default.
- [ ] Switching a profile is blocked while a request is running and resets the model session before the next request.

## P1 — Validation
- [ ] Add source-contract tests for first-profile default routing and per-window selectors.
- [ ] Run Repo Hygiene.
- [ ] Run Windows x64 Build.
- [ ] Run Windows ARM64 Package.

## Exit criteria
- Multiple API profiles can coexist in the Configuration Center.
- No feature-specific API key/base URL copy is required.
- Each AI window exposes only profile names and can choose a different one from other windows.
- Opening a new AI window uses the first configured profile unless the user chooses another profile in that window.
- Changing one AI window's selection does not rewrite the central profile list or another window's selection.
