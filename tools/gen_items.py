#!/usr/bin/env python3
"""Deterministic fine-grid item generator — weapons, tools, and small props.

No LLM. Emits the fine-voxel template format:

    # grid: 81
    V x y z Material [tint=#rrggbb] [state=...]

One cell = 1/81 unit (~1.23 cm) — 3x finer than a microcube per axis, the
resolution class items need to stop looking like planks (CLAUDE.md handtool
rule; the engine-side loader greedy-merges cells into arbitrary-scale
kinematic boxes, so per-cell emission here costs nothing at render time).

Conventions (shared with weapons/sword_fine.voxel, scaled up):
  * +Y up, origin at the grip/base after normalization (min corner >= 0).
  * Wieldables: grip column is CENTERED on the recorded grip point; the
    manifest (resources/templates/items_manifest.json) carries grip_point and
    dims in UNITS for items.json `held` blocks.
  * Blades lie in the XY plane (thickness along Z), matching the character's
    model-space +Z facing so an attack swing presents the edge.
  * Tints do the fine color work (leather, steel edge, gilt, magic glow);
    materials carry physics + texture.

Usage:
    python tools/gen_items.py            # regenerate all
    python tools/gen_items.py sword_long # regenerate one
"""

import json
import math
import os
import sys

GRID = 81                      # cells per cube edge (1 cell = 1/81 unit)
ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
OUT_WEAPONS = os.path.join(ROOT, "resources", "templates", "weapons")
OUT_ITEMS = os.path.join(ROOT, "resources", "templates", "items")
MANIFEST = os.path.join(ROOT, "resources", "templates", "items_manifest.json")
CATALOG = os.path.join(ROOT, "resources", "templates", "template_catalog.json")

# Catalog metadata per asset: (display_name, description, subcategory, tags).
CATALOG_META = {
    "sword_long":   ("Longsword", "A hand-and-a-half longsword with a leather-wrapped grip.", "sword", ["weapon", "sword", "blade", "melee"]),
    "sword_short":  ("Short Sword", "A broad one-handed short sword.", "sword", ["weapon", "sword", "blade", "melee"]),
    "dagger":       ("Dagger", "A slim thrusting dagger.", "dagger", ["weapon", "dagger", "blade", "melee"]),
    "axe_hand":     ("Hand Axe", "A one-handed bearded axe.", "axe", ["weapon", "axe", "tool", "melee"]),
    "axe_battle":   ("Battle Axe", "A two-handed twin-bladed battle axe.", "axe", ["weapon", "axe", "melee", "two-handed"]),
    "spear":        ("Spear", "A long ash spear with a leaf-shaped head.", "spear", ["weapon", "spear", "polearm", "melee"]),
    "mace":         ("Flanged Mace", "A steel mace with four crushing flanges.", "mace", ["weapon", "mace", "blunt", "melee"]),
    "maul":         ("Maul", "A massive two-handed iron-headed maul.", "hammer", ["weapon", "maul", "hammer", "blunt", "two-handed"]),
    "warhammer":    ("Warhammer", "A one-handed warhammer with a back spike.", "hammer", ["weapon", "hammer", "blunt", "melee"]),
    "staff_fire":   ("Staff of Embers", "A fire-mage's staff with a caged ember orb.", "staff", ["weapon", "staff", "magic", "fire"]),
    "staff_frost":  ("Staff of Rime", "A frost-mage's staff with a pale blue orb.", "staff", ["weapon", "staff", "magic", "frost"]),
    "staff_nature": ("Staff of the Grove", "A living staff cradling a verdant orb.", "staff", ["weapon", "staff", "magic", "nature"]),
    "staff_arcane": ("Staff of the Arcanum", "A gold-chased staff with a violet crystal.", "staff", ["weapon", "staff", "magic", "arcane"]),
    "wand":         ("Wand", "A slender wand tipped with a violet gem.", "wand", ["weapon", "wand", "magic"]),
    "tome":         ("Leather Tome", "A gilt-cornered leather tome with a clasp.", "book", ["item", "book", "tome", "library", "study"]),
    "scroll":       ("Scroll", "A rolled parchment scroll with a ribbon.", "scroll", ["item", "scroll", "parchment", "study"]),
    "candlestick":  ("Candlestick", "A brass chamberstick with a lit candle.", "light", ["item", "candle", "light", "tavern", "house"]),
    "potion":       ("Potion Flask", "A round-bellied glass potion flask.", "potion", ["item", "potion", "flask", "alchemy"]),
}

