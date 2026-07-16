#!/usr/bin/env python3
"""
Middle-earth mod .nbt structure analyzer  (READ-ONLY design reference).

Decodes every Minecraft structure template shipped in the Middle-earth mod jar
and emits, per structure:
  - dimensions, total block count, palette size
  - material-category breakdown (wall / floor / roof / door / window / stair /
    slab / light / furniture / plant / air / other) as % of solid blocks
  - detected floor levels (Y layers that read as walkable storeys)
  - per-floor top-down ASCII floorplans (walls #, doors D, windows o, stairs /,
    furniture f, light *, floor . )

Also writes a master index (JSON + Markdown table) across all structures.

Nothing here redistributes mod assets; it summarizes geometry for study.
"""
import zipfile, gzip, struct, io, os, json, re, collections, argparse

# READ-ONLY analyzer. Source mod is ARR + Tolkien IP; output is design reference only
# (do not commit the derived reports or ship them). See tools/middle_earth/README.md.
_ap = argparse.ArgumentParser(description="Decode Middle-earth mod .nbt structures into floorplan/material reports.")
_ap.add_argument("--jar", required=True, help="path to the Middle-earth mod .jar")
_ap.add_argument("--out", default="me_structures", help="output directory for reports")
_args, _ = _ap.parse_known_args()
JAR = _args.jar
OUT = _args.out

# ----------------------------- minimal NBT reader -----------------------------
class NBT:
    def __init__(s, d): s.d = d; s.i = 0
    def u1(s): v = s.d[s.i]; s.i += 1; return v
    def i2(s): v = struct.unpack('>h', s.d[s.i:s.i+2])[0]; s.i += 2; return v
    def i4(s): v = struct.unpack('>i', s.d[s.i:s.i+4])[0]; s.i += 4; return v
    def i8(s): v = struct.unpack('>q', s.d[s.i:s.i+8])[0]; s.i += 8; return v
    def f4(s): v = struct.unpack('>f', s.d[s.i:s.i+4])[0]; s.i += 4; return v
    def f8(s): v = struct.unpack('>d', s.d[s.i:s.i+8])[0]; s.i += 8; return v
    def st(s):
        n = s.i2(); v = s.d[s.i:s.i+n].decode('utf-8', 'ignore'); s.i += n; return v
    def payload(s, t):
        if t == 1: return s.u1()
        if t == 2: return s.i2()
        if t == 3: return s.i4()
        if t == 4: return s.i8()
        if t == 5: return s.f4()
        if t == 6: return s.f8()
        if t == 7:
            n = s.i4(); v = s.d[s.i:s.i+n]; s.i += n; return v
        if t == 8: return s.st()
        if t == 9:
            it = s.u1(); n = s.i4(); return [s.payload(it) for _ in range(n)]
        if t == 10:
            o = {}
            while True:
                tt = s.u1()
                if tt == 0: break
                nm = s.st(); o[nm] = s.payload(tt)
            return o
        if t == 11:
            n = s.i4(); return [s.i4() for _ in range(n)]
        if t == 12:
            n = s.i4(); return [s.i8() for _ in range(n)]
        raise ValueError("bad tag %d" % t)

def parse_structure(raw):
    try:
        data = gzip.decompress(raw)
    except OSError:
        data = raw
    r = NBT(data); r.u1(); r.st()          # root tag id + name
    return r.payload(10)

