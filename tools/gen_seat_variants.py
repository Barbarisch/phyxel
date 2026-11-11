"""Generate size-variant seat templates for the seat-fit policy.

Seat-fit enforcement (docs/CharacterLibraryPlan.md) refuses any sit where the
character doesn't fit — which means every body size needs LEGAL seating:

  stool_low   — seat top 0.333 (1 subcube): halfling/gnome/goblin/dwarf.
  bench_great — seat top 1.333, 2.0 wide, 1.33 deep: ogre (legs ~1.58) and
                goliath (legs ~1.27); standard humans are refused — feet
                would dangle. Leg lengths measured live 2026-07-22.

Both are simple solid subcube forms with a `# interaction_point:` seat header
so tools/characterize_asset.py extracts real seat features. Regenerate + re-
characterize after edits:
    python tools/gen_seat_variants.py
    python tools/characterize_asset.py resources/templates/furniture/stool_low.voxel \
        resources/templates/furniture/bench_great.voxel

Output goes to resources/templates/furniture/ (the library taxonomy folder since 2026-08-07; until
2026-11-11 this script wrote to the template root, and the writes ran on import).

--grid 8 (docs/VoxelFoundationPlan.md 7a.3 step 4): the same seats re-picked in METRES to the nearest
1/8 m cell -- stool_low 0.625 x 0.375 x 0.625 m, bench_great 2.0 x 1.375 x 1.375 m -- written as
'# grid: 8' B lines to resources/templates_g8/furniture. Checked against the SeatFit band
(engine/include/scene/SeatFit.h) for the leg lengths above (tools/test_gen_seat_variants_grid.py).
"""
import sys
from pathlib import Path

OUT = Path("resources/templates")

# name, display, description, tags, legacy size in subcubes (x, y, z), 1/8 m size in cells (x, y, z)
SEATS = [
    ("stool_low", "Low Stool",
     "A low stool sized for smallfolk — halflings, gnomes, goblins, dwarves.",
     "stool, seat, small, halfling, low", (2, 1, 2), (5, 3, 5)),
    ("bench_great", "Great Bench",
     "A massive high bench for giant-kin — ogres and goliaths.",
     "bench, seat, large, ogre, giant", (6, 4, 4), (16, 11, 11)),
]


def default_out_dir(grid=9):
    root = OUT if grid in (3, 9) else OUT.parent / f"templates_g{grid}"
    return root / "furniture"


def subcube_block(nx, ny, nz):
    """Solid block of nx x ny x nz subcubes as S lines (cube + sub coords)."""
    lines = []
    for x in range(nx):
        for y in range(ny):
            for z in range(nz):
                cx, sx = divmod(x, 3)
                cy, sy = divmod(y, 3)
                cz, sz = divmod(z, 3)
                lines.append(f"S {cx} {cy} {cz} {sx} {sy} {sz} Wood")
    return lines


def write(name, display, desc, tags, nx, ny, nz, seat_yaw=3.141593, out_dir=None, grid=9, per_m=3):
    out = Path(out_dir) if out_dir is not None else default_out_dir(grid)
    out.mkdir(parents=True, exist_ok=True)
    seat_x = nx / per_m / 2
    seat_y = ny / per_m
    seat_z = nz / per_m / 2
    header = [
        "# ==========================================================",
        "# ASSET METADATA",
        f"# name:         {name}",
        f"# display_name: {display}",
        f"# description:  {desc}",
        "# category:     furniture",
        "# subcategory:  seating",
        f"# tags:         {tags}",
        "# ==========================================================",
        f"# interaction_point: seat_0 seat {seat_x:.4f} {seat_y:.4f} {seat_z:.4f} {seat_yaw} *",
    ]
    if grid in (3, 9):
        header += ["# Format: S CubeX CubeY CubeZ SubX SubY SubZ Material", ""]
        body = subcube_block(nx, ny, nz)
    else:
        header = [f"# grid: {grid}"] + header + [
            f"# method:       tools/gen_seat_variants.py --grid {grid} (re-picked in metres)", ""]
        body = [f"B 0 0 0 {nx} {ny} {nz} Wood"]
    (out / f"{name}.voxel").write_text("\n".join(header + body) + "\n", encoding="utf-8")
    print(f"{name}: {nx}x{ny}x{nz} (1/{per_m} m), seat anchor ({seat_x:.2f}, {seat_y:.2f}, {seat_z:.2f}) -> {out}")


def write_all(out_dir=None, grid=9):
    if grid not in (3, 9, 8):
        raise SystemExit(f"gen_seat_variants: no seat sizes picked for grid {grid} (have 9 legacy, 8)")
    for name, display, desc, tags, legacy, g8 in SEATS:
        if grid in (3, 9):
            write(name, display, desc, tags, *legacy, out_dir=out_dir)
        else:
            write(name, display, desc, tags, *g8, out_dir=out_dir, grid=grid, per_m=grid)


if __name__ == "__main__":
    write_all(grid=int(sys.argv[sys.argv.index("--grid") + 1]) if "--grid" in sys.argv else 9)
