"""Contact sheet: legacy (C/S/M micro, 1/9 m) vs grid-8 (1/8 m) furniture, same pixels per metre, isometric
from +X +Y +Z (front = +Z toward the viewer, as the placer convention). Offline eye check of generator output;
the engine loader does not read grid 8 yet.

Usage: python tools/grid_contact_sheet.py <out.png> [pixels_per_metre] [asset_stem ...]
"""
import os, sys, hashlib
from PIL import Image, ImageDraw
os.chdir(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, 'tools')
import voxgrid as vg  # noqa: E402
from test_regen_furniture_grid import _legacy_cells  # noqa: E402

PAL = {"WoodWalnut": (110, 72, 45), "Wood": (160, 115, 70), "Log": (85, 60, 40), "Metal": (120, 125, 135),
       "Glass": (170, 210, 225), "glow": (255, 220, 120), "Bricks": (160, 70, 55), "Stone": (130, 130, 130),
       "StoneBricks": (150, 145, 135), "StoneTiles": (190, 188, 180), "Cobblestone": (115, 115, 110),
       "Linen": (230, 225, 210), "Wool": (160, 40, 40), "Leaf": (70, 130, 60), "Dirt": (100, 75, 50),
       "WoodPlanks": (150, 110, 70), "Sandstone": (210, 190, 140), "Gold": (220, 180, 60)}


def col(m):
    if isinstance(m, tuple):                  # (material, tint): items carry their look in the tint
        m = m[1] or m[0]
    if m.startswith("#"):
        return tuple(int(m[i:i + 2], 16) for i in (1, 3, 5))
    if m in PAL:
        return PAL[m]
    h = hashlib.md5(m.encode()).digest()
    return (h[0], h[1], h[2])


def render(cells, n, ppm=110, size=(300, 300)):
    img = Image.new("RGB", size, (235, 238, 242))
    d = ImageDraw.Draw(img)
    s = ppm / n
    # iso basis (screen px per cell): x -> right-down, z -> left-down, y -> up
    ex = (0.866 * s, 0.5 * s); ez = (-0.866 * s, 0.5 * s); ey = (0, -s)
    xs = [c[0] for c in cells]; ys = [c[1] for c in cells]; zs = [c[2] for c in cells]
    cx0 = (min(xs) + max(xs) + 1) / 2; cz0 = (min(zs) + max(zs) + 1) / 2
    oy = min(ys)
    def P(x, y, z):
        x -= cx0; z -= cz0; y -= oy
        return (size[0] / 2 + x * ex[0] + z * ez[0], size[1] * 0.85 + x * ex[1] + z * ez[1] + y * ey[1])
    for (x, y, z) in sorted(cells, key=lambda c: (c[0] + c[1] + c[2], c[1])):
        c = col(cells[(x, y, z)])
        if (x, y + 1, z) not in cells:
            d.polygon([P(x, y + 1, z), P(x + 1, y + 1, z), P(x + 1, y + 1, z + 1), P(x, y + 1, z + 1)], fill=c, outline=tuple(int(v * .7) for v in c))
        if (x + 1, y, z) not in cells:
            cc = tuple(int(v * .78) for v in c)
            d.polygon([P(x + 1, y, z), P(x + 1, y + 1, z), P(x + 1, y + 1, z + 1), P(x + 1, y, z + 1)], fill=cc, outline=tuple(int(v * .55) for v in c))
        if (x, y, z + 1) not in cells:
            cc = tuple(int(v * .62) for v in c)
            d.polygon([P(x, y, z + 1), P(x + 1, y, z + 1), P(x + 1, y + 1, z + 1), P(x, y + 1, z + 1)], fill=cc, outline=tuple(int(v * .45) for v in c))
    return img


def main(out, names=None, ppm=110):
    rows = []
    for root, _, fs in os.walk('resources/templates_g8'):
        if 'nature' in root:
            continue
        for f in sorted(fs):
            if f.endswith('.voxel') and (not names or f[:-6] in names):
                rows.append(os.path.relpath(os.path.join(root, f), 'resources/templates_g8').replace('\\', '/'))
    rows.sort(key=lambda r: r.split('/')[-1])
    W, H = 300, 300
    per_row = 4  # pairs per row
    nrow = (len(rows) + per_row - 1) // per_row
    sheet = Image.new("RGB", (per_row * 2 * W, nrow * (H + 20)), (255, 255, 255))
    dr = ImageDraw.Draw(sheet)
    for i, rel in enumerate(rows):
        _, g8 = vg.parse_grid(open('resources/templates_g8/' + rel).read().splitlines())
        leg = _legacy_cells('resources/templates/' + rel)
        r, c = divmod(i, per_row)
        sheet.paste(render(leg, 9, ppm), (c * 2 * W, r * (H + 20) + 20))
        sheet.paste(render(g8, 8, ppm), (c * 2 * W + W, r * (H + 20) + 20))
        dr.text((c * 2 * W + 6, r * (H + 20) + 4), f"{rel.split('/')[-1][:-6]}   legacy 1/9 m  |  1/8 m", fill=(0, 0, 0))
    sheet.save(out)
    print(out, sheet.size)


if __name__ == '__main__':
    main(sys.argv[1], set(sys.argv[3:]) or None, int(sys.argv[2]) if len(sys.argv) > 2 else 110)
