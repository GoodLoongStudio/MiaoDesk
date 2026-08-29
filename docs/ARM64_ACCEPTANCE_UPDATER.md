# MiaoDesk ARM64 acceptance updater

Status: M3/M2 real-Windows acceptance support tooling, not the final consumer update channel.
Date: 2026-08-26

## Purpose

`UPDATE-MIAODESK.cmd` downloads `scripts/update-miaodesk-arm64.ps1`, resolves an exact `main` SHA with a successful Native Windows ARM64 workflow, downloads that validated artifact, materializes the matching RuntimeBundle, self-tests the staged package, then swaps the installation with rollback/recovery protection.

This updater exists so real ARM64 Windows acceptance can install the same exact-head package that CI validated.

## Current authentication boundary

The current acceptance updater still uses GitHub CLI for workflow/artifact access and therefore still requires an authenticated `gh` session. This is temporary developer/acceptance tooling. It is not evidence that M12 consumer packaging is complete.

On a real validation machine, authentication is repaired with:

```powershell
gh auth login -h github.com --web
gh auth status -h github.com
```

The updater must not implement authentication as:

```powershell
gh auth status 2>&1
```

under Windows PowerShell 5.1 with `$ErrorActionPreference = 'Stop'`. `gh auth status` can write normal status text to stderr; merging that native stderr into the PowerShell error stream can throw before `$LASTEXITCODE` is inspected. The production acceptance updater therefore launches the auth probe with `Start-Process`, redirects stdout/stderr to ordinary files, and decides success only from the process exit code.

If authentication is genuinely missing or invalid, the updater must report an actionable `gh auth login -h github.com --web` command and must not modify the existing installation.

## Safety contract

Before install swap:

- resolve exact current `main`;
- require a successful ARM64 validation for that SHA (or start/wait for one);
- download the matching artifact;
- materialize the RuntimeBundle from the same SHA;
- run staged package self-tests.

During swap:

- hold the updater mutex;
- write the recovery journal;
- stop only MiaoDesk processes inside the managed deploy directory;
- preserve the previous package until the new package and index service pass installed self-tests;
- automatically roll back after a failed swap.

## M12 boundary

M12 remains responsible for the actual consumer update path. Its exit gate still requires:

- no Git requirement;
- no `gh` requirement;
- no GitHub login requirement;
- a public release/update channel;
- safe upgrade/rollback.

Do not weaken those M12 requirements merely because the current real-Windows acceptance updater is developer-authenticated.
