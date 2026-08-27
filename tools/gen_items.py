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
OUT_TEST = os.path.join(ROOT, "resources", "templates", "test")
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
    "rug_woven":    ("Woven Rug", "A hand-woven rug with a projected pattern; a loose furnishing, not baked voxels.", "rug", ["item", "rug", "decor", "house", "tavern"]),
    "pickaxe":      ("Pickaxe", "A steel mining pick on an ash haft.", "pickaxe", ["weapon", "tool", "pickaxe", "mining"]),
    "torch":        ("Torch", "A tar-wrapped torch with a glowing ember crown.", "torch", ["item", "torch", "light"]),
    "tome_arcane":  ("Arcane Tome", "A slim violet tome; cover art projected across the boards.", "book", ["item", "book", "tome", "magic", "arcane"]),
    "tome_fire":    ("Tome of Embers", "A slim ember-red tome with projected cover art.", "book", ["item", "book", "tome", "magic", "fire"]),
    "tome_necromantic": ("Necromantic Tome", "A slim grave-green tome with projected cover art.", "book", ["item", "book", "tome", "magic", "necromancy"]),
    "rug_oriental": ("Oriental Rug", "A hand-knotted oriental rug, one fine cell thick.", "rug", ["item", "rug", "decor", "projected-surface"]),
    "rug_test":     ("Rug (Projection Test)", "Surface-projection test rug.", "rug", ["item", "rug", "test"]),
    "sign_prancing_pony": ("Sign: The Prancing Pony", "A thin hanging tavern sign board.", "sign", ["item", "sign", "decor", "tavern"]),
    "sign_blacksmith":    ("Sign: Smithy", "A hanging blacksmith trade sign (anvil and hammer).", "sign", ["item", "sign", "decor", "blacksmith", "trade"]),
    "sign_bakery":        ("Sign: Bakery", "A hanging bakery trade sign (the guild pretzel).", "sign", ["item", "sign", "decor", "bakery", "trade"]),
    "sign_general_store": ("Sign: Goods", "A hanging general-store trade sign (merchant's balance).", "sign", ["item", "sign", "decor", "general_store", "trade"]),
    "sign_apothecary":    ("Sign: Herbs", "A hanging apothecary trade sign (mortar and pestle).", "sign", ["item", "sign", "decor", "apothecary", "trade"]),
    "sign_butcher":       ("Sign: Butcher", "A hanging butcher trade sign (cleaver).", "sign", ["item", "sign", "decor", "butcher", "trade"]),
    "lantern":      ("Hooded Lantern", "A steel-framed glass lantern with a carry ring.", "light", ["item", "lantern", "light", "dungeon"]),
    "oil_lamp":     ("Oil Lamp", "A squat clay oil lamp with a wick flame.", "light", ["item", "lamp", "light", "house"]),
    "candle":       ("Candle", "A bare wax candle stub, lit.", "light", ["item", "candle", "light", "table"]),
    "firewood":     ("Firewood Billet", "A split billet of stove-length firewood.", "fuel", ["item", "firewood", "log", "fuel", "hearth"]),
    "flaming_log":  ("Flaming Log", "A charred, ember-cracked log burning in a hearth.", "fuel", ["item", "firewood", "log", "fuel", "hearth", "fire", "light"]),
    "tankard":      ("Tankard", "A stave-built ale tankard with steel bands.", "tableware", ["item", "tankard", "mug", "tavern", "drink"]),
    "goblet":       ("Goblet", "A pewter goblet of wine.", "tableware", ["item", "goblet", "tavern", "drink"]),
    "plate":        ("Plate", "A turned wooden dinner plate.", "tableware", ["item", "plate", "tavern", "kitchen"]),
    "bowl":         ("Bowl", "A turned wooden bowl.", "tableware", ["item", "bowl", "tavern", "kitchen"]),
    "jug":          ("Clay Jug", "A bulged clay jug with a strap handle.", "tableware", ["item", "jug", "tavern", "kitchen"]),
    "bottle_wine":  ("Wine Bottle", "A slim green glass bottle, corked.", "tableware", ["item", "bottle", "wine", "tavern"]),
    "fork":         ("Fork", "A steel three-tine fork.", "cutlery", ["item", "fork", "cutlery", "tavern"]),
    "knife_table":  ("Table Knife", "A wood-handled table knife.", "cutlery", ["item", "knife", "cutlery", "tavern"]),
    "spoon":        ("Spoon", "A steel spoon.", "cutlery", ["item", "spoon", "cutlery", "tavern"]),
    "frying_pan":   ("Frying Pan", "An iron skillet.", "kitchen", ["item", "pan", "kitchen", "tavern"]),
    "ladle":        ("Ladle", "An iron serving ladle.", "kitchen", ["item", "ladle", "kitchen", "tavern"]),
    "bow":          ("Longbow", "A curved yew longbow with a wrapped grip.", "bow", ["weapon", "bow", "ranged"]),
    "crossbow":     ("Crossbow", "A steel-prod crossbow.", "crossbow", ["weapon", "crossbow", "ranged"]),
    "quiver":       ("Quiver", "A leather quiver of arrows.", "quiver", ["item", "quiver", "ranged", "arrows"]),
    "throwing_knife": ("Throwing Knife", "A balanced steel throwing knife.", "knife", ["weapon", "knife", "ranged", "thrown"]),
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
T_BARK = "#5b4630"          # split-billet bark face
T_SPLIT = "#c8a877"         # pale riven face (freshly split wood)
T_CHAR = "#241d18"          # charred billet
T_EMBER = "#ff7a1e"         # glowing ember


