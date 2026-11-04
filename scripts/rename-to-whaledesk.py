#!/usr/bin/env python3
"""One-time MiaoDesk -> WhaleDesK repository-wide migration.

Run at repository root on a clean checkout. Historical executable binaries are
not rewritten: binaries with old branding must be rebuilt, never relabeled.
"""
from __future__ import annotations
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
PAIRS = [
    ("MiaoDesk", "WhaleDesK"),
    ("MIAODESK", "WHALEDESK"),
    ("miaodesk", "whaledesk"),
    ("Miao", "Whale"),
    ("MIAO", "WHALE"),
    ("miao", "whale"),
    ("喵桌面", "鲸鱼桌面"),
    ("喵喵桌面", "鲸鱼桌面"),
]
# Stored runtime data needs explicit backward-compatibility code in a separate
# migration; do not confuse a source-code rename with migrated installations.
NON_REBRANDABLE = {".exe", ".zip", ".mp4", ".lib", ".dll", ".jpg", ".jpeg", ".png", ".ico", ".webp", ".gif", ".pdf"}
SKIP_PREFIXES = ("third_party/", "runtime/x64/node/", "runtime/arm64/node/")
SKIP_PATHS = {"scripts/rename-to-whaledesk.py"}
def replace(value: str) -> str:
    for old, new in PAIRS:
        value = value.replace(old, new)
    return value

def run() -> None:
    listed = subprocess.check_output(["git", "ls-files", "-z"], cwd=ROOT)
    paths = [p.decode("utf-8") for p in listed.split(b"\0") if p]
    edits, moved, binary = 0, 0, []
    for name in paths:
        if name in SKIP_PATHS or name.startswith(SKIP_PREFIXES):
            continue
        source = ROOT / name
        if not source.is_file():
            continue
        new_name = replace(name)
        if source.suffix.lower() in NON_REBRANDABLE:
            if new_name != name:
                # Binary outputs cannot be 'renamed' into a rebranded executable.
                if source.suffix.lower() in {".exe", ".zip", ".mp4", ".lib", ".dll"}:
                    binary.append(name)
                    continue
        else:
            data = source.read_bytes()
            if b"\0" not in data:
                try:
                    original = data.decode("utf-8")
                except UnicodeDecodeError:
                    original = None
                if original is not None:
                    updated = replace(original)
                    if updated != original:
                        source.write_bytes(updated.encode("utf-8"))
                        edits += 1
        if new_name != name:
            destination = ROOT / new_name
            destination.parent.mkdir(parents=True, exist_ok=True)
            if destination.exists():
                raise RuntimeError(f"Rename collision: {name} -> {new_name}")
            source.rename(destination)
            moved += 1
    print(f"WhaleDesK rename: edited {edits} text files, moved {moved} paths")
    if binary:
        print("Historical binary artifacts requiring rebuild (not falsely renamed):")
        for name in binary:
            print("  " + name)
    # The documentation and source should be brand clean; historical binaries
    # and third-party sources are intentionally not mutated.
    violations = []
    for p in ROOT.rglob("*"):
        if not p.is_file() or ".git" in p.parts:
            continue
        rel = p.relative_to(ROOT).as_posix()
        if rel in SKIP_PATHS or rel.startswith(SKIP_PREFIXES):
            continue
        if p.suffix.lower() in NON_REBRANDABLE:
            continue
        try:
            body = p.read_text(encoding="utf-8")
        except (UnicodeError, OSError):
            continue
        if any(key in body for key, _ in PAIRS) or any(key in rel for key, _ in PAIRS):
            violations.append(rel)
    if violations:
        print("Remaining old brand references:", *violations[:100], sep="\n  ")
        sys.exit(1)
if __name__ == "__main__":
    run()
