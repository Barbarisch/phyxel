#!/usr/bin/env python3
"""F0 content inventory for docs/VoxelFoundationPlan.md §7a.

For every .voxel template: which grids it uses (C = cube 1 m, S = subcube 1/3 m, M = microcube 1/9 m,
V = fine item 1/81 m), and how it was made (which generator, hand-authored, LLM, imported), read from the
template's own header and the catalog's "method" field. Then: does each grid convert exactly to the 1/8 m grid?

  C (1 m)      -> exact: one block (8x8x8 voxels).
  S (1/3 m)    -> not exact unless the S cells happen to fill whole cubes.
  M (1/9 m)    -> not exact.
  V (1/81 m)   -> not exact (items move to 1/64 m item voxels).

Usage: python tools/voxel_foundation_inventory.py [--json out.json] [--md out.md]
"""
import argparse, collections, json, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
TPL = ROOT / "resources" / "templates"

GEN_PATTERNS = [  # (regex on provenance text, class, generator)
    (r"gen_tree\.py", "generator", "gen_tree.py"),
    (r"tree_forge\.py", "generator", "tree_forge.py"),
    (r"regen_furniture\.py", "generator", "regen_furniture.py"),
    (r"gen_items", "generator", "gen_items.py"),
    (r"structure_pipeline\.furniture", "generator", "structure_pipeline.furniture"),
    (r"structure_pipeline\.doors", "generator", "structure_pipeline.doors"),
    (r"structure_pipeline", "generator", "structure_pipeline"),
    (r"vox_import\.py", "import", "vox_import.py"),
    (r"blocksmith", "llm", "blocksmith"),
    (r"Generated from voxel matrix", "generator", "voxel matrix (unknown tool)"),
    (r"multi-resolution\)", "generator", "building shell export (multi-resolution)"),
    (r"hand-authored|/model", "hand", "hand-authored (/model)"),
    (r"claude-|sonnet|opus|gpt|gemini", "llm", "LLM-authored"),
]


def provenance(header: str, method: str):
    text = (method or "") + "\n" + header
    for rx, cls, gen in GEN_PATTERNS:
        if re.search(rx, text, re.I):
            return cls, gen
    return "unknown", "unknown"


def scan(path: pathlib.Path):
    counts = collections.Counter()
    header_lines = []
    grid81 = False
    s_cells_by_cube = collections.defaultdict(int)
    with path.open(encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("#"):
                if len(header_lines) < 40:
                    header_lines.append(line)
                if re.match(r"#\s*grid:\s*81", line):
                    grid81 = True
                continue
            parts = line.split()
            if not parts:
                continue
            k = parts[0]
            if k in ("C", "S", "M", "V"):
                counts[k] += 1
                if k == "S" and len(parts) >= 7:
                    s_cells_by_cube[tuple(parts[1:4])] += 1
    # S cells that fill a whole cube (27 of 27) convert exactly; count the rest
    s_partial = sum(n for n in s_cells_by_cube.values() if n != 27)
    return counts, "".join(header_lines), grid81, s_partial


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json")
    ap.add_argument("--md")
    a = ap.parse_args()
    catalog = json.loads((TPL / "template_catalog.json").read_text(encoding="utf-8"))
    by_file = {v.get("file"): v for v in catalog.values()}
    rows = []
    for p in sorted(TPL.rglob("*.voxel")):
        rel = p.relative_to(TPL).as_posix()
        counts, header, grid81, s_partial = scan(p)
        cls, gen = provenance(header, (by_file.get(rel) or {}).get("method"))
        exact = (counts["M"] == 0 and counts["V"] == 0 and s_partial == 0)
        rows.append(dict(file=rel, category=rel.split("/")[0], C=counts["C"], S=counts["S"], M=counts["M"],
                         V=counts["V"], grid81=grid81, s_partial=s_partial, provenance=cls, generator=gen,
                         converts_exactly=exact))
    if a.json:
        pathlib.Path(a.json).write_text(json.dumps(rows, indent=1), encoding="utf-8")

    out = []
    n = len(rows)
    ex = sum(r["converts_exactly"] for r in rows)
    out.append(f"Templates: {n}. Convert exactly to the 1/8 m grid: {ex}. Need regeneration or resampling: {n - ex}.\n")
    out.append("| Made by | Class | Templates | Exact | Not exact | Voxel lines C / S / M / V |")
    out.append("|---|---|---|---|---|---|")
    g = collections.defaultdict(list)
    for r in rows:
        g[(r["generator"], r["provenance"])].append(r)
    for (gen, cls), rs in sorted(g.items(), key=lambda x: -len(x[1])):
        e = sum(r["converts_exactly"] for r in rs)
        tot = [sum(r[k] for r in rs) for k in "CSMV"]
        out.append(f"| {gen} | {cls} | {len(rs)} | {e} | {len(rs) - e} | {' / '.join(f'{t:,}' for t in tot)} |")
    out.append("")
    by_cls = collections.Counter()
    for r in rows:
        if not r["converts_exactly"]:
            by_cls[r["provenance"]] += 1
    out.append("Not exact, by class: " + ", ".join(f"{k} {v}" for k, v in by_cls.most_common()))
    unknown = [r["file"] for r in rows if r["provenance"] == "unknown" and not r["converts_exactly"]]
    if unknown:
        out.append(f"\nUnknown provenance, not exact ({len(unknown)}): " + ", ".join(unknown))
    text = "\n".join(out)
    print(text)
    if a.md:
        pathlib.Path(a.md).write_text(text + "\n", encoding="utf-8")


if __name__ == "__main__":
    sys.exit(main())