class Model:
    """A sparse fine-grid canvas. Signed coords allowed; emit() normalizes.

    grid: cells per cube edge (defaults to the global GRID=81). Broad flat
    props (rugs) use 27 — the projected surface carries their look, and 81
    would cost ~40k cells for no visible gain.
    surface: optional (texture_material, axis) planar projected surface —
    one image spans the whole footprint (rugs, paintings, banners)."""

    def __init__(self, name, category="item", grid=None):
        self.name = name
        self.category = category
        self.grid = grid or GRID
        self.surface = None      # (texture_material_name, axis_char)
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

        g = self.grid
        lines = [
            f"# {self.name} — fine-grid item ({g} cells/cube, 1 cell = {1.0/g:.4f} u)",
            f"# Generated by tools/gen_items.py — DO NOT hand-edit; edit the generator.",
            f"# dims: {dims[0]}x{dims[1]}x{dims[2]} cells "
            f"({dims[0]/g:.3f}x{dims[1]/g:.3f}x{dims[2]/g:.3f} u)",
            f"# grid: {g}",
            f"# category: {self.category}",
        ]
        if self.surface:
            lines.append(f"# surface: texture={self.surface[0]} projection=planar "
                         f"axis={self.surface[1]}")
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
            "grid": g,
            "cells": len(self.cells),
            "dims_units": [round(d / g, 4) for d in dims],
        }
        if self.grip:
            entry["grip_point_units"] = [
                round((self.grip[i] + shift[i] + 0.5) / g, 4) for i in range(3)
            ]
        print(f"  {self.name:16s} {len(self.cells):6d} cells  "
              f"{dims[0]/g:.2f}x{dims[1]/g:.2f}x{dims[2]/g:.2f} u")
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
# Lighting items
# --------------------------------------------------------------------------

def gen_lantern():
    # Hooded candle lantern (~16 cm tall, 6 cm across): compact 5x5 steel
    # frame, 3-wide glass panes, candle + glow, domed cap, small carry ring.
    m = Model("lantern", "item")
    m.fill(-2, 2, 0, 0, -2, 2, METAL, T_IRON_DARK)      # base plate 5x5
    for (px, pz) in ((-2, -2), (-2, 2), (2, -2), (2, 2)):
        m.fill(px, px, 1, 6, pz, pz, METAL, T_IRON_DARK)  # corner posts
    for y in range(1, 7):                                # glass panes 3-wide
        for a in range(-1, 2):
            m.v(a, y, -2, GLASS, None)
            m.v(a, y, 2, GLASS, None)
            m.v(-2, y, a, GLASS, None)
            m.v(2, y, a, GLASS, None)
    m.v(0, 1, 0, WOOD, T_CANDLE)                         # candle stub
    m.fill(0, 0, 2, 3, 0, 0, GLOW, "#ffc860")            # flame core
    m.fill(-2, 2, 7, 7, -2, 2, METAL, T_IRON_DARK)       # cap plate
    m.fill(-1, 1, 8, 8, -1, 1, METAL, T_IRON)            # dome
    m.v(0, 9, 0, METAL, T_IRON)
    m.v(-1, 10, 0, METAL, T_IRON)                        # carry ring
    m.v(1, 10, 0, METAL, T_IRON)
    m.v(0, 11, 0, METAL, T_IRON)
    m.mark_grip(0, 10, 0)
    return m, OUT_ITEMS


