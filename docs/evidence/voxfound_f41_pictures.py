"""F4.1 pictures: exploded views of the fracture-lab rigs, drawn from the CPU reference's own output.

Provenance: the input is the per-voxel dump written by VoxFoundFractureLab.SummaryAndPictureDump
(VOXFOUND_FRACTURE_DUMP=<dir>): every solid voxel with its material, piece id and kind (0 anchored, 1 body,
2 crumb). This script only draws it (PIL, isometric, painter's order) -- no engine, no physics. Loose pieces are
pushed out from the hit along (centroid - hit) and the hit direction so the cracks are visible; anchored matter
stays in place. Colours: anchored = material colour; bodies = one colour per piece; crumbs = white.

usage: python voxfound_f41_pictures.py <dump_dir> <out_dir>
"""
import math
import os
import sys
from collections import defaultdict

from PIL import Image, ImageDraw

MAT_RGB = {1: (150, 150, 150), 2: (170, 80, 60), 3: (150, 110, 60), 4: (170, 210, 230), 5: (150, 110, 60)}


def load(path):
    vox = []
    with open(path) as f:
        dim = [int(v) for v in f.readline().split()[1:]]
        hit = [float(v) for v in f.readline().split()[1:]]
        for line in f:
            x, y, z, m, p, k, c = (int(v) for v in line.split())
            vox.append((x, y, z, m, p, k))
            CUTS[(os.path.basename(path), x, y, z)] = c
    return dim, hit[:3], hit[3:], vox


CUTS = {}  # (dump name, x, y, z) -> cut bits (bit a: the bond to the +a neighbour is cut)


def piece_rgb(pid):
    h = (pid * 2654435761) & 0xFFFFFFFF
    hue = (h % 360) / 360.0
    # simple HSV -> RGB, saturation 0.55, value 0.85
    i = int(hue * 6) % 6
    f = hue * 6 - int(hue * 6)
    v, s = 0.85, 0.55
    p, q, t = v * (1 - s), v * (1 - s * f), v * (1 - s * (1 - f))
    r, g, b = [(v, t, p), (q, v, p), (p, v, t), (p, q, v), (t, p, v), (v, p, q)][i]
    return int(r * 255), int(g * 255), int(b * 255)


def render(dump, explode=1.0, size=(900, 650), title=""):
    dim, hit, hdir, vox = load(dump)
    # view from the side the hit comes from (against the hit direction), from above
    view = [1.0, 1.0, 1.0]
    for a in range(3):
        if hdir[a] > 0.5:
            view[a] = -1.0
        elif hdir[a] < -0.5:
            view[a] = 1.0
    if abs(hdir[1]) > 0.5:  # a hit from above: look from the front-top
        view = [1.0, 1.0, -1.0]
    occ = {}
    cent = defaultdict(lambda: [0.0, 0.0, 0.0, 0])
    kind = {}
    for x, y, z, m, p, k in vox:
        occ[(x, y, z)] = p
        c = cent[p]
        c[0] += x + 0.5; c[1] += y + 0.5; c[2] += z + 0.5; c[3] += 1
        kind[p] = k
    off = {}
    for p, c in cent.items():
        if kind[p] == 0:
            off[p] = (0.0, 0.0, 0.0)
            continue
        cx, cy, cz = c[0] / c[3], c[1] / c[3], c[2] / c[3]
        d = (cx - hit[0], cy - hit[1], cz - hit[2])
        off[p] = tuple(explode * (0.6 * d[a] - 6.0 * hdir[a]) for a in range(3))  # toward the viewer

    ang = math.radians(30)

    def proj(x, y, z):
        X = x * view[0]
        Z = z * view[2]
        return ((X - Z) * math.cos(ang), (X + Z) * math.sin(ang) - y)

    faces = []
    dirs = [(0, view[0]), (1, view[1]), (2, view[2])]
    for x, y, z, m, p, k in vox:
        ox, oy, oz = off[p]
        base = MAT_RGB.get(m, (200, 0, 200)) if k == 0 else ((235, 235, 235) if k == 2 else piece_rgb(p))
        for a, s in dirs:
            n = [0, 0, 0]
            n[a] = int(s)
            if occ.get((x + n[0], y + n[1], z + n[2])) == p:
                continue  # hidden inside its own piece
            corners = []
            lo = [x + ox, y + oy, z + oz]
            fixed = lo[a] + (1 if s > 0 else 0)
            others = [b for b in range(3) if b != a]
            for du, dv in ((0, 0), (1, 0), (1, 1), (0, 1)):
                c = list(lo)
                c[a] = fixed
                c[others[0]] += du
                c[others[1]] += dv
                corners.append(c)
            depth = (x + ox + 0.5) * view[0] + (y + oy + 0.5) * view[1] + (z + oz + 0.5) * view[2]
            shade = {1: 1.0, 0: 0.82, 2: 0.68}[a]
            col = tuple(int(ch * shade) for ch in base)
            faces.append((depth, [proj(*c) for c in corners], col))
    faces.sort(key=lambda f: f[0])
    xs = [pt[0] for f in faces for pt in f[1]]
    ys = [pt[1] for f in faces for pt in f[1]]
    w, h = size
    sc = min((w - 40) / (max(xs) - min(xs) + 1e-6), (h - 70) / (max(ys) - min(ys) + 1e-6))
    img = Image.new("RGB", size, (32, 34, 40))
    dr = ImageDraw.Draw(img)
    for _, pts, col in faces:
        P = [((px - min(xs)) * sc + 20, (py - min(ys)) * sc + 50) for px, py in pts]
        dr.polygon(P, fill=col, outline=tuple(int(c * 0.55) for c in col))
    if title:
        dr.text((12, 12), title, fill=(230, 230, 230))
    return img


