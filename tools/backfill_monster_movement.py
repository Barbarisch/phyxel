#!/usr/bin/env python3
"""backfill_monster_movement.py — add `movementModes` to every monster already in resources/monsters/.

Roadmap R1 decision 1 (docs/CharacterAnimationRoadmap.md): the SRD ingest kept only the walk
speed, so fly/swim/climb/burrow were lost. This tool looks each monster up by id in the same API
the ingest uses (dnd5eapi.co, 2014 SRD) and writes `movementModes` next to the unchanged `speed`
int. It edits TEXT, not re-serialized JSON: one `"movementModes": {...},` line is inserted
after each record's `"speed": N,` line (or replaces an existing movementModes line), so
hand-aligned files keep their formatting and the diff is pure insertions.

Ids the API does not know (hand-curated monsters with local ids) keep no `movementModes`; the
coverage report marks them "movement unverified" rather than guessing.

    python tools/backfill_monster_movement.py            # fetch (cached) + write
    python tools/backfill_monster_movement.py --dry-run  # report only

API responses are cached under tools/.cache/srd_monsters/ so reruns are offline.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MONSTERS = ROOT / "resources" / "monsters"
CACHE = ROOT / "tools" / ".cache" / "srd_monsters"
API = "https://www.dnd5eapi.co/api/2014/monsters/"
sys.path.insert(0, str(ROOT / "tools"))
from ingest_srd_monsters import movement_modes  # noqa: E402  (one conversion, shared)


def fetch(index: str):
    CACHE.mkdir(parents=True, exist_ok=True)
    cached = CACHE / f"{index}.json"
    if cached.exists():
        data = json.loads(cached.read_text(encoding="utf-8"))
        return data or None
    try:
        req = urllib.request.Request(API + index, headers={"User-Agent": "phyxel-srd-backfill"})
        data = json.load(urllib.request.urlopen(req, timeout=20))
    except urllib.error.HTTPError as e:
        if e.code == 404:
            cached.write_text("{}", encoding="utf-8")   # remember the miss
            return None
        raise
    cached.write_text(json.dumps(data), encoding="utf-8")
    time.sleep(0.05)
    return data


def candidates(mid: str):
    yield mid
    if "_" in mid:
        yield mid.replace("_", "-")


def insert_modes(text: str, modes: list) -> str:
    """Put one movementModes line after each record's speed line, in record order."""
    out, i, skipping = [], 0, False
    for ln in text.split("\n"):
        if skipping:                                   # inside an old multi-line block
            if re.match(r'^\s*},?\s*$', ln):
                skipping = False
            continue
        if re.match(r'^\s*"movementModes":', ln):
            # replaced below, never duplicated; a block opened on this line is skipped to its close
            skipping = ln.rstrip().endswith("{")
            continue
        out.append(ln)
        m = re.match(r'^(\s*)"speed":\s*\d+,\s*$', ln)
        if m:
            if modes[i] is not None:
                out.append(f'{m.group(1)}"movementModes": {json.dumps(modes[i])},')
            i += 1
    if i != len(modes):
        raise ValueError(f"{i} speed lines for {len(modes)} records")
    return "\n".join(out)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args(argv)
    found = missing = changed = 0
    modes_seen = {}
    for path in sorted(MONSTERS.glob("*.json")):
        text = path.read_text(encoding="utf-8")
        mons = json.loads(text)
        if not isinstance(mons, list):
            continue
        modes = []
        for m in mons:
            data = None
            for idx in candidates(m["id"]):
                data = fetch(idx)
                if data:
                    break
            if not data:
                missing += 1; modes.append(m.get("movementModes"))
                print(f"  unverified: {m['id']} ({path.name})")
                continue
            found += 1
            mm = movement_modes(data.get("speed") or {})
            for k in mm:
                modes_seen[k] = modes_seen.get(k, 0) + 1
            if m.get("movementModes") != mm:
                changed += 1
            modes.append(mm)
        new = insert_modes(text, modes)
        assert [x.get("movementModes") for x in json.loads(new)] == modes
        if new != text and not a.dry_run:
            path.write_text(new, encoding="utf-8")
    print(f"monsters with API data: {found}, unverified: {missing}, records changed: {changed}")
    print("movement modes present:", dict(sorted(modes_seen.items())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