def gen_oil_lamp():
    # Table oil lamp (~22 cm long): genie-lamp profile — shaded clay body,
    # tapering spout with a wick flame, loop handle, small lid knob.
    m = Model("oil_lamp", "item")
    def shaded_disc(y, r):
        for x in range(-r, r + 1):
            for z in range(-r, r + 1):
                d2 = x * x + z * z
                if d2 <= r * r + 1:
                    t = "#b57c4e" if d2 <= (r - 1) * (r - 1) else "#8a5a34"
                    m.v(x, y, z, WOOD, t)
    for y, r in enumerate((4, 5, 6, 6, 5, 3)):
        shaded_disc(y, r)
    m.v(0, 6, 0, WOOD, "#8a5a34")                       # lid knob
    m.v(0, 7, 0, GOLD, None)
    for i in range(6):                                   # tapering spout
        y = 3 + i // 3
        m.fill(6 + i, 6 + i, y, y + (1 if i < 4 else 0), 0, 0, WOOD, "#a06a3e")
    m.v(12, 5, 0, GLOW, "#ffb050")                       # wick flame
    m.v(12, 6, 0, GLOW, "#ffd070")
    for y in range(2, 7):                                # handle loop
        m.v(-6, y, 0, WOOD, "#8a5a34")
    m.v(-5, 7, 0, WOOD, "#8a5a34")
    m.v(-4, 7, 0, WOOD, "#8a5a34")
    m.v(-5, 1, 0, WOOD, "#8a5a34")
    m.mark_grip(-6, 4, 0)
    return m, OUT_ITEMS


def gen_candle():
    # Bare candle stub (~8 cm) with flame — scatter lighting for tables.
    m = Model("candle", "item")
    m.fill(-1, 1, 0, 5, -1, 1, WOOD, T_CANDLE)
    m.v(0, 6, 0, GLOW, "#ffdf90")
    m.mark_grip(0, 2, 0)
    return m, OUT_ITEMS


# --------------------------------------------------------------------------
# Tavern tableware
# --------------------------------------------------------------------------

def gen_tankard():
    # Stave tankard (~11 cm): wooden staves, two steel bands, D handle.
    m = Model("tankard", "item")
    for y in range(0, 8):
        band = y in (1, 6)
        m.disc(0, 0, y, 3, METAL if band else WOOD,
               T_IRON if band else T_WOOD_PALE)
    m.disc(0, 0, 7, 2, WOOD, "#6a4a2a")                # ale surface recess
    for y in range(1, 7):
        m.v(5, y, 0, WOOD, T_WOOD_DARK)                # handle spine
    m.v(4, 1, 0, WOOD, T_WOOD_DARK)
    m.v(4, 6, 0, WOOD, T_WOOD_DARK)
    m.mark_grip(5, 3, 0)
    return m, OUT_ITEMS


