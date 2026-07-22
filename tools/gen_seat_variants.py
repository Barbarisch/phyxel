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
    python tools/characterize_asset.py resources/templates/stool_low.voxel \
        resources/templates/bench_great.voxel
"""
from pathlib import Path

OUT = Path("resources/templates")


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


def write(name, display, desc, tags, nx, ny, nz, seat_yaw=3.141593):
    seat_x = nx / 3 / 2
    seat_y = ny / 3
    seat_z = nz / 3 / 2
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
        "# Format: S CubeX CubeY CubeZ SubX SubY SubZ Material",
        "",
    ]
    body = subcube_block(nx, ny, nz)
    (OUT / f"{name}.voxel").write_text("\n".join(header + body) + "\n", encoding="utf-8")
    print(f"{name}: {len(body)} subcubes, seat anchor ({seat_x:.2f}, {seat_y:.2f}, {seat_z:.2f})")


write("stool_low", "Low Stool",
      "A low stool sized for smallfolk — halflings, gnomes, goblins, dwarves.",
      "stool, seat, small, halfling, low", 2, 1, 2)

write("bench_great", "Great Bench",
      "A massive high bench for giant-kin — ogres and goliaths.",
      "bench, seat, large, ogre, giant", 6, 4, 4)
