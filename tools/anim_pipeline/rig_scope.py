#!/usr/bin/env python3
"""Rig scope: which .anim rigs are gameplay, tools-only, or unreferenced (A2, docs/AnimationSystemV3Plan.md).

Scans every place a rig can be referenced by stem or file name and writes
resources/animated_characters/rig_scope.json:

  gameplay   referenced from resources/ (bindings, biomes, races, ...), engine/, editor/, samples/,
             scripts/ or a PhyxelProjects game.json  -> the A2 plan gate applies
  tools_only referenced only from generator manifests under tools/                -> WARN tier
  tests_only referenced only from tests/                                           -> WARN tier
  unreferenced_top_level  referenced by nothing but still shipped at top level -> MUST be empty
  retired    whatever lives under animated_characters/legacy/ (the retirement destination)

`tests/test_rig_scope.py` asserts the committed JSON equals a fresh scan and that every
`retired` rig is under legacy/, so the file can never go stale silently.

Usage: python tools/anim_pipeline/rig_scope.py [--check]
"""
from __future__ import annotations

import argparse
import glob
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
RIG_DIR = REPO / "resources" / "animated_characters"
OUT = RIG_DIR / "rig_scope.json"
PROJECTS = Path.home() / "Documents" / "PhyxelProjects"

GAMEPLAY_GLOBS = ["resources/**/*.json", "engine/src/**/*.cpp", "engine/include/**/*.h",
                  "editor/src/**/*.cpp", "samples/**/*.json", "scripts/**/*.py"]
TOOLS_GLOBS = ["tools/**/*.py", "tools/**/*.json"]
TESTS_GLOBS = ["tests/**/*.cpp", "tests/**/*.py"]


def _rigs() -> list[str]:
    # Top-level rigs only. legacy/ is the retirement destination, not a scope candidate
    # (its generic stems — "character" — would match every source file).
    return sorted(p.stem for p in RIG_DIR.glob("*.anim"))


def _read(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return ""


def scan_stems(rigs: list[str]) -> dict[str, set[str]]:
    """{stem: {tiers that reference it}} for arbitrary stems (used for the retired check)."""
    patterns = {r: re.compile(r"(?<![A-Za-z0-9_])" + re.escape(r) + r"(\.anim)?(?![A-Za-z0-9_])") for r in rigs}
    hits: dict[str, set[str]] = {r: set() for r in rigs}

    def scan_files(files, tier):
        for f in files:
            p = Path(f)
            if RIG_DIR in p.parents or p == OUT:
                continue
            text = _read(p)
            for r, pat in patterns.items():
                if pat.search(text):
                    hits[r].add(tier)

    for g in GAMEPLAY_GLOBS:
        scan_files(glob.glob(str(REPO / g), recursive=True), "gameplay")
    if PROJECTS.exists():
        scan_files(glob.glob(str(PROJECTS / "*" / "game.json")), "gameplay")
    for g in TOOLS_GLOBS:
        scan_files(glob.glob(str(REPO / g), recursive=True), "tools")
    for g in TESTS_GLOBS:
        scan_files(glob.glob(str(REPO / g), recursive=True), "tests")
    return hits


def scan() -> dict:
    rigs = _rigs()
    hits = scan_stems(rigs)

    out = {"gameplay": [], "tools_only": [], "tests_only": [], "unreferenced_top_level": [],
           "retired": sorted(p.stem for p in (RIG_DIR / "legacy").glob("*.anim"))}
    for r in rigs:
        t = hits[r]
        if "gameplay" in t:
            out["gameplay"].append(r)
        elif "tools" in t:
            out["tools_only"].append(r)
        elif "tests" in t:
            out["tests_only"].append(r)
        else:
            out["unreferenced_top_level"].append(r)   # must be EMPTY: retire (move to legacy/)
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--check", action="store_true", help="exit 1 if the committed JSON differs from a fresh scan")
    a = ap.parse_args()
    fresh = scan()
    if a.check:
        old = json.loads(OUT.read_text(encoding="utf-8")) if OUT.exists() else None
        if old != fresh:
            print("rig_scope.json is stale; run tools/anim_pipeline/rig_scope.py")
            return 1
        print("rig_scope.json up to date")
        return 0
    OUT.write_text(json.dumps(fresh, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {OUT}: {len(fresh['gameplay'])} gameplay, {len(fresh['tools_only'])} tools-only, "
          f"{len(fresh['tests_only'])} tests-only, {len(fresh['retired'])} retired")
    return 0


if __name__ == "__main__":
    sys.exit(main())
