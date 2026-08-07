#!/usr/bin/env python3
"""Asset library index — regenerate + validate resources/templates/template_catalog.json.

THE single index of every voxel template, consumed by the search_templates MCP
tool, future LLM sessions, and world-building pipelines choosing assets by
category/tags/resolution. Regenerated from the ground truth (the .voxel files
plus sidecars) — hand-edited fields that describe INTENT (display_name,
description, tags, prompt) are preserved across regeneration; everything
measurable (counts, dims, materials, resolution class, category) is recomputed.

Library layout contract (2026-08-07):
  resources/templates/<category>/<stem>.voxel     category in KNOWN_CATEGORIES
  - stems UNIQUE across the whole library (engine scan hard-errors otherwise)
  - the templates ROOT holds only index/manifest json, never .voxel strays
  - .metrics.json sidecars sit beside their template

Usage:
  python tools/asset_index.py            # regenerate the catalog
  python tools/asset_index.py --validate # check invariants, exit 1 on failure
"""
import argparse
import json
import os
import sys
from collections import defaultdict

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
TPL = os.path.join(ROOT, "resources", "templates")
CATALOG = os.path.join(TPL, "template_catalog.json")
ITEMS_JSON = os.path.join(ROOT, "resources", "items.json")
ITEMS_MANIFEST = os.path.join(TPL, "items_manifest.json")

KNOWN_CATEGORIES = {"weapons", "items", "furniture", "nature", "architecture",
                    "decor", "imported", "test", "generated"}

# Intent fields preserved from the existing catalog across regeneration.
PRESERVED = ("display_name", "description", "tags", "prompt", "model",
             "method", "created", "subcategory", "seat_height", "facing",
             "interactions", "version", "building", "cost")


def parse_template(path):
    """Extract measurable facts from a .voxel file."""
    counts = {"C": 0, "S": 0, "M": 0, "V": 0}
    grid = 0
    category_header = ""
    surface = ""
    materials = set()
    lo = [10**9] * 3
    hi = [-10**9] * 3

    def extend(x, y, z):
        for i, v in enumerate((x, y, z)):
            lo[i] = min(lo[i], v)
            hi[i] = max(hi[i], v)

    with open(path, encoding="utf-8", errors="replace") as f:
        for raw in f:
            line = raw.strip()
            if not line:
                continue
            if line.startswith("#"):
                if line.startswith("# grid:"):
                    try:
                        grid = int(line.split(":", 1)[1])
                    except ValueError:
                        pass
                elif line.startswith("# category:"):
                    category_header = line.split(":", 1)[1].strip()
                elif line.startswith("# surface:"):
                    surface = line.split(":", 1)[1].strip()
                continue
            parts = line.split()
            kind = parts[0]
            if kind not in counts:
                continue
            counts[kind] += 1
            try:
                if kind == "C" and len(parts) >= 5:
                    extend(int(parts[1]), int(parts[2]), int(parts[3]))
                    materials.add(parts[4])
                elif kind == "S" and len(parts) >= 8:
                    extend(int(parts[1]), int(parts[2]), int(parts[3]))
                    materials.add(parts[7])
                elif kind == "M" and len(parts) >= 11:
                    extend(int(parts[1]), int(parts[2]), int(parts[3]))
                    materials.add(parts[10])
                elif kind == "V" and len(parts) >= 5:
                    extend(int(parts[1]), int(parts[2]), int(parts[3]))
                    materials.add(parts[4])
            except ValueError:
                continue

    total = sum(counts.values())
    if counts["V"]:
        res_class = f"fine-{grid}" if grid else "fine"
        denom = float(grid or 81)
    elif counts["M"]:
        res_class, denom = "microcube", 1.0
    elif counts["S"]:
        res_class, denom = "subcube", 1.0
    else:
        res_class, denom = "cube", 1.0
    dims = None
    if total:
        # C/S/M positions are cube coords (span +1 cube); V positions are cells.
        dims = [round((hi[i] - lo[i] + 1) / denom, 4) for i in range(3)]

    return {
        "cubes": counts["C"], "subcubes": counts["S"], "microcubes": counts["M"],
        "fine_voxels": counts["V"], "total": total,
        "fine_grid": grid or None,
        "resolution_class": res_class,
        "dims_units": dims,
        "materials": sorted(materials),
        "category_header": category_header,
        "surface": surface or None,
    }


