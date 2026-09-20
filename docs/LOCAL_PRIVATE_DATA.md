# Local private data policy

MiaoDesk treats privacy-sensitive developer data as **local-only**. The Git repository contains source code, public build configuration, sanitized examples and reproducible third-party/runtime inputs; it must not contain real credentials, signing material, personal test data or machine-specific private configuration.

## Where private data belongs

Use one of these local-only locations when working from a source checkout:

```text
.local/          private developer configuration and test inputs
.private/        alternate local-only private workspace
secrets/         local credentials/signing material when a file is unavoidable
local-secrets/   local integration secrets
userdata/        local personal/test user data
user-data/       local personal/test user data
```

All of these paths are ignored by Git.

For API keys used by the MiaoDesk product itself, continue using **Windows Credential Manager**. Do not copy API keys into `.env`, INI/JSON/YAML files, prompts, logs, test fixtures, GitHub Actions workflow files, issues or documentation.

## Local configuration files

Machine-specific overrides should use a `.local.*` or `.private.*` suffix, for example:

```text
config/dev.local.ini
.local/provider.local.json
.private/store-signing.local.json
```

Only sanitized templates such as `.env.example` or `*.example.*` may be committed. Templates must contain placeholders, never working credentials.

## Signing and publishing

Code-signing certificates, private keys, Store publishing credentials and password-bearing files stay local. The repository ignores common formats including `.pfx`, `.p12`, `.pem`, `.key`, `.jks`, `.keystore`, `.pvk`, `.spc`, `.snk`, `.publishsettings` and user-specific publish profiles.

A CI workflow may build unsigned artifacts from public/reproducible inputs. If a release process requires private signing material, keep the signing step on an approved local machine unless a separately reviewed secret-management design is intentionally introduced.

## Before committing

Run:

```powershell
.\scripts\check-private-files.ps1
```

To inspect only staged changes:

```powershell
.\scripts\check-private-files.ps1 -StagedOnly
```

The check rejects tracked local-private paths, sensitive filename patterns and several high-signal credential/private-key markers.

## If a secret was committed before

Adding a path to `.gitignore` does **not** remove existing Git history. Treat a committed credential as exposed: revoke or rotate it first, then remove it from current tracking and, when necessary, rewrite repository history in a separate reviewed operation.