def gen_goblet():
    # Slim pewter goblet (~14 cm): small shaded foot, 1-cell stem with a knop,
    # thin-walled flaring cup with a wine surface. Walls are RINGS, not solid
    # discs — solid cup discs read as a blocky lump.
    m = Model("goblet", "item")
    def ring(y, r, mat, tint):
        for x in range(-r, r + 1):
            for z in range(-r, r + 1):
                d2 = x * x + z * z
                if (r - 1) * (r - 1) < d2 <= r * r + 1:
                    m.v(x, y, z, mat, tint)
    m.disc(0, 0, 0, 2, METAL, T_STEEL_DARK)             # foot
    m.v(0, 1, 0, METAL, T_STEEL)
    for y in range(2, 6):                                # stem
        m.v(0, y, 0, METAL, T_STEEL)
    m.fill(0, 0, 3, 3, 0, 0, METAL, T_STEEL_EDGE)        # knop glint
    m.disc(0, 0, 6, 2, METAL, T_STEEL)                   # cup bottom
    ring(7, 2, METAL, T_STEEL)
    ring(8, 2, METAL, T_STEEL)
    ring(9, 3, METAL, T_STEEL)
    ring(10, 3, METAL, T_STEEL_EDGE)                     # rim highlight
    m.disc(0, 0, 9, 1, WOOD, "#5a1a20")                  # wine surface
    m.mark_grip(0, 4, 0)
    return m, OUT_ITEMS


def gen_plate():
    # Wooden dinner plate (~24 cm) — wide with concentric shading and a raised
    # rim so it reads as a plate, not a flat slab ("looks like a single voxel"
    # feedback on the first pass: too small + one uniform tint).
    m = Model("plate", "item")
    R = 10
    for x in range(-R, R + 1):
        for z in range(-R, R + 1):
            d2 = x * x + z * z
            if d2 > R * R + 2:
                continue
            if d2 <= 16:
                t = "#e2d3b0"            # bright eating well
            elif d2 <= 49:
                t = "#cdb98e"            # mid ring
            else:
                t = "#a5825a"            # outer band
            m.v(x, 0, z, WOOD, t)
    for x in range(-R, R + 1):
        for z in range(-R, R + 1):
            d2 = x * x + z * z
            if 64 <= d2 <= R * R + 2:
                m.v(x, 1, z, WOOD, "#8a6a44")   # raised rim
    m.mark_grip(0, 0, 0)
    return m, OUT_ITEMS


def gen_bowl():
    # Turned wooden bowl (~12 cm).
    m = Model("bowl", "item")
    m.disc(0, 0, 0, 3, WOOD, T_WOOD_DARK)
    for y in range(1, 4):
        r = 3 + y
        for x in range(-r, r + 1):
            for z in range(-r, r + 1):
                d2 = x * x + z * z
                if (r - 1) * (r - 1) <= d2 <= r * r:
                    m.v(x, y, z, WOOD, T_WOOD_PALE)
    m.mark_grip(0, 1, 0)
    return m, OUT_ITEMS


def gen_jug():
    # Clay jug (~22 cm): bulged body, narrow neck, strap handle.
    m = Model("jug", "item")
    profile = (3, 5, 6, 6, 5, 4, 3, 2, 2, 3)
    for y, r in enumerate(profile):
        m.disc(0, 0, y * 2, r, WOOD, "#a06a3c")
        m.disc(0, 0, y * 2 + 1, r, WOOD, "#a06a3c")
    for y in range(8, 16):
        m.v(7, y, 0, WOOD, "#8a5a30")                   # handle
    m.v(6, 16, 0, WOOD, "#8a5a30")
    m.v(5, 16, 0, WOOD, "#8a5a30")
    m.mark_grip(7, 11, 0)
    return m, OUT_ITEMS


def gen_bottle_wine():
    # Slim green glass bottle (~24 cm) with cork.
    m = Model("bottle_wine", "item")
    for y in range(0, 12):
        m.disc(0, 0, y, 2, GLASS, "#2a5a30")
    for y in range(12, 18):
        m.column(0, 0, y, y, 1, GLASS, "#2a5a30")
    m.column(0, 0, 18, 19, 1, WOOD, T_WOOD_PALE)        # cork
    m.mark_grip(0, 8, 0)
    return m, OUT_ITEMS


def gen_fork():
    m = Model("fork", "item")
    for y in range(0, 9):
        m.v(0, y, 0, METAL, T_STEEL)                    # handle
    m.fill(-1, 1, 9, 9, 0, 0, METAL, T_STEEL)           # bridge
    for x in (-1, 0, 1):
        m.fill(x, x, 10, 12, 0, 0, METAL, T_STEEL_EDGE) # tines
    m.mark_grip(0, 3, 0)
    return m, OUT_ITEMS


