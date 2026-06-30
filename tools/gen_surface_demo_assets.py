#!/usr/bin/env python3
"""
gen_surface_demo_assets.py — Demo art + templates for the Phase 3 planar
projected-surface feature (docs/VoxelAppearanceModel.md §7).

Generates procedurally-drawn placeholder surface textures (an oriental rug, a
"Prancing Pony" tavern sign, and three magical tomes) into
resources/textures/source/, plus the matching flat-slab .voxel templates that
carry a `# surface:` header so the engine projects the image across the prop.

Run from the repo root:  python tools/gen_surface_demo_assets.py
Then add the materials (materials.json) + items (items.json) — see the companion
edits — and relaunch the engine (the atlas rebuilds from the new PNGs).
"""
import math, os
from PIL import Image, ImageDraw, ImageFont, ImageFilter

TEX_DIR = os.path.join("resources", "textures", "source")
TMPL_DIR = os.path.join("resources", "templates")
SZ = 512


def font(size, bold=True):
    cands = [r"C:\Windows\Fonts\georgiab.ttf", r"C:\Windows\Fonts\timesbd.ttf",
             r"C:\Windows\Fonts\georgia.ttf", r"C:\Windows\Fonts\arialbd.ttf"]
    for p in cands:
        try:
            return ImageFont.truetype(p, size)
        except Exception:
            pass
    return ImageFont.load_default()


def ctext(d, cx, y, s, fnt, fill, anchor="mm"):
    d.text((cx, y), s, font=fnt, fill=fill, anchor=anchor)


# ----------------------------------------------------------------------------
# Oriental rug — crimson field, layered border, central lozenge medallion,
# corner spandrels, mirrored boteh motifs. Symmetric by construction.
# ----------------------------------------------------------------------------
def gen_rug_oriental():
    img = Image.new("RGB", (SZ, SZ), (140, 28, 28))
    d = ImageDraw.Draw(img)
    CRIM, NAVY, GOLD, CREAM, TEAL = (150, 30, 30), (28, 40, 92), (208, 168, 70), (232, 222, 196), (40, 110, 110)

    def frame(inset, w, col):
        for i in range(w):
            d.rectangle([inset + i, inset + i, SZ - 1 - inset - i, SZ - 1 - inset - i], outline=col)

    frame(6, 10, NAVY)
    frame(18, 4, GOLD)
    # Guard band with running diamonds.
    d.rectangle([34, 34, SZ - 35, SZ - 35], outline=CREAM, width=2)
    step = 32
    for t in range(34, SZ - 34, step):
        for (a, b) in [(t, 34), (t, SZ - 34), (34, t), (SZ - 34, t)]:
            d.polygon([(a, b - 7), (a + 7, b), (a, b + 7), (a - 7, b)], fill=GOLD, outline=NAVY)
    frame(48, 3, NAVY)

    cx = cy = SZ // 2
    # Central lozenge medallion (layered diamonds) + top/bottom pendants.
    for (r, col) in [(150, NAVY), (132, GOLD), (110, CREAM), (78, TEAL), (54, GOLD), (26, CRIM)]:
        d.polygon([(cx, cy - r), (cx + int(r * 0.66), cy), (cx, cy + r), (cx - int(r * 0.66), cy)],
                  fill=col, outline=NAVY)
    d.ellipse([cx - 14, cy - 14, cx + 14, cy + 14], fill=GOLD, outline=NAVY, width=2)
    for sgn in (-1, 1):
        py = cy + sgn * 168
        d.polygon([(cx, py - 22), (cx + 16, py), (cx, py + 22), (cx - 16, py)], fill=GOLD, outline=NAVY)

    # Corner spandrels (quarter fans) + mirrored boteh (paisley) drops.
    def boteh(x, y, flip):
        pts = [(x, y), (x + 18, y - 6), (x + 22, y + 14), (x + 6, y + 30), (x - 10, y + 20), (x - 6, y + 4)]
        if flip:
            pts = [(2 * x - px, py) for (px, py) in pts]
        d.polygon(pts, fill=TEAL, outline=GOLD)
    for (mx, fx) in [(96, False), (SZ - 96, True)]:
        for my in (120, SZ - 150):
            boteh(mx, my, fx)
    img = img.filter(ImageFilter.GaussianBlur(0.4))
    img.save(os.path.join(TEX_DIR, "rug_oriental.png"))