# Palette — materials are physics/texture carriers, tints are the color detail.
# Blades/heads use Steel (bright albedo added 2026-08-06): Metal's iron texture
# is too dark — even a near-white tint left swords reading black in-engine.
WOOD = "Wood"
METAL = "Steel"
GOLD = "Gold"
STONE = "Stone"
GLASS = "Glass"
GLOW = "glow"
GLOW_BLUE = "glow_blue"
GLOW_GREEN = "glow_green"

T_LEATHER = "#5a3a22"       # wrapped grip
T_LEATHER_DARK = "#462c18"  # wrap band shadow
# Steel/iron tints run BRIGHT: tint multiplies the Metal albedo, which is
# already dark — mid-gray tints rendered near-black in-engine (verified
# live 2026-08-06). Near-white lets the metal texture carry the shading.
T_STEEL = "#e4e9ee"         # polished steel
T_STEEL_EDGE = "#ffffff"    # honed edge highlight
T_STEEL_DARK = "#b6bdc6"    # fuller / recess
T_IRON = "#c2c6cc"          # rough iron
T_IRON_DARK = "#83878e"
T_WOOD_DARK = "#4c3319"     # stained haft
T_WOOD_PALE = "#a98753"     # ash shaft
T_GILT = "#e0b84c"          # gold trim
T_PAPER = "#efe6cf"         # parchment
T_PAPER_EDGE = "#d8c9a4"    # page block edge
T_CANDLE = "#f2ead6"        # wax


class Model:
    """A sparse fine-grid canvas. Signed coords allowed; emit() normalizes."""

    def __init__(self, name, category="item"):
        self.name = name
        self.category = category
        self.cells = {}          # (x,y,z) -> (material, tint_or_None, state_or_None)
        self.grip = None         # (x,y,z) cell marked as the held grip center

    def v(self, x, y, z, mat, tint=None, state=None):
        self.cells[(x, y, z)] = (mat, tint, state)

    def fill(self, x0, x1, y0, y1, z0, z1, mat, tint=None, state=None):
        """Inclusive box fill."""
        for x in range(x0, x1 + 1):
            for y in range(y0, y1 + 1):
                for z in range(z0, z1 + 1):
                    self.v(x, y, z, mat, tint, state)

    def clear(self, x0, x1, y0, y1, z0, z1):
        for x in range(x0, x1 + 1):
            for y in range(y0, y1 + 1):
                for z in range(z0, z1 + 1):
                    self.cells.pop((x, y, z), None)

    def disc(self, cx, cz, y, r, mat, tint=None, ry=None):
        """Filled circle in the XZ plane (ry: optional second radius for ovals)."""
        rz = ry if ry is not None else r
        for x in range(cx - r, cx + r + 1):
            for z in range(cz - rz, cz + rz + 1):
                if (x - cx) ** 2 / max(r * r, 1) + (z - cz) ** 2 / max(rz * rz, 1) <= 1.001:
                    self.v(x, y, z, mat, tint)

    def column(self, cx, cz, y0, y1, half, mat, tint=None):
        """Vertical square column, (2*half+1) or (2*half) wide — see hw()."""
        x0, x1, z0, z1 = hw(cx, half), hw2(cx, half), hw(cz, half), hw2(cz, half)
        self.fill(x0, x1, y0, y1, z0, z1, mat, tint)

    def mark_grip(self, x, y, z):
        self.grip = (x, y, z)

    def bounds(self):
        xs = [c[0] for c in self.cells]
        ys = [c[1] for c in self.cells]
        zs = [c[2] for c in self.cells]
        return (min(xs), min(ys), min(zs)), (max(xs), max(ys), max(zs))

    def emit(self, out_dir):
        if not self.cells:
            raise ValueError(f"{self.name}: empty model")
        (mnx, mny, mnz), (mxx, mxy, mxz) = self.bounds()
        shift = (-mnx, -mny, -mnz)
        dims = (mxx - mnx + 1, mxy - mny + 1, mxz - mnz + 1)

        lines = [
            f"# {self.name} — fine-grid item ({GRID} cells/cube, 1 cell = {1.0/GRID:.4f} u)",
            f"# Generated by tools/gen_items.py — DO NOT hand-edit; edit the generator.",
            f"# dims: {dims[0]}x{dims[1]}x{dims[2]} cells "
            f"({dims[0]/GRID:.3f}x{dims[1]/GRID:.3f}x{dims[2]/GRID:.3f} u)",
            f"# grid: {GRID}",
            f"# category: {self.category}",
        ]
        for (x, y, z) in sorted(self.cells):
            mat, tint, state = self.cells[(x, y, z)]
            line = f"V {x + shift[0]} {y + shift[1]} {z + shift[2]} {mat}"
            if tint:
                line += f" tint={tint}"
            if state:
                line += f" state={state}"
            lines.append(line)

        os.makedirs(out_dir, exist_ok=True)
        path = os.path.join(out_dir, self.name + ".voxel")
        with open(path, "w", newline="\n") as f:
            f.write("\n".join(lines) + "\n")

        entry = {
            "file": os.path.relpath(path, os.path.join(ROOT, "resources", "templates")).replace("\\", "/"),
            "grid": GRID,
            "cells": len(self.cells),
            "dims_units": [round(d / GRID, 4) for d in dims],
        }
        if self.grip:
            entry["grip_point_units"] = [
                round((self.grip[i] + shift[i] + 0.5) / GRID, 4) for i in range(3)
            ]
        print(f"  {self.name:16s} {len(self.cells):6d} cells  "
              f"{dims[0]/GRID:.2f}x{dims[1]/GRID:.2f}x{dims[2]/GRID:.2f} u")
        return entry