def build_index():
    old = {}
    if os.path.exists(CATALOG):
        with open(CATALOG, encoding="utf-8") as f:
            old = json.load(f)

    item_links = defaultdict(list)   # template relpath -> [item ids]
    if os.path.exists(ITEMS_JSON):
        with open(ITEMS_JSON, encoding="utf-8") as f:
            for it in json.load(f).get("items", []):
                tf = it.get("templateFile")
                if tf:
                    key = tf.replace("\\", "/")
                    if not key.endswith(".voxel"):
                        key += ".voxel"
                    item_links[key].append(it["id"])

    manifest = {}
    if os.path.exists(ITEMS_MANIFEST):
        with open(ITEMS_MANIFEST, encoding="utf-8") as f:
            manifest = json.load(f)

    catalog = {}
    problems = []
    stems = defaultdict(list)

    for dirpath, _dirnames, filenames in os.walk(TPL):
        for fn in sorted(filenames):
            if not fn.endswith(".voxel"):
                continue
            full = os.path.join(dirpath, fn)
            rel = os.path.relpath(full, TPL).replace("\\", "/")
            stem = fn[:-len(".voxel")]
            stems[stem].append(rel)

            parts = rel.split("/")
            if len(parts) == 1:
                problems.append(f"ROOT STRAY: {rel} — every template lives in a category dir")
                category = "UNSORTED"
            else:
                category = parts[0]
                if category not in KNOWN_CATEGORIES:
                    problems.append(f"UNKNOWN CATEGORY DIR: {rel}")

            info = parse_template(full)
            entry = {
                "file": rel,
                "category": category,
                "resolution_class": info["resolution_class"],
                "cubes": info["cubes"], "subcubes": info["subcubes"],
                "microcubes": info["microcubes"], "fine_voxels": info["fine_voxels"],
                "total": info["total"],
                "materials": info["materials"],
            }
            if info["fine_grid"]:
                entry["fine_grid"] = info["fine_grid"]
            if info["dims_units"]:
                entry["dims_units"] = info["dims_units"]
            if info["surface"]:
                entry["surface"] = info["surface"]
            if rel in item_links:
                entry["item_ids"] = sorted(item_links[rel])
            if stem in manifest and manifest[stem].get("grip_point_units"):
                entry["grip_point_units"] = manifest[stem]["grip_point_units"]
            if os.path.exists(full[:-len(".voxel")] + ".metrics.json"):
                entry["metrics_sidecar"] = True

            # Preserve intent fields from the previous catalog entry.
            prev = old.get(stem, {})
            for k in PRESERVED:
                if k in prev and k not in entry:
                    entry[k] = prev[k]
            catalog[stem] = entry

    for stem, paths in stems.items():
        if len(paths) > 1:
            problems.append(f"DUPLICATE STEM '{stem}': {paths}")

    # items.json referential integrity.
    for tf, ids in item_links.items():
        if not os.path.exists(os.path.join(TPL, tf)):
            problems.append(f"DEAD templateFile '{tf}' (items: {ids})")

    return catalog, problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--validate", action="store_true",
                    help="check invariants only; exit 1 on any problem")
    args = ap.parse_args()

    catalog, problems = build_index()

    if problems:
        print(f"{len(problems)} PROBLEMS:")
        for p in problems:
            print(f"  - {p}")
    if args.validate:
        by_cat = defaultdict(int)
        for e in catalog.values():
            by_cat[e["category"]] += 1
        print(f"{len(catalog)} templates: "
              + ", ".join(f"{c}={n}" for c, n in sorted(by_cat.items())))
        return 1 if problems else 0

    with open(CATALOG, "w", newline="\n", encoding="utf-8") as f:
        json.dump(catalog, f, indent=2, sort_keys=True)
    by_cat = defaultdict(int)
    for e in catalog.values():
        by_cat[e["category"]] += 1
    print(f"catalog regenerated: {len(catalog)} templates ("
          + ", ".join(f"{c}={n}" for c, n in sorted(by_cat.items())) + ")")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