# ----------------------------------------------------------------------------
# Tavern sign — weathered wood planks, carved frame, a prancing-pony silhouette,
# and "THE PRANCING PONY" in a serif face. Square so text/horse don't stretch.
# ----------------------------------------------------------------------------
def horse_silhouette(d, cx, cy, s, col):
    # Rearing horse in profile, facing LEFT (forelegs lifted, hind legs planted).
    # Drawn as one continuous outline (coherent silhouette) plus the lifted
    # forelegs. Coordinates in units of s (~half the figure size).
    def P(pts, fill=col):
        d.polygon([(cx + px * s, cy + py * s) for (px, py) in pts], fill=fill)

    # Main outline, traced clockwise from the muzzle: head -> neck -> back ->
    # rump -> tail -> hind legs -> belly -> chest.
    outline = [
        (-1.02, -0.52),                       # muzzle tip
        (-0.96, -0.66), (-0.86, -0.70),       # forehead
        (-0.88, -0.92), (-0.78, -0.74),       # ear
        (-0.66, -0.78),                       # poll
        (-0.40, -0.50), (-0.20, -0.30),       # crest of the arched neck
        (0.02, -0.30), (0.30, -0.30),         # back / withers to loin
        (0.52, -0.18),                        # croup
        (0.66, -0.34), (0.78, -0.20),         # tail head
        (0.92, 0.10), (0.74, 0.20),           # flowing tail
        (0.84, 0.40), (0.66, 0.46),
        (0.50, 0.30),                         # back of rump
        (0.46, 0.62), (0.30, 0.92),           # hind leg (rear)
        (0.16, 0.92), (0.20, 0.58),
        (0.06, 0.66), (-0.06, 0.92),          # hind leg (near)
        (-0.20, 0.92), (-0.14, 0.50),         # stifle / belly
        (-0.34, 0.30), (-0.48, 0.02),         # belly up to chest
        (-0.70, -0.16),                       # chest
        (-0.86, -0.34),                       # throat
        (-1.02, -0.40),                       # jaw / muzzle bottom
    ]
    P(outline)
    # Lifted, bent forelegs (the "prancing" pose).
    P([(-0.62, -0.10), (-0.50, -0.20), (-0.74, -0.46), (-0.90, -0.40), (-0.80, -0.22)])
    P([(-0.56, 0.02), (-0.44, -0.06), (-0.66, -0.30), (-0.82, -0.24), (-0.72, -0.06)])
    # Mane (a few locks down the neck).
    P([(-0.66, -0.78), (-0.56, -0.62), (-0.42, -0.70), (-0.34, -0.48),
       (-0.22, -0.54), (-0.20, -0.30), (-0.40, -0.34)])


def gen_sign_prancing_pony():
    img = Image.new("RGB", (SZ, SZ), (74, 48, 26))
    d = ImageDraw.Draw(img)
    DARK, MID, LITE, GOLD, CREAM = (52, 32, 16), (96, 62, 32), (120, 82, 44), (206, 170, 80), (236, 224, 198)
    # Vertical planks + grain.
    nb = 6
    bw = SZ / nb
    for i in range(nb):
        x0 = int(i * bw)
        shade = MID if i % 2 == 0 else LITE
        d.rectangle([x0, 0, int(x0 + bw) - 2, SZ], fill=shade)
        for g in range(6):
            gy = 20 + g * 80 + (i * 13 % 30)
            d.arc([x0 - 10, gy, x0 + bw + 10, gy + 36], 200, 340, fill=DARK)
    # Carved frame.
    for i in range(14):
        d.rectangle([i, i, SZ - 1 - i, SZ - 1 - i], outline=DARK if i < 8 else GOLD)
    d.rectangle([26, 26, SZ - 27, SZ - 27], outline=GOLD, width=2)

    horse_silhouette(d, SZ * 0.50, SZ * 0.40, SZ * 0.20, CREAM)
    # Text.
    ctext(d, SZ / 2, SZ * 0.78, "THE PRANCING", font(46), GOLD)
    ctext(d, SZ / 2, SZ * 0.90, "PONY", font(58), CREAM)
    img.save(os.path.join(TEX_DIR, "sign_prancing_pony.png"))