def hw(c, half):
    """Min coord of a column 2*half wide centered between c and c+1 (even
    widths straddle the center line so grips stay symmetric)."""
    return c - half + 1


def hw2(c, half):
    return c + half


# --------------------------------------------------------------------------
# Shared part builders
# --------------------------------------------------------------------------

def wrapped_grip(m, y0, y1, half=1, base=T_LEATHER, band=T_LEATHER_DARK, step=3):
    """Leather-wrapped grip: alternate tint bands every `step` cells."""
    for y in range(y0, y1 + 1):
        tint = band if ((y - y0) // step) % 2 else base
        m.column(0, 0, y, y, half, WOOD, tint)


def taper_blade(m, y0, length, w0, thick_half=1, mat=METAL,
                body=T_STEEL, edge=T_STEEL_EDGE, fuller=T_STEEL_DARK):
    """Straight double-edged blade along +Y in the XY plane.

    Width tapers from w0 half-cells to a point. Edge columns get the honed
    tint; the center column a fuller (groove) tint. Thickness 2*thick_half.
    """
    for i in range(length):
        y = y0 + i
        frac = i / max(length - 1, 1)
        w = max(0, int(round(w0 * (1.0 - frac ** 1.7))))
        for x in range(-w, w + 1):
            tint = edge if abs(x) == w and w > 0 else (fuller if x == 0 else body)
            for z in range(hw(0, thick_half), hw2(0, thick_half) + 1):
                m.v(x, y, z, mat, tint)


def crossguard(m, y0, span, depth_half=1, droop=1, mat=METAL, tint=T_STEEL,
               tip=T_GILT):
    """Horizontal guard bar along X with drooped, gilt-capped ends."""
    m.fill(-span, span, y0, y0 + 1, hw(0, depth_half), hw2(0, depth_half), mat, tint)
    for s in (-span, span):
        m.fill(s, s, y0 - droop, y0 - 1, hw(0, depth_half), hw2(0, depth_half), mat, tint)
        m.fill(s, s, y0 - droop, y0 - droop, hw(0, depth_half), hw2(0, depth_half), mat, tip)


def pommel(m, y0, r=2, mat=METAL, tint=T_STEEL, cap=T_GILT):
    for y in range(y0, y0 + r + 1):
        rr = r if y > y0 else r - 1
        m.disc(0, 0, y, rr, mat, cap if y == y0 + r else tint)


# --------------------------------------------------------------------------
# Weapons
# --------------------------------------------------------------------------

def gen_sword_long():
    m = Model("sword_long", "item")
    pommel(m, 0, r=2)
    wrapped_grip(m, 3, 16, half=1)                 # 14 cells ≈ 17 cm — hand-and-a-half
    m.mark_grip(0, 9, 0)
    crossguard(m, 17, span=8, droop=2)
    taper_blade(m, 19, 80, w0=3)                   # ~99 cm blade
    return m, OUT_WEAPONS


def gen_sword_short():
    m = Model("sword_short", "item")
    pommel(m, 0, r=2)
    wrapped_grip(m, 3, 12, half=1)
    m.mark_grip(0, 7, 0)
    crossguard(m, 13, span=6, droop=1)
    taper_blade(m, 15, 46, w0=3)                   # ~57 cm blade, broader profile
    return m, OUT_WEAPONS


def gen_dagger():
    m = Model("dagger", "item")
    pommel(m, 0, r=1, tint=T_IRON)
    wrapped_grip(m, 2, 9, half=1, step=2)
    m.mark_grip(0, 5, 0)
    crossguard(m, 10, span=4, droop=1, tint=T_IRON, tip=T_IRON_DARK)
    taper_blade(m, 12, 22, w0=2, body=T_STEEL, edge=T_STEEL_EDGE, fuller=T_STEEL)
    return m, OUT_WEAPONS


def _axe_head(m, y_lo, y_hi, x_eye, reach, tint_body=T_IRON, tint_edge=T_STEEL_EDGE,
              mirror=False):
    """Bearded axe head: cheek wedge from the eye out to a curved edge.

    The cutting edge is an arc bulging outward; the lower horn (beard) drops
    below y_lo. Thickness tapers 3 cells at the eye -> 1 at the edge.
    """
    h = y_hi - y_lo
    for x in range(x_eye, x_eye + reach + 1):
        f = (x - x_eye) / max(reach, 1)
        # Edge arc: head grows taller toward the edge, beard drops down.
        grow = int(round(3 * f))
        beard = int(round(6 * f ** 1.5))
        lo, hi = y_lo - beard, y_hi + grow - 1
        thick = 1 if f > 0.6 else (2 if f > 0.25 else 3)
        z0, z1 = -(thick - 1) // 2 - (0 if thick % 2 else 0), 0
        z0 = -(thick // 2)
        z1 = (thick - 1) // 2
        for y in range(lo, hi + 1):
            edge_col = f > 0.92
            tint = tint_edge if edge_col else tint_body
            for z in range(z0, z1 + 1):
                xx = -x if mirror else x
                m.v(xx, y, z, METAL, tint)


def gen_axe_hand():
    m = Model("axe_hand", "item")
    # Haft: 55 cm, slight knob at butt.
    m.column(0, 0, 0, 1, 1, WOOD, T_WOOD_DARK)
    wrapped_grip(m, 2, 12, half=1, step=3)
    m.mark_grip(0, 7, 0)
    m.column(0, 0, 13, 44, 1, WOOD, T_WOOD_PALE)
    # Head: eye wraps the haft near the top; blade reaches +X ~15 cm.
    m.fill(-2, 1, 38, 45, -1, 1, METAL, T_IRON_DARK)   # eye + poll
    _axe_head(m, 38, 45, 2, 12)
    return m, OUT_WEAPONS


def gen_axe_battle():
    m = Model("axe_battle", "item")
    # Long haft: 1.35 u.
    m.column(0, 0, 0, 1, 1, WOOD, T_WOOD_DARK)
    wrapped_grip(m, 2, 20, half=1, step=4)
    m.mark_grip(0, 12, 0)
    m.column(0, 0, 21, 102, 1, WOOD, T_WOOD_DARK)
    m.column(0, 0, 103, 108, 1, WOOD, T_GILT)          # gilt collar at the top
    # Twin bearded blades, mirrored.
    m.fill(-2, 2, 88, 101, -1, 1, METAL, T_IRON_DARK)  # central eye block
    _axe_head(m, 88, 100, 3, 14)
    _axe_head(m, 88, 100, 3, 14, mirror=True)
    # Top spike.
    for i in range(8):
        w = max(0, 1 - i // 4)
        m.fill(-w, w, 109 + i, 109 + i, 0, 0, METAL, T_STEEL)
    return m, OUT_WEAPONS


def gen_spear():
    m = Model("spear", "item")
    # 1.55 u ash shaft (2-cell cross-section), butt cap.
    m.column(0, 0, 0, 2, 1, METAL, T_IRON_DARK)
    m.column(0, 0, 3, 125, 1, WOOD, T_WOOD_PALE)
    m.mark_grip(0, 70, 0)
    wrapped_grip(m, 62, 78, half=1, step=3)            # mid-shaft grip wrap
    # Socket + leaf-shaped head (~28 cm).
    m.column(0, 0, 126, 131, 1, METAL, T_IRON_DARK)
    for i in range(23):
        y = 132 + i
        f = i / 22.0
        w = int(round(3 * math.sin(min(1.0, f * 1.25) * math.pi) ** 0.8))
        if i > 18:
            w = max(0, 22 - i)
        for x in range(-w, w + 1):
            tint = T_STEEL_EDGE if abs(x) == w and w > 0 else T_STEEL
            m.v(x, y, 0, METAL, tint)
    return m, OUT_WEAPONS


def gen_mace():
    m = Model("mace", "item")
    pommel(m, 0, r=1, tint=T_IRON)
    wrapped_grip(m, 2, 14, half=1, step=3)
    m.mark_grip(0, 8, 0)
    m.column(0, 0, 15, 40, 1, METAL, T_IRON_DARK)      # steel haft
    # Flanged head: core cylinder + 4 radial fins + crown stud.
    for y in range(41, 55):
        m.disc(0, 0, y, 2, METAL, T_IRON)
    for y in range(42, 54):
        f = abs((y - 48) / 6.0)
        reach = int(round(5 * (1.0 - f * f)))
        for d in range(3, 3 + reach):
            for (fx, fz) in ((d, 0), (-d, 0), (0, d), (0, -d)):
                tint = T_STEEL_EDGE if d == 2 + reach else T_STEEL
                m.v(fx, y, fz, METAL, tint)
    m.fill(-1, 1, 55, 57, -1, 1, METAL, T_IRON)        # crown
    m.v(0, 58, 0, METAL, T_STEEL_EDGE)
    return m, OUT_WEAPONS


def gen_maul():
    m = Model("maul", "item")
    # Heavy two-hand haft.
    m.column(0, 0, 0, 1, 1, METAL, T_IRON_DARK)
    wrapped_grip(m, 2, 24, half=1, step=4)
    m.mark_grip(0, 14, 0)
    m.column(0, 0, 25, 94, 1, WOOD, T_WOOD_DARK)
    # Massive stone-gray head with iron bands, slightly domed striking faces.
    m.fill(-11, 11, 95, 108, -5, 5, METAL, T_IRON)
    m.fill(-12, -12, 97, 106, -4, 4, METAL, T_IRON)    # domed -X face
    m.fill(12, 12, 97, 106, -4, 4, METAL, T_IRON)      # domed +X face
    for bx in (-8, 8):                                  # gilt retaining bands
        m.fill(bx - 1, bx + 1, 95, 108, -5, 5, METAL, T_IRON_DARK)
        m.fill(bx - 1, bx + 1, 108, 108, -5, 5, GOLD, None)
    return m, OUT_WEAPONS


def gen_warhammer():
    m = Model("warhammer", "item")
    pommel(m, 0, r=1, tint=T_IRON)
    wrapped_grip(m, 2, 16, half=1, step=3)
    m.mark_grip(0, 9, 0)
    m.column(0, 0, 17, 52, 1, METAL, T_IRON_DARK)
    # Hammer face (+X) and back spike (-X).
    m.fill(2, 9, 46, 57, -3, 3, METAL, T_IRON)
    m.fill(10, 10, 48, 55, -2, 2, METAL, T_STEEL)      # proud striking face
    for i in range(9):
        w = max(0, 2 - i // 3)
        m.fill(-3 - i, -3 - i, 50 - w + 1, 51 + w, -w, w, METAL,
               T_STEEL_EDGE if i > 6 else T_IRON)
    m.fill(-2, 1, 46, 57, -2, 2, METAL, T_IRON_DARK)   # eye block
    return m, OUT_WEAPONS


def _staff(name, orb_mat, orb_tint, accent, shaft_tint, wrap_tint):
    m = Model(name, "item")
    # Full-height walking staff, 1.75 u.
    m.column(0, 0, 0, 2, 1, METAL, accent)             # ferrule
    m.column(0, 0, 3, 118, 1, WOOD, shaft_tint)
    wrapped_grip(m, 52, 70, half=1, base=wrap_tint,
                 band=T_LEATHER_DARK, step=4)
    m.mark_grip(0, 61, 0)
    # Collar + four prongs cradling a floating orb.
    m.column(0, 0, 119, 123, 1, METAL, accent)
    for (px, pz) in ((3, 0), (-3, 0), (0, 3), (0, -3)):
        for i in range(10):
            y = 122 + i
            # Prongs bow outward then curl back in over the orb.
            r = 3 + (1 if 2 < i < 7 else 0) - (2 if i >= 8 else 0)
            x = int(math.copysign(r, px)) if px else 0
            z = int(math.copysign(r, pz)) if pz else 0
            m.v(x, y, z, METAL, accent)
    # Orb: 5-cell sphere of school-colored glow, floating between the prongs.
    for x in range(-2, 3):
        for y in range(-2, 3):
            for z in range(-2, 3):
                if x * x + y * y + z * z <= 5:
                    m.v(x, 128 + y, z, orb_mat, orb_tint)
    return m, OUT_WEAPONS


def gen_staff_fire():
    return _staff("staff_fire", GLOW, "#ff7828", "#b0703c", T_WOOD_DARK, T_LEATHER)


def gen_staff_frost():
    return _staff("staff_frost", GLOW_BLUE, None, "#c8d8e8", "#7b8a99", "#31414f")


def gen_staff_nature():
    return _staff("staff_nature", GLOW_GREEN, None, "#5a7a3a", T_WOOD_PALE, "#3a5a2a")


def gen_staff_arcane():
    return _staff("staff_arcane", GLASS, "#b060ff", T_GILT, "#3a2a4a", "#241a30")


def gen_wand():
    m = Model("wand", "item")
    m.column(0, 0, 0, 7, 1, WOOD, T_WOOD_DARK)         # handle, 2-cell
    m.mark_grip(0, 4, 0)
    m.v(0, 8, 0, GOLD, None)                            # gilt ring
    for y in range(9, 26):                              # tapering 1-cell shaft
        m.v(0, y, 0, WOOD, T_WOOD_PALE)
    m.v(0, 26, 0, GLASS, "#b060ff")                     # gem tip
    m.v(0, 27, 0, GLASS, "#d0a0ff")
    return m, OUT_WEAPONS


# --------------------------------------------------------------------------
# Props
# --------------------------------------------------------------------------

def gen_tome():
    m = Model("tome", "item")
    # Closed tome lying flat: 24 x 30 x 7 cm -> 19 x 24 x 6 cells (X x Z x Y).
    W, D, H = 19, 24, 6
    # Page block (inset 1 cell from cover edges except the spine at x=0).
    m.fill(1, W - 2, 1, H - 2, 1, D - 2, WOOD, T_PAPER_EDGE)
    # Leather covers top/bottom + spine wrap.
    m.fill(0, W - 1, 0, 0, 0, D - 1, WOOD, "#6a2a1a")
    m.fill(0, W - 1, H - 1, H - 1, 0, D - 1, WOOD, "#6a2a1a")
    m.fill(0, 0, 0, H - 1, 0, D - 1, WOOD, "#521f12")
    # Gilt corner caps + clasp.
    for (x, z) in ((W - 1, 0), (W - 1, D - 1)):
        m.fill(x - 1, x, 0, H - 1, z if z == 0 else z - 1, z if z == 0 else z, GOLD, None)
    m.fill(W - 1, W - 1, 2, H - 3, D // 2 - 1, D // 2 + 1, GOLD, None)   # clasp
    # Embossed rune on the front cover.
    m.fill(W // 2 - 1, W // 2 + 1, H - 1, H - 1, D // 2 - 3, D // 2 - 3, GOLD, None)
    m.fill(W // 2, W // 2, H - 1, H - 1, D // 2 - 5, D // 2 + 3, GOLD, None)
    return m, OUT_ITEMS


def gen_scroll():
    m = Model("scroll", "item")
    # Rolled parchment on a wooden rod, lying along X (~22 cm).
    L = 18
    for x in range(0, L):
        # Parchment roll: radius-2 disc in the YZ plane.
        for y in range(-2, 3):
            for z in range(-2, 3):
                if y * y + z * z <= 4:
                    tint = T_PAPER if y * y + z * z < 4 else T_PAPER_EDGE
                    m.v(x, y + 2, z, WOOD, tint)
    # Rod ends + knobs.
    for x in (-1, L):
        m.v(x, 2, 0, WOOD, T_WOOD_DARK)
    for x in (-2, L + 1):
        m.fill(x, x, 1, 3, -1, 1, WOOD, T_WOOD_DARK)
        m.clear(x, x, 1, 1, -1, -1); m.clear(x, x, 1, 1, 1, 1)
        m.clear(x, x, 3, 3, -1, -1); m.clear(x, x, 3, 3, 1, 1)
    # Ribbon band.
    for y in range(0, 5):
        for z in range(-2, 3):
            if y in (0, 4) or abs(z) == 2:
                if (y - 2) ** 2 + z * z <= 6:
                    m.v(L // 2, y, z, WOOD, "#a02030")
    return m, OUT_ITEMS


def gen_candlestick():
    m = Model("candlestick", "item")
    # Brass chamberstick: broad base, short stem, drip pan, candle, flame.
    m.disc(0, 0, 0, 4, GOLD)
    m.disc(0, 0, 1, 3, GOLD)
    m.column(0, 0, 2, 6, 1, GOLD)
    m.disc(0, 0, 7, 2, GOLD)                            # drip pan
    m.column(0, 0, 8, 14, 1, WOOD, T_CANDLE)            # candle (2-cell)
    m.v(0, 15, 0, GLOW, "#ffdf90")                      # flame
    m.v(0, 16, 0, GLOW, "#ffb050")
    # Finger loop on the base rim.
    m.fill(4, 6, 1, 1, 0, 0, GOLD)
    m.fill(6, 6, 1, 3, 0, 0, GOLD)
    return m, OUT_ITEMS


def gen_potion():
    m = Model("potion", "item")
    # Round-bellied flask (~11 cm): glass body, liquid tint, cork.
    for y in range(0, 6):
        r = (2, 3, 3, 3, 2, 1)[y]
        m.disc(0, 0, y, r, GLASS, "#3fa050" if y < 4 else None)
    m.column(0, 0, 6, 7, 1, GLASS)
    m.column(0, 0, 8, 9, 1, WOOD, T_WOOD_PALE)          # cork
    return m, OUT_ITEMS


ALL = [
    gen_sword_long, gen_sword_short, gen_dagger,
    gen_axe_hand, gen_axe_battle,
    gen_spear, gen_mace, gen_maul, gen_warhammer,
    gen_staff_fire, gen_staff_frost, gen_staff_nature, gen_staff_arcane,
    gen_wand,
    gen_tome, gen_scroll, gen_candlestick, gen_potion,
]


def register_catalog(manifest):
    """Register generated assets in template_catalog.json so the agent-facing
    search surface (search_templates MCP) can find them. Deterministic: no
    timestamps, stable key order — regeneration is a no-op diff."""
    catalog = {}
    if os.path.exists(CATALOG):
        with open(CATALOG, encoding="utf-8") as f:
            catalog = json.load(f)
    for name, entry in manifest.items():
        meta = CATALOG_META.get(name)
        if not meta:
            continue
        display, desc, subcat, tags = meta
        category = "weapon" if "weapon" in tags else "item"
        catalog[name] = {
            "display_name": display,
            "description": desc,
            "category": category,
            "subcategory": subcat,
            "tags": tags,
            "file": entry["file"],
            "method": "gen_items",
            "size": max(entry["dims_units"]),
            "cubes": 0,
            "subcubes": 0,
            "microcubes": 0,
            "fine_voxels": entry["cells"],
            "fine_grid": entry["grid"],
            "total": entry["cells"],
            "version": 1,
        }
    with open(CATALOG, "w", newline="\n", encoding="utf-8") as f:
        json.dump(catalog, f, indent=2, sort_keys=True)
    print(f"catalog  -> {os.path.relpath(CATALOG, ROOT)}")


def main():
    only = set(sys.argv[1:])
    manifest = {}
    if os.path.exists(MANIFEST):
        with open(MANIFEST) as f:
            manifest = json.load(f)
    print(f"gen_items.py — grid {GRID} ({1.0/GRID*100:.2f} cm/cell)")
    for gen in ALL:
        name = gen.__name__[len("gen_"):]
        if only and name not in only:
            continue
        model, out_dir = gen()
        manifest[model.name] = model.emit(out_dir)
    with open(MANIFEST, "w", newline="\n") as f:
        json.dump(manifest, f, indent=2, sort_keys=True)
    print(f"manifest -> {os.path.relpath(MANIFEST, ROOT)}")
    register_catalog(manifest)


if __name__ == "__main__":
    main()
