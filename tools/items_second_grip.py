"""Derive `held.secondGrip` for two-handed items from the template manifest (A3 item 3).

The second grip is NOT hand-typed: it is the manifest grip point moved along the item's shaft
(+Y in the normalized template frame, which gen_items.py guarantees: +Y up, origin at the
grip/base) by one hand width plus a gap, clamped inside the template. Applies to every gameplay
item whose template is tagged two-handed, or is a staff / polearm, in gen_items' catalogue.

    python tools/items_second_grip.py            # report
    python tools/items_second_grip.py --write    # write resources/items.json

Units: world units in the template frame (same as grip_point_units).
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ITEMS = ROOT / "resources" / "items.json"
MANIFEST = ROOT / "resources" / "templates" / "items_manifest.json"
sys.path.insert(0, str(ROOT / "tools"))

HAND_SPAN_UNITS = 0.16      # one hand width, hands touching (was 0.28: measured live 2026-09-30 the maul's second grip sat 0.12-0.15 u beyond the off-hand's reach in the 2H carry pose)
END_MARGIN_UNITS = 0.05     # never place the second hand past the top of the template


def two_handed_stems() -> set[str]:
    from gen_items import CATALOG_META  # tags are the single source of "two-handed"
    out = set()
    for stem, (_name, _desc, sub, tags) in CATALOG_META.items():
        if "two-handed" in tags or sub in ("staff", "spear") or "polearm" in tags:
            out.add(stem)
    return out


def derive(manifest_entry: dict) -> list[float] | None:
    grip = manifest_entry.get("grip_point_units")
    dims = manifest_entry.get("dims_units")
    if not grip or not dims:
        return None
    y = min(grip[1] + HAND_SPAN_UNITS, dims[1] - END_MARGIN_UNITS)
    if y < grip[1] + 0.05:
        return None                      # shaft too short for a second hand
    return [round(grip[0], 4), round(y, 4), round(grip[2], 4)]


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--write", action="store_true")
    args = ap.parse_args(argv)

    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    data = json.loads(ITEMS.read_text(encoding="utf-8"))
    items = data["items"] if isinstance(data, dict) and "items" in data else data
    stems = two_handed_stems()
    changed = 0
    for item in items:
        tf = item.get("templateFile", "")
        stem = Path(tf).stem if tf else ""
        held = item.get("held")
        if not held or stem not in stems:
            if held and "secondGrip" in held and stem not in stems:
                print(f"[PRUNE] {item['id']}: not two-handed, secondGrip removed")
                held.pop("secondGrip")
                changed += 1
            continue
        entry = manifest.get(stem)
        if not entry:
            print(f"[SKIP] {item['id']}: no manifest entry for '{stem}'")
            continue
        sg = derive(entry)
        if sg is None:
            print(f"[SKIP] {item['id']}: shaft too short for a second hand")
            continue
        if held.get("secondGrip") != sg:
            held["secondGrip"] = sg
            changed += 1
        print(f"[OK]   {item['id']}: secondGrip {sg} (grip {entry['grip_point_units']}, height {entry['dims_units'][1]})")
    if args.write and changed:
        ITEMS.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        print(f"wrote {changed} items to {ITEMS}")
    elif args.write:
        print("nothing to write")
    return 0


if __name__ == "__main__":
    sys.exit(main())