def gen_knife_table():
    m = Model("knife_table", "item")
    for y in range(0, 7):
        m.v(0, y, 0, WOOD, T_WOOD_DARK)                 # handle
    for y in range(7, 15):
        m.v(0, y, 0, METAL, T_STEEL)
        if y < 13:
            m.v(1, y, 0, METAL, T_STEEL_EDGE)           # edge side
    m.mark_grip(0, 3, 0)
    return m, OUT_ITEMS


def gen_spoon():
    m = Model("spoon", "item")
    for y in range(0, 9):
        m.v(0, y, 0, METAL, T_STEEL)
    m.disc(0, 0, 9, 2, METAL, T_STEEL)                  # bowl (flat oval)
    m.disc(0, 0, 10, 2, METAL, T_STEEL_DARK)            # bowl hollow shade
    m.mark_grip(0, 3, 0)
    return m, OUT_ITEMS


def gen_frying_pan():
    # Iron skillet (~14 cm dish + handle).
    m = Model("frying_pan", "item")
    m.disc(0, 0, 0, 6, METAL, T_IRON_DARK)
    for x in range(-6, 7):
        for z in range(-6, 7):
            d2 = x * x + z * z
            if 25 <= d2 <= 36:
                m.v(x, 1, z, METAL, T_IRON_DARK)        # wall ring
    for i in range(10):
        m.v(7 + i, 1, 0, METAL, T_IRON)                 # handle
    m.mark_grip(13, 1, 0)
    return m, OUT_ITEMS


def gen_ladle():
    m = Model("ladle", "item")
    for y in range(0, 14):
        m.v(0, y, 0, METAL, T_IRON)                     # handle
    m.v(0, 14, 0, METAL, T_IRON)
    m.disc(0, 0, 0, 2, METAL, T_IRON_DARK)              # cup bottom (at base)
    for x in range(-3, 4):
        for z in range(-3, 4):
            d2 = x * x + z * z
            if 4 <= d2 <= 9:
                m.v(x, 1, z, METAL, T_IRON_DARK)        # cup wall
    m.mark_grip(0, 10, 0)
    return m, OUT_ITEMS


# --------------------------------------------------------------------------
# Ranged weapons (models + held data; firing mechanics are combat-system work)
# --------------------------------------------------------------------------

def gen_bow():
    # Longbow (~1.4 u): curved limbs, wrapped grip, string.
    m = Model("bow", "item")
    H = 113
    mid = H // 2
    for y in range(H):
        t = (y - mid) / float(mid)                      # -1..1 along the stave
        x = int(round(9 * (1.0 - t * t)))               # parabolic curve
        thick = 1 if abs(t) > 0.75 else 2
        for w in range(thick):
            m.v(x + w, y, 0, WOOD, T_WOOD_DARK if abs(t) < 0.15 else T_WOOD_PALE)
        m.v(0, y, 0, WOOD, "#d8cfc0") if x > 0 else None  # string plane
    for y in range(mid - 6, mid + 7):                   # grip wrap
        m.v(9, y, 0, WOOD, T_LEATHER)
        m.v(10, y, 0, WOOD, T_LEATHER)
    m.mark_grip(9, mid, 0)
    return m, OUT_WEAPONS


def gen_crossbow():
    # Crossbow (~0.6 u stock): wood stock, steel prod (bow) across the front,
    # string, trigger lump. Held pointing up like other items; the attack
    # anim orients it.
    m = Model("crossbow", "item")
    m.fill(-1, 1, 0, 44, -1, 1, WOOD, T_WOOD_DARK)      # stock (vertical)
    m.fill(-1, 1, 12, 14, -3, -2, WOOD, T_WOOD_DARK)    # trigger lump
    for i in range(16):                                  # prod: curved steel
        drop = int(round(3 * (i / 15.0) ** 2))
        for s in (1, -1):
            m.v(s * (2 + i), 42 - drop, 0, METAL, T_STEEL)
    m.fill(-17, 17, 39, 39, 1, 1, WOOD, "#d8cfc0")      # string
    m.fill(-1, 1, 36, 44, 0, 1, WOOD, T_WOOD_PALE)      # arrow groove
    m.mark_grip(0, 10, 0)
    return m, OUT_WEAPONS


