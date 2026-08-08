#!/usr/bin/env python3
"""Asset-request ledger — the structure pipeline's DEMAND side (StructureForge M3.5).

The generator never invents or substitutes an asset: when a build needs a fixture
type the engine cannot supply, it records a structured request and REFUSES. This
tool is the other half of that loop — it discovers demand BEFORE builds start
refusing, and burns it down.

    python tools/asset_requests.py --scan    # dry-run every purpose the recipes
                                             # can emit; record unmet demand
    python tools/asset_requests.py --list    # show the open burn-down list
    python tools/asset_requests.py --check   # exit 1 if any request is open
                                             # (CI/pre-commit: keeps main out of
                                             #  a mass-refusing state)

Authoring a requested asset follows the standing discipline — archetype sheet in
docs/structure-generation/archetypes/ FIRST, then the generator
(tools/regen_furniture.py or tools/gen_items.py), then the conformance audit —
after which the entry flips to status "conformant" and the build succeeds.

The ledger is committed and merged deterministically (sorted by type, requesters
a sorted set, first_seen preserved), so a re-run never churns the file.
"""

import argparse
import json
import os
import re
import sys
from datetime import date

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LEDGER = os.path.join(ROOT, "resources", "asset_requests.json")
RECIPES = os.path.join(ROOT, "resources", "furnishing_recipes.json")
TEMPLATES = os.path.join(ROOT, "resources", "templates")
CATALOG_CPP = os.path.join(ROOT, "engine", "src", "core", "FurnitureCatalog.cpp")


def load_ledger(path=LEDGER):
    try:
        with open(path, encoding="utf-8") as f:
            doc = json.load(f)
        return doc if isinstance(doc, dict) and "requests" in doc else {"requests": []}
    except (OSError, ValueError):
        return {"requests": []}


def save_ledger(doc, path=LEDGER):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(doc, f, indent=2, sort_keys=False)
        f.write("\n")


def catalog_map():
    """type -> template stem, parsed from FurnitureCatalog.cpp's table (the ONE
    supply-side source of truth; parsed rather than duplicated so this tool can
    never disagree with the engine)."""
    try:
        with open(CATALOG_CPP, encoding="utf-8") as f:
            src = f.read()
    except OSError:
        return {}
    return dict(re.findall(r'\{\s*"([a-z_0-9]+)"\s*,\s*"([A-Za-z_0-9/\-]+)"\s*\}', src))


def template_exists(stem):
    """Recursive search — the 2026-08-07 reorg put templates in category subdirs."""
    if not stem:
        return False
    target = os.path.basename(stem) + ".voxel"
    for dirpath, _dirs, files in os.walk(TEMPLATES):
        if target in files:
            return True
    return False


def recipe_demand():
    """purpose -> [types], from the shipped recipes (tiers included: a tiered
    piece is still demand — a 'high' tavern must be buildable)."""
    try:
        with open(RECIPES, encoding="utf-8") as f:
            doc = json.load(f)
    except (OSError, ValueError) as exc:
        print(f"error: cannot read {RECIPES}: {exc}", file=sys.stderr)
        return {}
    # Schema: {"_comment": ..., "recipes": {purpose: [piece, ...]}, "surface_items": {...}}
    recipes = doc.get("recipes")
    if not isinstance(recipes, dict) or not recipes:
        print(f"error: {RECIPES} has no 'recipes' object — refusing to report a "
              f"false all-clear", file=sys.stderr)
        return {}
    out = {}
    for purpose, spec in recipes.items():
        if not isinstance(spec, (list, dict)):
            continue
        pieces = spec if isinstance(spec, list) else spec.get("pieces", [])
        types = []
        for p in pieces:
            if isinstance(p, dict) and p.get("type"):
                types.append(p["type"])
            elif isinstance(p, str):
                types.append(p)
        if types:
            out[purpose] = types
    return out


def scan():
    cat, demand = catalog_map(), recipe_demand()
    # A parser that reads nothing would report a triumphant "0 unmet demand" —
    # the exact false all-clear this tool exists to prevent. Fail loudly instead.
    if not demand or not cat:
        raise SystemExit("error: parsed 0 recipe purposes or 0 catalog types — "
                         "the scan would be a FALSE all-clear; fix the parser/paths first")
    requests = []
    for purpose, types in sorted(demand.items()):
        for t in sorted(set(types)):
            stem = cat.get(t, "")
            if not stem:
                requests.append((t, purpose, "unmapped",
                                 f"{purpose} requires a '{t}' but no template is mapped for it"))
            elif not template_exists(stem):
                requests.append((t, purpose, "template_missing",
                                 f"{purpose} requires a '{t}' -> template '{stem}' is not on disk"))
    return requests


def merge(doc, requests, today):
    by_type = {e["type"]: e for e in doc.get("requests", []) if e.get("type")}
    for t, purpose, reason, message in requests:
        entry = by_type.get(t)
        requester = {"purpose": purpose, "reason": reason}
        if entry is None:
            by_type[t] = {"type": t, "category": "furniture", "dims": "NEEDS-RESEARCH",
                          "status": "open", "first_seen": today,
                          "requested_by": [requester], "message": message}
        else:
            rs = entry.setdefault("requested_by", [])
            if requester not in rs:
                rs.append(requester)
            rs.sort(key=lambda r: (r.get("purpose", ""), r.get("reason", "")))
    return {"requests": [by_type[k] for k in sorted(by_type)]}


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--scan", action="store_true", help="dry-run demand and record it")
    ap.add_argument("--list", action="store_true", help="show the open burn-down list")
    ap.add_argument("--check", action="store_true", help="exit 1 if any request is open")
    args = ap.parse_args()
    if not (args.scan or args.list or args.check):
        ap.print_help()
        return 0

    if args.scan:
        found = scan()
        merged = merge(load_ledger(), found, date.today().isoformat())
        save_ledger(merged)
        demand = recipe_demand()
        print(f"scan: checked {sum(len(v) for v in demand.values())} type reference(s) across "
              f"{len(demand)} purpose(s) -> {len(found)} unmet demand(s); "
              f"ledger now holds {len(merged['requests'])} entr(ies)")

    doc = load_ledger()
    open_entries = [e for e in doc["requests"] if e.get("status", "open") != "conformant"]

    if args.list:
        if not open_entries:
            print("asset requests: none open — every shipped recipe resolves.")
        else:
            print(f"asset requests: {len(open_entries)} open")
            for e in open_entries:
                who = ", ".join(sorted({r.get("purpose", "?") for r in e.get("requested_by", [])}))
                print(f"  {e['type']:<18} [{e.get('status', 'open')}] wanted by: {who}")
                print(f"    dims: {e.get('dims', 'NEEDS-RESEARCH')}   "
                      f"first seen: {e.get('first_seen', '?')}")

    if args.check and open_entries:
        print(f"FAIL: {len(open_entries)} open asset request(s) — builds needing them REFUSE. "
              f"Author them (archetype sheet -> generator -> conformance) or fix the recipe.",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
