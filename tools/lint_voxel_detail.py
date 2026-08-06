#!/usr/bin/env python3
"""Lint a .voxel asset for sub-voxel detail.

Enforces the standing rule (CLAUDE.md, "Object Templates"): detail assets —
especially handtools held in the fist (axe, sword, pickaxe, dagger, ...) — MUST
be authored in microcubes, never full cubes. A handtool built from `C` (full
cube) lines renders as an oversized blocky plank in the hand.

Voxel line kinds:
    C x y z Material                       -> full cube   (1 unit)
    S px py pz sx sy sz Material           -> subcube      (1/3 unit)
    M px py pz sx sy sz mx my mz Material  -> microcube    (1/9 unit)
    V x y z Material                       -> fine voxel   (1/N unit, N from
                                              a preceding `# grid: N` header;
                                              finer than microcube — satisfies
                                              the detail rule outright)

Usage:
    python tools/lint_voxel_detail.py <file.voxel> [more.voxel ...]
    # exits 1 if any HANDTOOL asset has no microcube content.

Designed to run as a PostToolUse hook on Write/Edit of **/*.voxel so a
full-cube-only handtool can never be shipped unnoticed.
"""
from __future__ import annotations

import sys
from pathlib import Path

# Directories / name fragments whose assets are held-in-hand detail objects and
# therefore MUST be microcube-authored.
HANDTOOL_DIRS = ("weapons", "tools")
HANDTOOL_HINTS = ("axe", "sword", "pickaxe", "dagger", "knife", "hammer",
                  "mace", "spear", "wand", "staff", "bow", "torch", "shovel", "hoe")


def counts(path: Path) -> tuple[int, int, int, int]:
    c = s = m = v = 0
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        kind = line.split(None, 1)[0]
        if kind == "C":
            c += 1
        elif kind == "S":
            s += 1
        elif kind == "M":
            m += 1
        elif kind == "V":
            v += 1
    return c, s, m, v


def is_handtool(path: Path) -> bool:
    parts = {p.lower() for p in path.parts}
    if parts & set(HANDTOOL_DIRS):
        return True
    stem = path.stem.lower()
    return any(h in stem for h in HANDTOOL_HINTS)


def lint(path: Path) -> list[str]:
    if path.suffix != ".voxel" or not path.exists():
        return []
    c, s, m, v = counts(path)
    problems: list[str] = []
    if is_handtool(path) and m == 0 and v == 0:
        problems.append(
            f"{path}: HANDTOOL with NO microcubes or fine voxels (C={c} S={s} M={m} V={v}). "
            f"Handtools held in the fist MUST be authored in microcubes (see "
            f"resources/templates/weapons/sword_fine.voxel). Full-cube handtools "
            f"render as an oversized blocky plank in the hand."
        )
    elif c > 0 and s == 0 and m == 0 and v == 0 and c <= 12:
        # Small full-cube-only asset: likely a detail object missing sub-voxel work.
        problems.append(
            f"{path}: small full-cube-only asset (C={c}, no subcubes/microcubes). "
            f"Detail assets should use subcubes/microcubes, not full cubes."
        )
    return problems


def main(argv: list[str]) -> int:
    problems: list[str] = []
    for arg in argv:
        problems.extend(lint(Path(arg)))
    if problems:
        print("voxel-detail-lint: sub-voxel-detail rule violated:", file=sys.stderr)
        for p in problems:
            print("  - " + p, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