# ----------------------------- block categorization ---------------------------
# ordered rules: first match wins. keyword tested against the block id.
CAT_RULES = [
    ("air",      ["air", "cave_air", "void_air", "structure_void", "barrier", "light"]),
    ("door",     ["door", "gate"]),
    ("window",   ["window", "glass_pane", "glass", "bars", "lattice"]),
    ("stair",    ["stair"]),
    ("slab",     ["slab", "vertical_slab"]),
    ("roof",     ["roof", "shingle", "thatch"]),  # note: bare "tile" is masonry, not roof
    ("light",    ["torch", "lantern", "campfire", "bonfire", "candle", "glow", "lamp",
                  "fire", "chandelier", "brazier", "sconce"]),
    ("furniture",["bed", "chair", "table", "stool", "bench", "barrel", "chest",
                  "pot", "cauldron", "anvil", "loom", "furnace", "smoker", "shelf",
                  "bookshelf", "crate", "sack", "plate", "cup", "mug", "keg",
                  "sign", "banner", "carpet", "rug", "cushion", "smith", "forge",
                  "workbench", "desk", "cabinet", "wardrobe", "counter"]),
    ("plant",    ["log", "leaves", "sapling", "flower", "grass", "wheat", "crop",
                  "mushroom", "vine", "sunflower", "reed", "hay", "berries", "bush",
                  "root", "turf", "moss", "fern", "plant", "wood"]),
    ("floor",    ["planks", "plank", "floor", "path", "dirt", "gravel", "cobble",
                  "wood_floor", "brick_floor"]),
    ("wall",     ["wall", "brick", "stone", "wattle", "beam", "framed", "frame",
                  "pillar", "column", "fence", "logs", "timber", "plaster",
                  "mud_brick", "sandstone", "concrete", "clay", "granite",
                  "andesite", "diorite", "dolomite", "limestone", "gneiss",
                  "marble", "cobbled", "tiles", "tile", "ashlar", "masonry"]),
]
def categorize(block_id):
    b = block_id.split("[")[0]            # drop blockstate props
    b = b.split(":", 1)[-1]              # drop namespace
    low = b.lower()
    for cat, kws in CAT_RULES:
        for kw in kws:
            if kw in low:
                return cat
    return "other"

CAT_GLYPH = {"wall": "#", "door": "D", "window": "o", "stair": "/", "slab": "=",
             "roof": "^", "light": "*", "furniture": "f", "floor": ".",
             "plant": "T", "other": "+", "air": " "}

# ----------------------------- analysis ---------------------------------------
def analyze(root):
    size = root.get("size", [0, 0, 0])
    W, H, D = size
    palette = root.get("palette") or (root.get("palettes", [[]])[0] if root.get("palettes") else [])
    pal_names = [p.get("Name", "?") for p in palette]
    blocks = root.get("blocks", [])

    # 3D grid of category (only where a block exists); air handled by absence too
    grid = {}                                 # (x,y,z) -> category
    cat_count = collections.Counter()
    mat_count = collections.Counter()
    for b in blocks:
        state = b.get("state", 0)
        pos = b.get("pos", [0, 0, 0])
        name = pal_names[state] if state < len(pal_names) else "?"
        cat = categorize(name)
        mat_count[name.split(":", 1)[-1].split("[")[0]] += 1
        cat_count[cat] += 1
        x, y, z = pos
        grid[(x, y, z)] = cat

    # per-Y solid coverage (non-air) to detect storeys
    per_y = collections.Counter()
    for (x, y, z), c in grid.items():
        if c != "air":
            per_y[y] += 1
    footprint = max((per_y[y] for y in per_y), default=0)

    # storey detection (geometry, material-agnostic): a floor surface = a Y layer
    # with many cells that are solid AND have air/nothing directly above (i.e. a
    # walkable top). Handles grass/turf/plank floors alike. Prominent, vertically
    # separated peaks => storeys.
    surf = collections.Counter()
    solidset = {p for p, c in grid.items() if c != "air"}
    for (x, y, z) in solidset:
        above = grid.get((x, y + 1, z))
        if above is None or above == "air":
            surf[y] += 1
    # threshold on the *strongest* floor layer, not the (yard-inflated) footprint,
    # so upper storeys of a building sitting in a lawn still register.
    peak = max(surf.values(), default=1)
    cand = sorted(y for y in surf if surf[y] >= max(8, peak * 0.28))
    # collapse runs within 2 Y into a single storey (keep the strongest layer)
    storeys = []
    for y in cand:
        if storeys and y - storeys[-1] <= 2:
            if surf[y] > surf[storeys[-1]]:
                storeys[-1] = y
        else:
            storeys.append(y)

    return dict(size=[W, H, D], blocks=len(blocks), palette=len(pal_names),
                categories=dict(cat_count), materials=mat_count,
                grid=grid, per_y=dict(per_y), footprint=footprint,
                storeys=storeys, ymin=min((p[1] for p in [k for k in grid]), default=0),
                ymax=max((k[1] for k in grid), default=0))

def render_layer(grid, y, W, D):
    """Top-down ASCII of layer y (looking down -Y). Prefer wall/opening glyphs;
    take the most 'structural' category in a small vertical window above floor."""
    lines = []
    for z in range(D):
        row = []
        for x in range(W):
            best = None
            # sample this layer and one above (walls usually sit above floor)
            for dy in (1, 0, 2):
                c = grid.get((x, y + dy, z))
                if c and c != "air":
                    # prioritize meaningful glyphs
                    if c in ("wall", "door", "window", "stair", "furniture", "light"):
                        best = c; break
                    if best is None:
                        best = c
            row.append(CAT_GLYPH.get(best, " ") if best else " ")
        lines.append("".join(row))
    return lines