# ----------------------------------------------------------------------------
# Magical tomes — leather cover, metal corner brackets + clasp, a glowing sigil.
# ----------------------------------------------------------------------------
def tome(name, leather, metal, glow, sigil):
    img = Image.new("RGB", (SZ, SZ), leather)
    d = ImageDraw.Draw(img)
    dark = tuple(int(c * 0.55) for c in leather)
    # Cover edge bevel + inset tooling line.
    for i in range(18):
        d.rectangle([i, i, SZ - 1 - i, SZ - 1 - i], outline=dark if i < 10 else metal)
    d.rectangle([40, 40, SZ - 41, SZ - 41], outline=metal, width=3)
    # Corner brackets.
    L = 70
    for (ox, oy, sx, sy) in [(40, 40, 1, 1), (SZ - 40, 40, -1, 1), (40, SZ - 40, 1, -1), (SZ - 40, SZ - 40, -1, -1)]:
        d.line([(ox, oy), (ox + sx * L, oy)], fill=metal, width=8)
        d.line([(ox, oy), (ox, oy + sy * L)], fill=metal, width=8)
    # Central clasp band.
    d.rectangle([SZ // 2 - 16, 30, SZ // 2 + 16, SZ - 30], fill=dark)
    d.rectangle([SZ // 2 - 16, 30, SZ // 2 + 16, SZ - 30], outline=metal, width=2)

    cx = cy = SZ // 2
    # Glow halo behind the sigil.
    halo = Image.new("RGB", (SZ, SZ), (0, 0, 0))
    hd = ImageDraw.Draw(halo)
    hd.ellipse([cx - 150, cy - 150, cx + 150, cy + 150], fill=glow)
    halo = halo.filter(ImageFilter.GaussianBlur(40))
    img = Image.blend(img, Image.composite(halo, img, halo.convert("L")), 0.35)
    d = ImageDraw.Draw(img)
    sigil(d, cx, cy, glow, metal)
    img.save(os.path.join(TEX_DIR, name + ".png"))


def sigil_arcane(d, cx, cy, glow, metal):
    for r in (120, 96, 60):
        d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=glow, width=4)
    for k in range(12):                       # radial runes
        a = k * math.pi / 6
        x, y = cx + 108 * math.cos(a), cy + 108 * math.sin(a)
        d.line([(x, y), (cx + 96 * math.cos(a), cy + 96 * math.sin(a))], fill=glow, width=5)
    pts = [(cx + 56 * math.cos(a), cy + 56 * math.sin(a))
           for a in [(-90 + i * 144) * math.pi / 180 for i in range(5)]]
    d.line(pts + [pts[0]], fill=glow, width=4, joint="curve")   # pentagram


def sigil_necro(d, cx, cy, glow, metal):
    d.ellipse([cx - 118, cy - 118, cx + 118, cy + 118], outline=glow, width=5)
    bone = (228, 230, 214)
    d.ellipse([cx - 60, cy - 70, cx + 60, cy + 40], fill=bone)          # cranium
    d.polygon([(cx - 42, cy + 24), (cx + 42, cy + 24), (cx + 30, cy + 70), (cx - 30, cy + 70)], fill=bone)  # jaw
    for ex in (-30, 30):                                                 # eye sockets
        d.ellipse([cx + ex - 20, cy - 36, cx + ex + 20, cy + 2], fill=(12, 26, 14))
    d.polygon([(cx, cy - 4), (cx - 9, cy + 20), (cx + 9, cy + 20)], fill=(12, 26, 14))   # nasal
    for tx in range(-30, 31, 15):
        d.line([(cx + tx, cy + 50), (cx + tx, cy + 68)], fill=(12, 26, 14), width=4)     # teeth


def sigil_fire(d, cx, cy, glow, metal):
    d.ellipse([cx - 120, cy - 120, cx + 120, cy + 120], outline=glow, width=5)
    for k in range(16):                       # sun rays
        a = k * math.pi / 8
        r0, r1 = 92, 118
        d.line([(cx + r0 * math.cos(a), cy + r0 * math.sin(a)),
                (cx + r1 * math.cos(a), cy + r1 * math.sin(a))], fill=glow, width=6)
    # Stylized flame.
    flame = [(cx, cy - 80), (cx + 34, cy - 20), (cx + 22, cy + 10), (cx + 40, cy + 50),
             (cx, cy + 64), (cx - 40, cy + 50), (cx - 22, cy + 10), (cx - 34, cy - 20)]
    d.polygon(flame, fill=glow)
    d.polygon([(cx, cy - 30), (cx + 16, cy + 18), (cx, cy + 40), (cx - 16, cy + 18)], fill=(255, 246, 200))


# ----------------------------------------------------------------------------
# Templates — flat subcube slabs with a `# surface:` header.
# ----------------------------------------------------------------------------
def _hdr(name, disp, desc, mats, bounds, prim, axis, tex):
    return (f"""# ==========================================================
# ASSET METADATA
# name:         {name}
# display_name: {disp}
# description:  {desc}
# category:     item
# subcategory:  decor
# tags:         projected-surface, decor, demo
# materials:    {mats}
# bounds:       {bounds}
# primitives:   {prim} S
# author:       phyxel
# method:       gen_surface_demo_assets.py
# created:      2026-06-30
# version:      1
# ==========================================================

# surface: texture={tex} projection=planar axis={axis}

""")


def floor_slab(name, disp, desc, tex, nx, nz, mat="Wood"):
    """Thin horizontal slab nx x nz subcubes (sy=0), surface projected on +Y."""
    lines = []
    for gx in range(nx):
        for gz in range(nz):
            lines.append(f"S {gx//3} 0 {gz//3}  {gx%3} 0 {gz%3}  {mat}")
    body = _hdr(name, disp, desc, mat, f"{nx/3:.2f}W x 1/3H x {nz/3:.2f}D", nx*nz, "y", tex)
    open(os.path.join(TMPL_DIR, name + ".voxel"), "w").write(body + "\n".join(lines) + "\n")


def wall_slab(name, disp, desc, tex, nx, ny, mat="Wood"):
    """Thin vertical panel nx x ny subcubes (sz=0), surface projected on +Z."""
    lines = []
    for gx in range(nx):
        for gy in range(ny):
            lines.append(f"S {gx//3} {gy//3} 0  {gx%3} {gy%3} 0  {mat}")
    body = _hdr(name, disp, desc, mat, f"{nx/3:.2f}W x {ny/3:.2f}H x 1/3D", nx*ny, "z", tex)
    open(os.path.join(TMPL_DIR, name + ".voxel"), "w").write(body + "\n".join(lines) + "\n")


def main():
    os.makedirs(TEX_DIR, exist_ok=True)
    os.makedirs(TMPL_DIR, exist_ok=True)
    gen_rug_oriental()
    gen_sign_prancing_pony()
    tome("tome_arcane", (38, 44, 96), (180, 188, 210), (90, 170, 255), sigil_arcane)
    tome("tome_necromantic", (26, 40, 30), (120, 132, 120), (120, 220, 130), sigil_necro)
    tome("tome_fire", (86, 24, 18), (210, 170, 80), (255, 150, 50), sigil_fire)

    floor_slab("rug_oriental", "Oriental Rug",
               "Hand-knotted oriental rug; one woven image projected across the whole rug.",
               "rug_oriental", 6, 6)
    wall_slab("sign_prancing_pony", "Sign: The Prancing Pony",
              "Hanging tavern sign; the painted board is projected across the panel.",
              "sign_prancing_pony", 6, 6)
    for n, disp in [("tome_arcane", "Arcane Tome"),
                    ("tome_necromantic", "Necromantic Tome"),
                    ("tome_fire", "Tome of Embers")]:
        wall_slab(n, disp, "A magical tome; the glowing cover is projected onto the front.",
                  n, 2, 3)
    print("Generated 5 textures + 5 templates.")


if __name__ == "__main__":
    main()