def gen_quiver():
    # Leather quiver (~0.45 u) with arrow shafts poking out.
    m = Model("quiver", "item")
    for y in range(0, 28):
        m.disc(0, 0, y, 3, WOOD, T_LEATHER if y % 7 else T_LEATHER_DARK)
    for (ax, az) in ((-1, -1), (1, 0), (0, 1)):
        m.fill(ax, ax, 28, 34, az, az, WOOD, T_WOOD_PALE)   # shafts
        m.v(ax, 35, az, WOOD, "#d0d0d0")                    # fletching hint
    m.mark_grip(0, 14, 0)
    return m, OUT_WEAPONS


def gen_throwing_knife():
    m = Model("throwing_knife", "item")
    for y in range(0, 5):
        m.v(0, y, 0, METAL, T_IRON_DARK)                # skeletal handle
    for y in range(5, 13):
        m.v(0, y, 0, METAL, T_STEEL)
        if y < 11:
            m.v(1, y, 0, METAL, T_STEEL_EDGE)
    m.mark_grip(0, 2, 0)
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
    # Slender brass candlestick (~22 cm): shaded tapering base, thin stem with
    # a knop, drip pan, dripping candle. Radial tint shading fakes roundness —
    # at 1.2 cm cells a flat gold disc reads as a blocky slab.
    m = Model("candlestick", "item")
    def shaded_disc(y, r):
        for x in range(-r, r + 1):
            for z in range(-r, r + 1):
                d2 = x * x + z * z
                if d2 <= r * r + 1:
                    t = "#f0cc60" if d2 <= (r - 1) * (r - 1) else "#a87f2c"
                    m.v(x, y, z, GOLD, t)
    shaded_disc(0, 3)
    shaded_disc(1, 2)
    m.v(0, 2, 0, GOLD, "#d8b448")
    for y in range(3, 9):                               # 1-cell stem
        m.v(0, y, 0, GOLD, "#e2be52" if y != 5 else "#f0cc60")
    m.fill(-1, 1, 5, 5, -1, 1, GOLD, "#c89c38")         # knop
    shaded_disc(9, 2)                                    # drip pan
    for y in range(10, 16):                              # candle, 2-cell
        m.fill(0, 1, y, y, 0, 1, WOOD, T_CANDLE)
    m.v(0, 12, -1, WOOD, "#fff8ea")                      # wax drip
    m.v(0, 11, -1, WOOD, "#fff8ea")
    m.v(0, 16, 0, GLOW, "#ffdf90")                       # flame
    m.v(0, 17, 0, GLOW, "#ffb050")
    m.mark_grip(0, 4, 0)
    return m, OUT_ITEMS


def gen_pickaxe():
    # In-place remodel of the last legacy full-cube tool (the CLAUDE.md
    # anti-pattern file). One-hand mining pick: wood haft, steel head with
    # two down-curved tapering picks.
    m = Model("pickaxe", "item")
    m.column(0, 0, 0, 1, 1, WOOD, T_WOOD_DARK)
    wrapped_grip(m, 2, 13, half=1, step=3)
    m.mark_grip(0, 8, 0)
    m.column(0, 0, 14, 42, 1, WOOD, T_WOOD_PALE)
    m.fill(-2, 1, 38, 43, -1, 1, METAL, T_IRON_DARK)   # eye block
    for side in (1, -1):
        for i in range(14):
            x = side * (2 + i)
            f = i / 13.0
            drop = int(round(4 * f * f))               # picks curve downward
            w = max(0, 2 - int(round(2 * f)))
            tint = T_STEEL_EDGE if i > 11 else T_STEEL
            m.fill(x, x, 40 - drop - w, 41 - drop, 0, 0, METAL, tint)
    return m, OUT_WEAPONS


