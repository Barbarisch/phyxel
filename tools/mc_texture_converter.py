#!/usr/bin/env python3
"""
Minecraft Texture Pack Converter for Phyxel Engine
====================================================
Extracts and converts block textures from Minecraft resource packs to
the Phyxel atlas format (64x64 RGBA PNG per face, 6 faces per material).

Usage:
  python tools/mc_texture_converter.py scan <pack_dir>
  python tools/mc_texture_converter.py convert [options]
  python tools/mc_texture_converter.py info [options]
"""

import os
import sys
import json
import shutil
import subprocess
import argparse
from pathlib import Path
from PIL import Image

PHYXEL_ROOT = Path(__file__).parent.parent
MATERIALS_JSON = PHYXEL_ROOT / "resources" / "materials.json"
DEFAULT_CONFIG = PHYXEL_ROOT / "resources" / "mc_texture_map.json"
DEFAULT_OUTPUT = PHYXEL_ROOT / "resources" / "textures" / "source"
DEFAULT_TEXTURE_SIZE = 64
FACE_NAMES = ["side_n", "side_s", "side_e", "side_w", "top", "bottom"]

# Bidirectional pre-1.13 <-> post-1.13 rename table.
# Keys are pre-1.13 names, values are post-1.13 names.
PRE_TO_POST = {
    "planks_oak":         "oak_planks",
    "planks_birch":       "birch_planks",
    "planks_spruce":      "spruce_planks",
    "planks_jungle":      "jungle_planks",
    "planks_big_oak":     "dark_oak_planks",
    "planks_acacia":      "acacia_planks",
    "log_oak":            "oak_log",
    "log_oak_top":        "oak_log_top",
    "log_birch":          "birch_log",
    "log_birch_top":      "birch_log_top",
    "log_spruce":         "spruce_log",
    "log_spruce_top":     "spruce_log_top",
    "log_jungle":         "jungle_log",
    "log_jungle_top":     "jungle_log_top",
    "log_big_oak":        "dark_oak_log",
    "log_acacia":         "acacia_log",
    "leaves_oak":         "oak_leaves",
    "leaves_birch":       "birch_leaves",
    "leaves_spruce":      "spruce_leaves",
    "leaves_jungle":      "jungle_leaves",
    "leaves_big_oak":     "dark_oak_leaves",
    "leaves_acacia":      "acacia_leaves",
    "grass_top":          "grass_block_top",
    "grass_side":         "grass_block_side",
    "stonebrick":         "stone_bricks",
    "stonebrick_mossy":   "mossy_stone_bricks",
    "stonebrick_cracked": "cracked_stone_bricks",
    "stonebrick_carved":  "chiseled_stone_bricks",
    "cobblestone_mossy":  "mossy_cobblestone",
    "brick":              "bricks",
    "sandstone_normal":   "sandstone",
    "sandstone_smooth":   "smooth_sandstone",
    "sandstone_carved":   "chiseled_sandstone",
    "hardened_clay":      "terracotta",
    "wool_colored_white": "white_wool",
    "wool_colored_black": "black_wool",
    "dirt_podzol_top":    "podzol_top",
    "dirt_podzol_side":   "podzol_side",
    "nether_brick":       "nether_bricks",
    "red_sand":           "red_sand",
}
POST_TO_PRE = {v: k for k, v in PRE_TO_POST.items()}


