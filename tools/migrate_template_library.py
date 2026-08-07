#!/usr/bin/env python3
"""One-time migration: move root-level templates into the category taxonomy.

resources/templates/{weapons,items,furniture,nature,architecture,decor,
imported,test}/ — stems are preserved (world DBs, flora, FurnitureCatalog all
reference stems; the engine's recursive scan keeps stem lookups working), and
.metrics.json sidecars travel with their template.

Run with --dry-run to print the plan; without it, executes `git mv`.
Idempotent: files already in place are skipped.
"""
import argparse
import os
import subprocess
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
TPL = os.path.join(ROOT, "resources", "templates")

# Explicit exceptions first; prefix rules after.
EXPLICIT = {
    # items (registered in items.json; paths there are updated by this script's sibling edits)
    "tome_arcane": "items", "tome_fire": "items", "tome_necromantic": "items",
    "rug_oriental": "items", "sign_prancing_pony": "items",
    # test rigs
    "bedroll_test": "test", "chest_test": "test", "drawer_test": "test",
    "ladder_test": "test", "lever_test": "test", "rug_test": "test",
    "tavern_test": "test", "test_chair": "test",
    # nature exceptions to the forge_ rule
    "forge_hearth": "furniture",
    # architecture one-offs
    "gate_timber": "architecture", "iron_bars": "architecture",
    "ladder": "architecture", "pillar_stone": "architecture",
    "house_2story": "architecture", "house2": "architecture",
    "houseL": "architecture", "manor_gen": "architecture",
    "shop_detailed": "architecture", "well": "architecture",
    # nature one-offs without a shared prefix
    "fern": "nature", "mushroom": "nature", "hedge_section": "nature",
}

PREFIX_RULES = [
    ("barony_", "imported"),
    ("tree_", "nature"),
    ("bush_", "nature"),
    ("shrub_", "nature"),
    ("forge_", "nature"),        # tree-forge species (forge_hearth excepted above)
    ("door_", "architecture"),
    ("burgomaster", "architecture"),
]

FALLBACK = "furniture"


def category_of(stem: str) -> str:
    if stem in EXPLICIT:
        return EXPLICIT[stem]
    for prefix, cat in PREFIX_RULES:
        if stem.startswith(prefix):
            return cat
    return FALLBACK


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    moves = []
    for fn in sorted(os.listdir(TPL)):
        full = os.path.join(TPL, fn)
        if not os.path.isfile(full):
            continue
        if not (fn.endswith(".voxel") or fn.endswith(".metrics.json")):
            continue
        stem = fn[:-len(".voxel")] if fn.endswith(".voxel") else fn[:-len(".metrics.json")]
        cat = category_of(stem)
        moves.append((fn, cat))

    by_cat = {}
    for fn, cat in moves:
        by_cat.setdefault(cat, []).append(fn)
    for cat in sorted(by_cat):
        print(f"[{cat}] {len(by_cat[cat])} files")
        if args.dry_run:
            for fn in by_cat[cat]:
                print(f"    {fn}")

    if args.dry_run:
        print(f"\n{len(moves)} files total (dry run — nothing moved)")
        return 0

    for cat in by_cat:
        os.makedirs(os.path.join(TPL, cat), exist_ok=True)
    errors = 0
    for fn, cat in moves:
        src = os.path.join("resources", "templates", fn)
        dst = os.path.join("resources", "templates", cat, fn)
        r = subprocess.run(["git", "mv", src, dst], cwd=ROOT,
                           capture_output=True, text=True)
        if r.returncode != 0:
            print(f"FAILED: {src} -> {dst}: {r.stderr.strip()}")
            errors += 1
    print(f"moved {len(moves) - errors} files, {errors} errors")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