def gen_torch():
    # In-place remodel of items/torch.voxel: wooden stave, cloth-wrapped head,
    # glowing ember crown. The flame VFX/light anchor (items.json) sits just
    # above the head — recorded in the manifest as flame_anchor_units.
    m = Model("torch", "item")
    m.column(0, 0, 0, 21, 1, WOOD, T_WOOD_DARK)
    m.mark_grip(0, 8, 0)
    m.fill(-1, 2, 22, 28, -1, 2, WOOD, "#4a3626")      # tar-cloth wrap (4x4)
    m.fill(-1, 2, 29, 30, -1, 2, GLOW, "#ff9a3c")      # ember crown
    m.fill(0, 1, 31, 31, 0, 1, GLOW, "#ffd070")        # bright tip
    return m, OUT_ITEMS


def _billet(name, bark_tint, split_tint, embers):
    """A split firewood billet lying on its side, long axis +Z.

    GROUNDED: split firewood is cut to ~0.33-0.40 m ("stove length", 16 in =
    0.406 m) and split to ~0.10 m across. Here 28 cells long (0.346 m) x 8
    across (0.099 m) — inside that band. A billet is RIVEN, not round: the
    cross-section is a wedge with bark on the outside arc and two flat split
    faces, which is why this is a half-disc and not a cylinder.
    `embers` seeds the burning variant's glow along the underside, where a
    real log burns hottest (the fire is under and between the wood).
    """
    m = Model(name, "item")
    R, LEN = 4, 28
    for z in range(LEN):
        # Half-round: bark arc above, flat riven face at y=0 (it lies split-side down).
        for x in range(-R, R + 1):
            for y in range(0, R + 1):
                if (x * x) / float(R * R) + (y * y) / float(R * R) > 1.001:
                    continue
                bark = (x * x + y * y) >= (R - 1) * (R - 1)      # outer arc = bark
                m.v(x, y, z, WOOD, bark_tint if bark else split_tint)
        # End grain: the cut faces show pale rings, not bark.
        if z == 0 or z == LEN - 1:
            for x in range(-R + 1, R):
                for y in range(0, R):
                    if (x * x) / float(R * R) + (y * y) / float(R * R) <= 1.001:
                        m.v(x, y, z, WOOD, split_tint)
    if embers:
        # Embers in the underside crevice + a few breaking through the char.
        for z in range(2, LEN - 2, 3):
            m.v(-1, 0, z, GLOW, T_EMBER)
            m.v(1, 0, z, GLOW, T_EMBER)
        for z in range(4, LEN - 4, 7):
            m.v(0, 1, z, GLOW, "#ffb050")
    m.mark_grip(0, 2, LEN // 2)
    return m, OUT_ITEMS


def gen_firewood():
    # A plain split billet — hearth fuel, stacked but not lit.
    return _billet("firewood", T_BARK, T_SPLIT, embers=False)


def gen_flaming_log():
    # The burning billet at the heart of the pile: charred outside, embers in
    # the crevices. Its FLAME (particles) and FIRELIGHT are declarative item
    # effects in items.json — the same mechanism the torch uses — so a lit
    # hearth relights itself when the world reloads.
    return _billet("flaming_log", T_CHAR, "#6b4a2c", embers=True)


def _school_tome(name, cover, spine, surface_tex):
    # Thin lying tome (5 cm) with the school's cover art projected across the
    # top/bottom faces. Replaces the legacy subcube bricks ("too thick").
    m = Model(name, "item")
    m.surface = (surface_tex, "y")
    W, D, H = 19, 24, 4
    m.fill(1, W - 2, 1, H - 2, 1, D - 2, WOOD, T_PAPER_EDGE)   # page block
    m.fill(0, W - 1, 0, 0, 0, D - 1, WOOD, cover)              # bottom cover
    m.fill(0, W - 1, H - 1, H - 1, 0, D - 1, WOOD, cover)      # top cover
    m.fill(0, 0, 0, H - 1, 0, D - 1, WOOD, spine)              # spine
    m.fill(W - 1, W - 1, 1, H - 2, D // 2 - 1, D // 2 + 1, GOLD, None)  # clasp
    return m, OUT_ITEMS


def gen_tome_arcane():
    return _school_tome("tome_arcane", "#3a2a5a", "#2a1e42", "tome_arcane")


def gen_tome_fire():
    return _school_tome("tome_fire", "#6a2418", "#521a10", "tome_fire")


def gen_tome_necromantic():
    return _school_tome("tome_necromantic", "#28302a", "#1a201c", "tome_necromantic")


def _flat_projected(name, tex, axis, w_cells, h_cells, out_dir, grid=27,
                    rim=None):
    # Shared builder for thin projected-surface boards: rugs (axis=y, lying)
    # and signs (axis=z, standing). ONE cell thick on the projection axis.
    m = Model(name, "item", grid=grid)
    m.surface = (tex, axis)
    for a in range(w_cells):
        for b in range(h_cells):
            edge = a == 0 or b == 0 or a == w_cells - 1 or b == h_cells - 1
            tint = (rim if (rim and edge) else "#8a6a4a")
            if axis == "y":
                m.v(a, 0, b, WOOD, tint)
            else:
                m.v(a, b, 0, WOOD, tint)
    return m, out_dir


def gen_rug_oriental():
    # In-place remodel: was 1/3-cube thick; now one 27-cell (3.7 cm).
    return _flat_projected("rug_oriental", "rug_oriental", "y", 54, 54,
                           OUT_ITEMS, rim="#c9b18a")


def gen_rug_test():
    return _flat_projected("rug_test", "surface_test", "y", 40, 40,
                           OUT_TEST, rim="#c9b18a")


def gen_sign_prancing_pony():
    # In-place remodel: hanging tavern sign board, one cell thick.
    return _flat_projected("sign_prancing_pony", "sign_prancing_pony", "z",
                           54, 40, OUT_ITEMS, rim="#5a4632")


def _trade_sign(name):
    # Default trade-sign boards (CityForgePlan M3c): square art (gen_trade_signs.py),
    # so the board is square too — 40x40 cells (~1.48 m), inside the medieval
    # projection limit the tavern board grounded (1375 ale-stake ordinance).
    return _flat_projected(name, name, "z", 40, 40, OUT_ITEMS, rim="#5a4632")


def gen_sign_blacksmith():
    return _trade_sign("sign_blacksmith")


def gen_sign_bakery():
    return _trade_sign("sign_bakery")


def gen_sign_general_store():
    return _trade_sign("sign_general_store")


def gen_sign_apothecary():
    return _trade_sign("sign_apothecary")


def gen_sign_butcher():
    return _trade_sign("sign_butcher")


def gen_rug_woven():
    # Loose furnishing, NOT baked microcubes: a 1.5 x 2.0 u woven rug, ONE cell
    # thick (grid 27 -> 3.7 cm), with the rug_oriental image projected across
    # the top/bottom via the planar-surface path. Physics: broad + flat, so it
    # spawns lying and settles instantly; walk-through scoots it.
    m = Model("rug_woven", "item", grid=27)
    m.surface = ("rug_oriental", "y")
    W, D = 40, 54
    for x in range(W):
        for z in range(D):
            edge = x == 0 or z == 0 or x == W - 1 or z == D - 1
            m.v(x, 0, z, WOOD, "#c9b18a" if edge else "#8a4a30")  # fringe rim
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
    gen_rug_woven,
    # Lighting set
    gen_lantern, gen_oil_lamp, gen_candle,
    # Hearth fuel (structure gen dresses fireboxes with these)
    gen_firewood, gen_flaming_log,
    # Tavern tableware
    gen_tankard, gen_goblet, gen_plate, gen_bowl, gen_jug, gen_bottle_wine,
    gen_fork, gen_knife_table, gen_spoon, gen_frying_pan, gen_ladle,
    # Ranged weapons (models; firing = combat-system follow-up)
    gen_bow, gen_crossbow, gen_quiver, gen_throwing_knife,
    # In-place remodels of legacy assets (feedback 2026-08-06: "too thick" /
    # blocky): overwrite the original files so every reference keeps working.
    gen_pickaxe, gen_torch,
    gen_tome_arcane, gen_tome_fire, gen_tome_necromantic,
    gen_rug_oriental, gen_rug_test, gen_sign_prancing_pony,
    gen_sign_blacksmith, gen_sign_bakery, gen_sign_general_store,
    gen_sign_apothecary, gen_sign_butcher,
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