class MinecraftPackReader:
    """Reads a Minecraft resource pack and resolves block texture paths."""

    def __init__(self, pack_dir: Path):
        self.pack_dir = Path(pack_dir)
        self.pack_format = self._read_pack_format()
        self.is_pre_113 = self.pack_format < 4
        # Prefer the canonical directory for this format; fall back to the other
        if self.is_pre_113:
            primary = self.pack_dir / "assets" / "minecraft" / "textures" / "blocks"
            fallback = self.pack_dir / "assets" / "minecraft" / "textures" / "block"
        else:
            primary = self.pack_dir / "assets" / "minecraft" / "textures" / "block"
            fallback = self.pack_dir / "assets" / "minecraft" / "textures" / "blocks"
        self.texture_dir = primary if primary.exists() else fallback

    def _read_pack_format(self) -> int:
        mcmeta = self.pack_dir / "pack.mcmeta"
        if mcmeta.exists():
            try:
                with open(mcmeta, encoding="utf-8") as f:
                    data = json.load(f)
                return data.get("pack", {}).get("pack_format", 2)
            except Exception:
                pass
        return 2

    def list_textures(self) -> list:
        """List all block texture base names (no extension, no PBR _n/_s/_e variants)."""
        if not self.texture_dir.exists():
            return []
        result = []
        for f in sorted(self.texture_dir.iterdir()):
            if f.suffix.lower() == ".png":
                stem = f.stem
                # Skip PBR normal/specular/emissive maps
                if not (stem.endswith("_n") or stem.endswith("_s") or stem.endswith("_e")):
                    result.append(stem)
        return result

    def get_texture_path(self, block_name: str) -> Path:
        """Resolve a block name to a PNG path, trying naming variants. Returns None if not found."""
        direct = self.texture_dir / f"{block_name}.png"
        if direct.exists():
            return direct

        # Try the alternate naming convention (pre <-> post 1.13)
        if self.is_pre_113:
            alt = POST_TO_PRE.get(block_name)
        else:
            alt = PRE_TO_POST.get(block_name)

        if alt:
            p = self.texture_dir / f"{alt}.png"
            if p.exists():
                return p

        return None

    def get_texture_size(self):
        """Estimate the pack's native texture resolution from common blocks."""
        for name in ["stone", "dirt", "planks_oak", "oak_planks", "iron_block"]:
            p = self.texture_dir / f"{name}.png"
            if p.exists():
                try:
                    with Image.open(p) as img:
                        return img.size
                except Exception:
                    pass
        return None