def face_map(dump, size=(500, 650)):
    """Face-on view from the hit side, nothing moved: the front-most voxel per column in its piece colour, with a
    dark line wherever two neighbouring columns belong to different pieces (a crack seen on the surface)."""
    dim, hit, hdir, vox = load(dump)
    ax = max(range(3), key=lambda a: abs(hdir[a]))
    toward = hdir[ax] > 0  # the hit travels toward +ax, so the front is the smallest coordinate
    u, v = [a for a in range(3) if a != ax]
    if v != 1 and u == 1:
        u, v = v, u
    name = os.path.basename(dump)
    front = {}
    for x, y, z, m, p, k in vox:
        c = (x, y, z)
        key = (c[u], c[v])
        depth = c[ax] if toward else -c[ax]
        if key not in front or depth < front[key][0]:
            col = MAT_RGB.get(m, (200, 0, 200)) if k == 0 else ((235, 235, 235) if k == 2 else piece_rgb(p))
            front[key] = (depth, p, col, c)

    def cut_between(c, axis):  # the bond from voxel c to its +axis neighbour
        return (CUTS.get((name,) + tuple(c), 0) >> axis) & 1
    W, H = dim[u], dim[v]
    w, h = size
    sc = max(1, int(min((w - 20) / W, (h - 70) / H)))
    img = Image.new("RGB", size, (32, 34, 40))
    dr = ImageDraw.Draw(img)
    x0, y0 = 10, 50
    for (a, b), (_, p, col, c) in front.items():
        X, Y = x0 + a * sc, y0 + (H - 1 - b) * sc
        dr.rectangle([X, Y, X + sc - 1, Y + sc - 1], fill=col)
    hair = (200, 40, 40)
    for (a, b), (d, p, col, c) in front.items():
        X, Y = x0 + a * sc, y0 + (H - 1 - b) * sc
        r = front.get((a + 1, b))
        if r and r[1] != p:
            dr.line([X + sc, Y, X + sc, Y + sc], fill=(10, 10, 10), width=2)
        elif r and r[0] == d and cut_between(c, u):
            dr.line([X + sc, Y, X + sc, Y + sc], fill=hair, width=2)
        t = front.get((a, b + 1))
        if t and t[1] != p:
            dr.line([X, Y, X + sc, Y], fill=(10, 10, 10), width=2)
        elif t and t[0] == d and cut_between(c, v):
            dr.line([X, Y, X + sc, Y], fill=hair, width=2)
    hu, hv = hit[u], hit[v]
    HX, HY = x0 + hu * sc, y0 + (H - hv) * sc
    dr.ellipse([HX - 5, HY - 5, HX + 5, HY + 5], outline=(255, 60, 60), width=2)
    dr.text((10, 12), "face-on from the hit side (red ring = hit);", fill=(230, 230, 230))
    dr.text((10, 28), "black = cracks between pieces; red = cut bonds that free nothing", fill=(230, 230, 230))
    return img


def panel(dump, title, explode=1.0):
    a = render(dump, explode, title=title)
    b = face_map(dump)
    img = Image.new("RGB", (a.width + b.width, a.height), (32, 34, 40))
    img.paste(a, (0, 0))
    img.paste(b, (a.width, 0))
    return img


def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    captions = {
        "stone_centre_e2": "stone wall, centre hit, E/T 2",
        "stone_centre_e5": "stone wall, centre hit, E/T 5",
        "stone_centre_e12": "stone wall, centre hit, E/T 12",
        "stone_centre_e30": "stone wall, centre hit, E/T 30",
        "stone_centre_e80": "stone wall, centre hit, E/T 80",
        "stone_edge_e5": "stone wall, edge hit, E/T 5",
        "brick_e8": "brick wall (running bond), E/T 8",
        "brick_e30": "brick wall (running bond), E/T 30",
        "beam_e8": "wooden beam, grain along x, hit from above, E/T 8",
        "glass_e20": "glass pane, E/T 20",
        "column_e30": "column under a lintel, column hit, E/T 30",
        "chair_e8": "chair (item voxels 1/64 m), leg hit, E/T 8",
    }
    note = "  [F4.1 CPU reference; loose pieces pushed apart for viewing; white = crumbs]"
    for tag, cap in captions.items():
        panel(os.path.join(src, tag + ".txt"), cap + note).save(os.path.join(out, f"f41_{tag}.png"))
    # the energy sweep as an animation
    frames = [panel(os.path.join(src, f"stone_centre_e{e}.txt"), f"stone wall, centre hit, E/T {e}" + note)
              for e in (2, 5, 12, 30, 80)]
    frames[0].save(os.path.join(out, "f41_stone_sweep.gif"), save_all=True, append_images=frames[1:], duration=1200,
                   loop=0)
    # the heavy hit coming apart
    frames = [render(os.path.join(src, "stone_centre_e80.txt"), t / 11.0,
                     title="stone wall, centre hit, E/T 80 (pieces pushed apart)" + note) for t in range(12)]
    frames[0].save(os.path.join(out, "f41_stone_e80_apart.gif"), save_all=True, append_images=frames[1:], duration=150,
                   loop=0)
    print("wrote", out)


if __name__ == "__main__":
    main()
