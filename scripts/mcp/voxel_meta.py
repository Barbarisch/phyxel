"""Dependency-free reader of a Phyxel `.voxel` template's metadata (M0 row 11, 2026-10-14).

Replaces the `blocksmith.generators.phyxel_parser.read_voxel_metadata` import that the MCP server
tried first: that module never existed in the submodule, so the manual fallback below was the real
path all along. Returns the dict `inspect_template` reads:

    {"name": str|None, "bounds": {"w","h","d"}|{}, "facing_yaw": float|None,
     "fine_grid": int|None, "primitive_counts": {"cubes","subcubes","microcubes","fine_voxels"},
     "materials": {name: count}}

Formats (docs/AssetLibrary.md, docs/FineVoxelItems.md):
  `# key: value` header lines; `C x y z Mat [tint=..]` cubes, `S ...` subcubes, `M ...` microcubes
  (static tiers); `# grid: N` + `V x y z Mat [tint=..]` lines (the fine-voxel item tier).
"""
from __future__ import annotations
import re
import sys

_BOUNDS = re.compile(r"(\d+)\s*W\s*x\s*(\d+)\s*H\s*x\s*(\d+)\s*D", re.I)


def read_voxel_metadata(path: str) -> dict:
    meta: dict = {"name": None, "bounds": {}, "facing_yaw": None, "fine_grid": None,
                  "primitive_counts": {"cubes": 0, "subcubes": 0, "microcubes": 0, "fine_voxels": 0},
                  "materials": {}}
    counts = meta["primitive_counts"]
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            s = line.strip()
            if not s:
                continue
            if s.startswith("#"):
                body = s[1:].strip()
                if ":" in body:
                    key, _, val = body.partition(":")
                    key, val = key.strip().lower(), val.strip()
                    if key == "name" and val:
                        meta["name"] = val
                    elif key == "grid":
                        try: meta["fine_grid"] = int(val)
                        except ValueError: pass
                    elif key == "facing_yaw":
                        try: meta["facing_yaw"] = float(val)
                        except ValueError: pass
                    elif key == "bounds":
                        m = _BOUNDS.search(val)
                        if m:
                            meta["bounds"] = {"w": int(m.group(1)), "h": int(m.group(2)), "d": int(m.group(3))}
                continue
            tag = s.split(None, 1)[0]
            kind = {"C": "cubes", "S": "subcubes", "M": "microcubes", "V": "fine_voxels"}.get(tag)
            if kind is None:
                continue
            counts[kind] += 1
            parts = s.split()
            # material = the first token after the coordinates (C/V: 3 coords; S: 6; M: 9)
            ncoord = {"C": 3, "V": 3, "S": 6, "M": 9}[tag]
            if len(parts) > ncoord + 1:
                mat = parts[ncoord + 1]
                meta["materials"][mat] = meta["materials"].get(mat, 0) + 1
    return meta


if __name__ == "__main__":   # self-check: python -I scripts/mcp/voxel_meta.py <file.voxel> [...]
    import json
    for p in sys.argv[1:]:
        print(p, json.dumps(read_voxel_metadata(p)))