# ----------------------------- main -------------------------------------------
def main():
    z = zipfile.ZipFile(JAR)
    nbts = sorted(n for n in z.namelist()
                  if n.startswith("data/middle-earth/structure/") and n.endswith(".nbt"))
    os.makedirs(OUT, exist_ok=True)
    index = []
    global_mat = collections.Counter()
    for n in nbts:
        rel = n[len("data/middle-earth/structure/"):-4]
        faction = rel.split("/")[0]
        try:
            root = parse_structure(z.read(n))
            a = analyze(root)
        except Exception as e:
            index.append(dict(name=rel, faction=faction, error=str(e)))
            continue
        global_mat.update(a["materials"])
        W, H, D = a["size"]
        solid = sum(v for k, v in a["categories"].items() if k != "air")
        catpct = {k: round(100 * v / solid, 1) for k, v in a["categories"].items()
                  if k != "air" and solid} if solid else {}
        top_cats = sorted(catpct.items(), key=lambda kv: -kv[1])[:5]
        entry = dict(name=rel, faction=faction, size=[W, H, D],
                     blocks=a["blocks"], palette=a["palette"],
                     footprint=a["footprint"], storeys=len(a["storeys"]),
                     storey_levels=a["storeys"],
                     top_categories=top_cats,
                     doors=a["categories"].get("door", 0),
                     windows=a["categories"].get("window", 0),
                     stairs=a["categories"].get("stair", 0),
                     lights=a["categories"].get("light", 0),
                     furniture=a["categories"].get("furniture", 0))
        index.append(entry)

        # write per-structure floorplan report
        rep = [f"# {rel}", "",
               f"faction: {faction}   size (WxHxD): {W}x{H}x{D}   blocks: {a['blocks']}   palette: {a['palette']}",
               f"footprint (max layer voxels): {a['footprint']}   storeys detected: {len(a['storeys'])} @ Y={a['storeys']}",
               "", "## material categories (% of solid)"]
        for k, v in top_cats:
            rep.append(f"  {k:10s} {v:5.1f}%")
        rep.append("")
        rep.append("## top materials")
        for m, c in a["materials"].most_common(12):
            rep.append(f"  {c:5d}  {m}")
        rep.append("")
        rep.append("## floorplans  ( # wall  D door  o window  / stair  f furniture  * light  . floor  T plant  = slab )")
        levels = a["storeys"] or [a["ymin"]]
        for lvl in levels:
            rep.append("")
            rep.append(f"--- storey @ Y={lvl} ---")
            rep += render_layer(a["grid"], lvl, W, D)
        with open(os.path.join(OUT, rel.replace("/", "__") + ".txt"), "w", encoding="utf-8") as f:
            f.write("\n".join(rep))

    # master index json
    with open(os.path.join(OUT, "_index.json"), "w", encoding="utf-8") as f:
        json.dump(index, f, indent=1)

    # markdown summary
    ok = [e for e in index if "error" not in e]
    md = ["# Middle-earth structures — master index", "",
          f"{len(ok)} structures parsed (of {len(index)}).", "",
          "| structure | faction | size WxHxD | blocks | storeys | doors | windows | stairs | top categories |",
          "|---|---|---|---|---|---|---|---|---|"]
    for e in sorted(ok, key=lambda e: (e["faction"], e["name"])):
        W, H, D = e["size"]
        tc = ", ".join(f"{k} {v}%" for k, v in e["top_categories"][:3])
        md.append(f"| {e['name']} | {e['faction']} | {W}x{H}x{D} | {e['blocks']} | "
                  f"{e['storeys']} | {e['doors']} | {e['windows']} | {e['stairs']} | {tc} |")
    md += ["", "## global material frequency (all structures)", "",
           "| count | block |", "|---|---|"]
    for m, c in global_mat.most_common(60):
        md.append(f"| {c} | {m} |")
    with open(os.path.join(OUT, "_SUMMARY.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(md))

    print(f"parsed {len(ok)}/{len(index)} structures")
    print(f"output -> {OUT}")
    print(f"  _SUMMARY.md, _index.json, and {len(ok)} per-structure .txt floorplans")

if __name__ == "__main__":
    main()