class TextureConverter:
    """Converts images to the Phyxel target format (RGBA, fixed size)."""

    FILTER_MAP = {
        "lanczos":  Image.LANCZOS,
        "nearest":  Image.NEAREST,
        "bilinear": Image.BILINEAR,
    }

    def __init__(self, target_size: int = DEFAULT_TEXTURE_SIZE, resize_filter: str = "lanczos"):
        self.target_size = target_size
        self.filter = self.FILTER_MAP.get(resize_filter, Image.LANCZOS)

    def convert(self, src_path: Path) -> Image.Image:
        """Load a PNG, convert to RGBA, and resize to target_size×target_size."""
        img = Image.open(src_path).convert("RGBA")
        # MC animated textures are tall strips (Nx16 or Nx32); use only the first frame
        w, h = img.size
        if h > w and h % w == 0:
            img = img.crop((0, 0, w, w))
        if img.size != (self.target_size, self.target_size):
            img = img.resize((self.target_size, self.target_size), self.filter)
        return img

    def generate_checkerboard(self) -> Image.Image:
        """Magenta/black checkerboard — used for the Default error material."""
        size = self.target_size
        img = Image.new("RGBA", (size, size), (0, 0, 0, 255))
        pixels = img.load()
        tile = max(size // 8, 1)
        for y in range(size):
            for x in range(size):
                if ((x // tile) + (y // tile)) % 2 == 0:
                    pixels[x, y] = (255, 0, 255, 255)
                else:
                    pixels[x, y] = (20, 20, 20, 255)
        return img


class TextureMapper:
    """Resolves Phyxel material+face pairs to MC texture source paths."""

    def __init__(self, config_path: Path, pack_readers: dict):
        self.config_path = config_path
        self.packs = pack_readers
        with open(config_path, encoding="utf-8") as f:
            self.config = json.load(f)

    def resolve(self, material_name: str, face: str):
        """
        Returns (pack_name, texture_path) or one of these sentinel pairs:
          ('_generated', None)  — programmatically generated texture
          (None, None)          — no mapping found
        """
        mat_cfg = self.config.get("materials", {}).get(material_name)
        if mat_cfg is None:
            return None, None

        source = mat_cfg.get("source", "mc")
        if source == "generated":
            return "_generated", None

        pack_name = mat_cfg.get("pack", self.config.get("default_pack"))
        faces_cfg = mat_cfg.get("faces", {})

        # Priority: explicit face key > "all" key
        face_entry = faces_cfg.get(face) or faces_cfg.get("all")
        if face_entry is None:
            return None, None

        # Per-face pack override: {"pack": "rtxbeta", "block": "iron_block"}
        if isinstance(face_entry, dict):
            pack_name = face_entry.get("pack", pack_name)
            block_name = face_entry.get("block")
        else:
            block_name = face_entry

        if pack_name not in self.packs:
            return None, None

        tex_path = self.packs[pack_name].get_texture_path(block_name)
        return pack_name, tex_path


def load_phyxel_materials(materials_json: Path = MATERIALS_JSON) -> list:
    """Return ordered list of material names from materials.json."""
    with open(materials_json, encoding="utf-8") as f:
        data = json.load(f)
    return [m["name"] for m in data.get("materials", [])]


# ---------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------

def cmd_scan(args):
    pack_dir = Path(args.pack_dir)
    if not pack_dir.exists():
        print(f"ERROR: Pack directory not found: {pack_dir}")
        sys.exit(1)

    reader = MinecraftPackReader(pack_dir)
    era = "pre-1.13" if reader.is_pre_113 else "post-1.13"
    print(f"Pack: {pack_dir.name}")
    print(f"  pack_format : {reader.pack_format} ({era})")
    print(f"  texture dir : {reader.texture_dir}")

    size = reader.get_texture_size()
    if size:
        print(f"  native size : {size[0]}×{size[1]} px")

    textures = reader.list_textures()
    print(f"\n  {len(textures)} block textures found:")
    for t in textures:
        print(f"    {t}")

    # Show which Phyxel materials could be satisfied
    if MATERIALS_JSON.exists():
        config_path = Path(args.config) if hasattr(args, "config") and args.config else DEFAULT_CONFIG
        if config_path.exists():
            with open(config_path, encoding="utf-8") as f:
                cfg = json.load(f)
            print("\n  Phyxel material coverage (from mapping config):")
            for mat, mat_cfg in cfg.get("materials", {}).items():
                if mat_cfg.get("source") == "generated":
                    print(f"    {mat:<16} [generated]")
                    continue
                p_name = mat_cfg.get("pack", cfg.get("default_pack"))
                faces_cfg = mat_cfg.get("faces", {})
                missing = []
                for face in FACE_NAMES:
                    entry = faces_cfg.get(face) or faces_cfg.get("all")
                    if entry is None:
                        missing.append(face)
                        continue
                    if isinstance(entry, dict):
                        p_name2 = entry.get("pack", p_name)
                        block = entry.get("block")
                    else:
                        p_name2, block = p_name, entry
                    if p_name2 == pack_dir.name or p_name2 == pack_dir.stem:
                        resolved = reader.get_texture_path(block)
                        if resolved is None:
                            missing.append(f"{face}({block})")
                status = "OK" if not missing else f"MISSING: {', '.join(missing)}"
                print(f"    {mat:<16} {status}")


def cmd_info(args):
    config_path = Path(args.config)
    if not config_path.exists():
        print(f"ERROR: Config not found: {config_path}")
        sys.exit(1)

    with open(config_path, encoding="utf-8") as f:
        cfg = json.load(f)

    pack_readers = {}
    for name, path in cfg.get("packs", {}).items():
        p = Path(path)
        if p.exists():
            pack_readers[name] = MinecraftPackReader(p)
        else:
            print(f"WARNING: Pack '{name}' not found at {path}")

    materials = load_phyxel_materials()
    mapper = TextureMapper(config_path, pack_readers)

    print(f"{'Material':<16} {'Face':<10} {'Pack':<12} Source file")
    print("─" * 72)
    for mat in materials:
        for face in FACE_NAMES:
            pack_name, tex_path = mapper.resolve(mat, face)
            if pack_name == "_generated":
                print(f"  {mat:<14} {face:<10} [generated]")
            elif tex_path:
                print(f"  {mat:<14} {face:<10} {pack_name:<12} {tex_path.name}")
            else:
                print(f"  {mat:<14} {face:<10} [MISSING]")


def cmd_preview(args):
    """Extract a specific pack's texture for a material+face to an output PNG.

    If --pack is omitted, writes all known packs side-by-side as
    <output_stem>_<packname>.png so you can compare them in any viewer.
    """
    config_path = Path(args.config)
    if not config_path.exists():
        print(f"ERROR: Config not found: {config_path}")
        sys.exit(1)

    with open(config_path, encoding="utf-8") as f:
        cfg = json.load(f)

    pack_readers = {}
    for name, path in cfg.get("packs", {}).items():
        p = Path(path)
        if p.exists():
            pack_readers[name] = MinecraftPackReader(p)
        else:
            print(f"WARNING: Pack '{name}' not found at {path}")

    target_size = getattr(args, "texture_size", DEFAULT_TEXTURE_SIZE)
    converter = TextureConverter(target_size, "lanczos")

    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    packs_to_try = [args.pack] if args.pack else list(pack_readers.keys())

    mat_cfg = cfg.get("materials", {}).get(args.material)
    if mat_cfg is None:
        print(f"ERROR: Material '{args.material}' not in config")
        sys.exit(1)

    if mat_cfg.get("source") == "generated":
        print(f"  Material '{args.material}' uses a generated texture — no pack source")
        sys.exit(1)

    faces_cfg = mat_cfg.get("faces", {})
    face_entry = faces_cfg.get(args.face) or faces_cfg.get("all")
    if face_entry is None:
        print(f"  [MISSING]  No mapping for {args.material}/{args.face}")
        sys.exit(1)

    if isinstance(face_entry, dict):
        block_name = face_entry.get("block")
    else:
        block_name = face_entry

    wrote = 0
    for pack_name in packs_to_try:
        if pack_name not in pack_readers:
            print(f"  [SKIP]   '{pack_name}' pack not loaded (check path in mc_texture_map.json)")
            continue

        tex_path = pack_readers[pack_name].get_texture_path(block_name)
        if tex_path is None:
            print(f"  [MISSING]  {pack_name}: '{block_name}.png' not found in pack")
            continue

        try:
            img = converter.convert(tex_path)
            if len(packs_to_try) == 1:
                out = output_path
            else:
                out = output_path.parent / f"{output_path.stem}_{pack_name}{output_path.suffix}"
            img.save(out)
            print(f"  [ok]  {pack_name}: {tex_path.name} -> {out.name}")
            wrote += 1
        except Exception as exc:
            print(f"  [ERROR]  {pack_name}: {exc}")

    if wrote == 0:
        sys.exit(1)


def cmd_convert(args):
    config_path = Path(args.config)
    output_dir = Path(args.output_dir)

    if not config_path.exists():
        print(f"ERROR: Config not found: {config_path}")
        sys.exit(1)

    output_dir.mkdir(parents=True, exist_ok=True)

    with open(config_path, encoding="utf-8") as f:
        cfg = json.load(f)

    target_size = args.texture_size
    pack_readers = {}
    for name, path in cfg.get("packs", {}).items():
        p = Path(path)
        if p.exists():
            pack_readers[name] = MinecraftPackReader(p)
        else:
            print(f"WARNING: Pack '{name}' not found at {path}")

    materials = load_phyxel_materials()
    if args.materials:
        filter_set = {m.strip() for m in args.materials.split(",")}
        materials = [m for m in materials if m in filter_set]
        if not materials:
            print(f"ERROR: None of the requested materials found in materials.json")
            sys.exit(1)

    mapper = TextureMapper(config_path, pack_readers)
    converter = TextureConverter(target_size, args.resize_filter)

    ok = 0
    missing = 0

    for mat in materials:
        mat_lower = mat.lower()
        for face in FACE_NAMES:
            out_path = output_dir / f"{mat_lower}_{face}.png"
            pack_name, tex_path = mapper.resolve(mat, face)

            if pack_name == "_generated":
                if not args.dry_run:
                    img = converter.generate_checkerboard()
                    if args.backup and out_path.exists():
                        shutil.copy2(out_path, str(out_path) + ".bak")
                    img.save(out_path)
                label = "[generated]"
                print(f"  {label:<13} {mat_lower}_{face}.png")
                ok += 1
                continue

            if tex_path is None:
                src_hint = ""
                mat_cfg = cfg.get("materials", {}).get(mat, {})
                face_entry = mat_cfg.get("faces", {}).get(face) or mat_cfg.get("faces", {}).get("all")
                if face_entry:
                    block = face_entry if isinstance(face_entry, str) else face_entry.get("block", "?")
                    src_hint = f"  (wanted: {block})"
                print(f"  [MISSING]     {mat_lower}_{face}.png{src_hint}")
                missing += 1
                continue

            if args.dry_run:
                print(f"  [dry-run]     {mat_lower}_{face}.png  <- {pack_name}:{tex_path.name}")
                ok += 1
                continue

            try:
                img = converter.convert(tex_path)
                if args.backup and out_path.exists():
                    shutil.copy2(out_path, str(out_path) + ".bak")
                img.save(out_path)
                print(f"  [ok]          {mat_lower}_{face}.png  <- {pack_name}:{tex_path.name}")
                ok += 1
            except Exception as exc:
                print(f"  [ERROR]       {mat_lower}_{face}.png  {exc}")
                missing += 1

    print(f"\n  Done: {ok} converted, {missing} missing/failed")

    if args.rebuild_atlas and not args.dry_run:
        atlas_builder = PHYXEL_ROOT / "tools" / "texture_atlas_builder.py"
        if not atlas_builder.exists():
            print("WARNING: texture_atlas_builder.py not found — skipping atlas rebuild")
            return
        cmd = [
            sys.executable, str(atlas_builder),
            str(output_dir), "cube_atlas.png",
            "--texture-size", str(target_size),
            "--padding", "1",
            "--textures-per-row", "6",
        ]
        print(f"\nRebuilding atlas...")
        try:
            subprocess.run(cmd, cwd=str(PHYXEL_ROOT), check=True)
            print("Atlas rebuilt successfully.")
        except subprocess.CalledProcessError as e:
            print(f"ERROR: Atlas rebuild failed: {e}")
            sys.exit(1)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Minecraft → Phyxel texture converter",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Inspect a texture pack
  python tools/mc_texture_converter.py scan C:/Users/jack/Downloads/test_textures/realistico

  # Preview what would be converted (no writes)
  python tools/mc_texture_converter.py convert --dry-run

  # Convert Stone and Wood only, with backup
  python tools/mc_texture_converter.py convert --materials Stone,Wood --backup

  # Full conversion + atlas rebuild
  python tools/mc_texture_converter.py convert --rebuild-atlas

  # Show the full resolved mapping table
  python tools/mc_texture_converter.py info
""",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    # ---- scan ----
    p_scan = sub.add_parser("scan", help="Inspect a texture pack")
    p_scan.add_argument("pack_dir", help="Path to the texture pack root directory")
    p_scan.add_argument("--config", default=str(DEFAULT_CONFIG),
                        help="Mapping config for coverage report (default: resources/mc_texture_map.json)")

    # ---- info ----
    p_info = sub.add_parser("info", help="Show the fully resolved mapping table")
    p_info.add_argument("--config", default=str(DEFAULT_CONFIG))

    # ---- preview ----
    p_prev = sub.add_parser("preview", help="Extract one pack's texture for a material+face (for comparison)")
    p_prev.add_argument("material", help="Phyxel material name, e.g. Stone")
    p_prev.add_argument("face", choices=FACE_NAMES, help="Face name, e.g. side_n")
    p_prev.add_argument("--pack", default="",
                        help="Pack name to extract from (default: all registered packs)")
    p_prev.add_argument("--output", default="resources/textures/preview_temp.png",
                        help="Output PNG path (default: resources/textures/preview_temp.png)")
    p_prev.add_argument("--config", default=str(DEFAULT_CONFIG))
    p_prev.add_argument("--texture-size", type=int, default=DEFAULT_TEXTURE_SIZE)

    # ---- convert ----
    p_conv = sub.add_parser("convert", help="Convert textures from MC packs to Phyxel format")
    p_conv.add_argument("--config", default=str(DEFAULT_CONFIG),
                        help="Mapping config (default: resources/mc_texture_map.json)")
    p_conv.add_argument("--output-dir", default=str(DEFAULT_OUTPUT),
                        help="Output directory for PNGs (default: resources/textures/source/)")
    p_conv.add_argument("--materials",
                        help="Comma-separated list of material names to convert (default: all)")
    p_conv.add_argument("--resize-filter", choices=["lanczos", "nearest", "bilinear"],
                        default="lanczos", help="Downscale filter (default: lanczos)")
    p_conv.add_argument("--texture-size", type=int, default=DEFAULT_TEXTURE_SIZE,
                        help=f"Output texture size in pixels (default: {DEFAULT_TEXTURE_SIZE})")
    p_conv.add_argument("--backup", action="store_true",
                        help="Save .bak copies of existing textures before overwriting")
    p_conv.add_argument("--dry-run", action="store_true",
                        help="Print what would be done without writing any files")
    p_conv.add_argument("--rebuild-atlas", action="store_true",
                        help="Run texture_atlas_builder.py after conversion")

    args = parser.parse_args()

    if args.command == "scan":
        cmd_scan(args)
    elif args.command == "info":
        cmd_info(args)
    elif args.command == "preview":
        cmd_preview(args)
    elif args.command == "convert":
        cmd_convert(args)


if __name__ == "__main__":
    main()
