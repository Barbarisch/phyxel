#!/usr/bin/env python3
"""
Generate procedural 18x18 textures for Phyxel engine materials.

Creates 6-face texture sets (side_n, side_s, side_e, side_w, top, bottom)
for each material that doesn't already have textures.

Materials to generate:
  stone, wood, metal, glass, rubber, ice, cork, glow, default

Style: Simple pixel-art compatible with the existing grassdirt/placeholder textures.
"""

import os
import math
import random
from PIL import Image

SIZE = 18
OUTPUT_DIR = "textures"


def clamp(v, lo=0, hi=255):
    return max(lo, min(hi, int(v)))


def noise_texture(base_color, variation=20, seed=0):
    """Generate a simple noisy texture around a base color."""
    rng = random.Random(seed)
    img = Image.new("RGB", (SIZE, SIZE))
    pixels = img.load()
    for y in range(SIZE):
        for x in range(SIZE):
            r = clamp(base_color[0] + rng.randint(-variation, variation))
            g = clamp(base_color[1] + rng.randint(-variation, variation))
            b = clamp(base_color[2] + rng.randint(-variation, variation))
            pixels[x, y] = (r, g, b)
    return img


def striped_texture(color1, color2, stripe_width=3, horizontal=True, seed=0, variation=10):
    """Generate a striped texture with noise and variable stripe width."""
    rng = random.Random(seed)
    img = Image.new("RGB", (SIZE, SIZE))
    pixels = img.load()
    for y in range(SIZE):
        for x in range(SIZE):
            coord = y if horizontal else x
            in_stripe = (coord // stripe_width) % 2 == 0
            base = color1 if in_stripe else color2
            v = rng.randint(-variation, variation)
            pixels[x, y] = (clamp(base[0]+v), clamp(base[1]+v), clamp(base[2]+v))
    return img


def gradient_texture(top_color, bottom_color, variation=10, seed=0):
    """Generate a vertical gradient with noise."""
    rng = random.Random(seed)
    img = Image.new("RGB", (SIZE, SIZE))
    pixels = img.load()
    for y in range(SIZE):
        t = y / (SIZE - 1)
        for x in range(SIZE):
            r = clamp(top_color[0] * (1-t) + bottom_color[0] * t + rng.randint(-variation, variation))
            g = clamp(top_color[1] * (1-t) + bottom_color[1] * t + rng.randint(-variation, variation))
            b = clamp(top_color[2] * (1-t) + bottom_color[2] * t + rng.randint(-variation, variation))
            pixels[x, y] = (r, g, b)
    return img


def smooth_texture(base_color, variation=8, seed=0):
    """Generate a smooth, slightly varied texture (for ice/glass)."""
    rng = random.Random(seed)
    img = Image.new("RGB", (SIZE, SIZE))
    pixels = img.load()
    for y in range(SIZE):
        for x in range(SIZE):
            v = rng.randint(-variation, variation)
            # Slight shimmer: add small x+y based offset
            shimmer = ((x + y) % 3) * 2 - 2
            r = clamp(base_color[0] + v + shimmer)
            g = clamp(base_color[1] + v + shimmer)
            b = clamp(base_color[2] + v + shimmer)
            pixels[x, y] = (r, g, b)
    return img


def emissive_texture(base_color, glow_center=True, seed=0):
    """Generate a glowing texture — bright center, slightly dimmer edges."""
    rng = random.Random(seed)
    img = Image.new("RGB", (SIZE, SIZE))
    pixels = img.load()
    cx, cy = SIZE // 2, SIZE // 2
    max_dist = ((SIZE//2)**2 + (SIZE//2)**2) ** 0.5
    for y in range(SIZE):
        for x in range(SIZE):
            dist = ((x - cx)**2 + (y - cy)**2) ** 0.5
            if glow_center:
                brightness = 1.0 - (dist / max_dist) * 0.3  # 70-100% brightness
            else:
                brightness = 1.0
            v = rng.randint(-5, 5)
            r = clamp(base_color[0] * brightness + v)
            g = clamp(base_color[1] * brightness + v)
            b = clamp(base_color[2] * brightness + v)
            pixels[x, y] = (r, g, b)
    return img


def stratified_texture(base_color, layer_color, variation=15, layer_spacing=4, seed=0):
    """Generate stone-like texture with subtle horizontal stratification layers."""
    rng = random.Random(seed)
    img = Image.new("RGB", (SIZE, SIZE))
    pixels = img.load()
    for y in range(SIZE):
        is_layer = (y % layer_spacing == 0)
        for x in range(SIZE):
            v = rng.randint(-variation, variation)
            if is_layer:
                base = layer_color
                v = rng.randint(-8, 8)
            else:
                base = base_color
            pixels[x, y] = (clamp(base[0]+v), clamp(base[1]+v), clamp(base[2]+v))
    return img


def growth_ring_texture(base_color, ring_color, variation=12, seed=0):
    """Generate concentric growth ring pattern for wood top face."""
    rng = random.Random(seed)
    img = Image.new("RGB", (SIZE, SIZE))
    pixels = img.load()
    cx, cy = SIZE // 2 + 2, SIZE // 2 - 1  # Off-center for naturalism
    for y in range(SIZE):
        for x in range(SIZE):
            dist = math.sqrt((x - cx)**2 + (y - cy)**2)
            ring = int(dist) % 4  # Ring every 4 pixels
            is_ring = (ring == 0)
            v = rng.randint(-variation, variation)
            base = ring_color if is_ring else base_color
            pixels[x, y] = (clamp(base[0]+v), clamp(base[1]+v), clamp(base[2]+v))
    return img


def brushed_metal_texture(base_color, variation=8, streak_dir='vertical', seed=0):
    """Generate brushed metal with directional streaks."""
    rng = random.Random(seed)
    img = Image.new("RGB", (SIZE, SIZE))
    pixels = img.load()
    # Pre-generate streak brightnesses per row/column
    streak_vals = [rng.randint(-6, 6) for _ in range(SIZE)]
    for y in range(SIZE):
        for x in range(SIZE):
            v = rng.randint(-variation, variation)
            streak = streak_vals[x] if streak_dir == 'vertical' else streak_vals[y]
            shimmer = ((x + y) % 3) * 2 - 2
            r = clamp(base_color[0] + v + streak + shimmer)
            g = clamp(base_color[1] + v + streak + shimmer)
            b = clamp(base_color[2] + v + streak + shimmer)
            pixels[x, y] = (r, g, b)
    return img


def glass_edge_texture(base_color, variation=5, seed=0):
    """Generate glass with bright edge highlights."""
    rng = random.Random(seed)
    img = Image.new("RGB", (SIZE, SIZE))
    pixels = img.load()
    for y in range(SIZE):
        for x in range(SIZE):
            v = rng.randint(-variation, variation)
            # Bright edges: pixels near border get a brightness boost
            edge_dist = min(x, y, SIZE-1-x, SIZE-1-y)
            edge_boost = max(0, (2 - edge_dist) * 12)  # 0-1 pixel from edge
            shimmer = ((x + y) % 3) * 2 - 2
            r = clamp(base_color[0] + v + shimmer + edge_boost)
            g = clamp(base_color[1] + v + shimmer + edge_boost)
            b = clamp(base_color[2] + v + shimmer + edge_boost)
            pixels[x, y] = (r, g, b)
    return img


def dimple_texture(base_color, variation=12, dimple_spacing=5, seed=0):
    """Generate rubber-like texture with subtle dimple pattern."""
    rng = random.Random(seed)
    img = Image.new("RGB", (SIZE, SIZE))
    pixels = img.load()
    for y in range(SIZE):
        for x in range(SIZE):
            v = rng.randint(-variation, variation)
            # Dimple at regular intervals
            is_dimple = (x % dimple_spacing == dimple_spacing // 2 and
                         y % dimple_spacing == dimple_spacing // 2)
            dimple_adj = -15 if is_dimple else 0
            pixels[x, y] = (clamp(base_color[0]+v+dimple_adj),
                            clamp(base_color[1]+v+dimple_adj),
                            clamp(base_color[2]+v+dimple_adj))
    return img


def cracked_ice_texture(base_color, variation=6, seed=0):
    """Generate ice with subtle crystalline crack lines."""
    rng = random.Random(seed)
    img = Image.new("RGB", (SIZE, SIZE))
    pixels = img.load()
    # Pre-generate a few random crack paths
    cracks = set()
    for _ in range(3):
        cx, cy = rng.randint(0, SIZE-1), rng.randint(0, SIZE-1)
        for step in range(rng.randint(4, 10)):
            cracks.add((cx, cy))
            cx += rng.choice([-1, 0, 1])
            cy += rng.choice([-1, 0, 1])
            cx = max(0, min(SIZE-1, cx))
            cy = max(0, min(SIZE-1, cy))
    for y in range(SIZE):
        for x in range(SIZE):
            v = rng.randint(-variation, variation)
            shimmer = ((x + y) % 3) * 2 - 2
            crack_adj = -20 if (x, y) in cracks else 0
            r = clamp(base_color[0] + v + shimmer + crack_adj)
            g = clamp(base_color[1] + v + shimmer + crack_adj)
            b = clamp(base_color[2] + v + shimmer + crack_adj)
            pixels[x, y] = (r, g, b)
    return img


def porous_texture(base_color, variation=22, pore_chance=0.06, seed=0):
    """Generate cork-like texture with random dark pores."""
    rng = random.Random(seed)
    img = Image.new("RGB", (SIZE, SIZE))
    pixels = img.load()
    for y in range(SIZE):
        for x in range(SIZE):
            v = rng.randint(-variation, variation)
            is_pore = rng.random() < pore_chance
            pore_adj = -35 if is_pore else 0
            r = clamp(base_color[0] + v + pore_adj)
            g = clamp(base_color[1] + v + pore_adj)
            b = clamp(base_color[2] + v + pore_adj)
            pixels[x, y] = (r, g, b)
    return img


def brick_texture(mortar_color, brick_color, seed=0):
    """Generate a simple brick/block pattern for city buildings."""
    rng = random.Random(seed)
    img = Image.new("RGB", (SIZE, SIZE))
    pixels = img.load()
    brick_h = 3  # brick height
    brick_w = 6  # brick width
    for y in range(SIZE):
        row = y // brick_h
        offset = (brick_w // 2) if (row % 2 == 1) else 0
        for x in range(SIZE):
            ax = (x + offset) % SIZE
            is_mortar = (y % brick_h == 0) or (ax % brick_w == 0)
            base = mortar_color if is_mortar else brick_color
            v = rng.randint(-8, 8)
            pixels[x, y] = (clamp(base[0]+v), clamp(base[1]+v), clamp(base[2]+v))
    return img


def save_face_set(material_name, textures):
    """Save a dict of {face_name: Image} as material texture files."""
    faces = ["side_n", "side_s", "side_e", "side_w", "top", "bottom"]
    for face in faces:
        img = textures.get(face, textures.get("side", textures.get("all")))
        path = os.path.join(OUTPUT_DIR, f"{material_name}_{face}.png")
        img.save(path)
        print(f"  Created {path}")


def generate_stone():
    """Stone: Gray with stratification layers and increased texture."""
    print("Generating: stone")
    save_face_set("stone", {
        "side_n": stratified_texture((130, 125, 115), (115, 110, 105), variation=20, layer_spacing=4, seed=100),
        "side_s": stratified_texture((130, 125, 115), (115, 110, 105), variation=20, layer_spacing=4, seed=103),
        "side_e": stratified_texture((130, 125, 115), (115, 110, 105), variation=20, layer_spacing=5, seed=104),
        "side_w": stratified_texture((130, 125, 115), (115, 110, 105), variation=20, layer_spacing=5, seed=105),
        "top": noise_texture((145, 140, 130), variation=18, seed=101),
        "bottom": noise_texture((110, 105, 100), variation=18, seed=102),
    })


def generate_wood():
    """Wood: Brown with wider horizontal grain lines, growth rings on top."""
    print("Generating: wood")
    grain_dark = (120, 75, 30)
    grain_light = (160, 110, 50)
    # Side faces: wider horizontal grain for more prominent appearance
    save_face_set("wood", {
        "side_n": striped_texture(grain_dark, grain_light, stripe_width=3, horizontal=True, seed=200, variation=8),
        "side_s": striped_texture(grain_dark, grain_light, stripe_width=3, horizontal=True, seed=201, variation=8),
        "side_e": striped_texture(grain_dark, grain_light, stripe_width=3, horizontal=True, seed=202, variation=8),
        "side_w": striped_texture(grain_dark, grain_light, stripe_width=3, horizontal=True, seed=203, variation=8),
        # Top: concentric growth rings
        "top": growth_ring_texture((140, 95, 40), (110, 65, 20), variation=10, seed=204),
        "bottom": noise_texture((110, 70, 25), variation=15, seed=205),
    })


def generate_metal():
    """Metal: Silver-gray with directional brushed streaks."""
    print("Generating: metal")
    save_face_set("metal", {
        "side_n": brushed_metal_texture((170, 170, 190), variation=8, streak_dir='vertical', seed=300),
        "side_s": brushed_metal_texture((170, 170, 190), variation=8, streak_dir='vertical', seed=301),
        "side_e": brushed_metal_texture((170, 170, 190), variation=8, streak_dir='vertical', seed=302),
        "side_w": brushed_metal_texture((170, 170, 190), variation=8, streak_dir='vertical', seed=303),
        "top": brushed_metal_texture((185, 185, 200), variation=6, streak_dir='horizontal', seed=304),
        "bottom": brushed_metal_texture((155, 155, 170), variation=6, streak_dir='horizontal', seed=305),
    })


def generate_glass():
    """Glass: Light blue-white, smooth with bright edge highlights."""
    print("Generating: glass")
    save_face_set("glass", {
        "side_n": glass_edge_texture((200, 220, 240), variation=4, seed=400),
        "side_s": glass_edge_texture((200, 220, 240), variation=4, seed=401),
        "side_e": glass_edge_texture((200, 220, 240), variation=4, seed=402),
        "side_w": glass_edge_texture((200, 220, 240), variation=4, seed=403),
        "top": smooth_texture((210, 230, 245), variation=3, seed=404),
        "bottom": smooth_texture((190, 210, 230), variation=3, seed=405),
    })


def generate_rubber():
    """Rubber: Dark gray with dimple/bump pattern."""
    print("Generating: rubber")
    save_face_set("rubber", {
        "side_n": dimple_texture((65, 65, 65), variation=12, dimple_spacing=5, seed=500),
        "side_s": dimple_texture((65, 65, 65), variation=12, dimple_spacing=5, seed=501),
        "side_e": dimple_texture((65, 65, 65), variation=12, dimple_spacing=5, seed=502),
        "side_w": dimple_texture((65, 65, 65), variation=12, dimple_spacing=5, seed=503),
        "top": dimple_texture((75, 75, 75), variation=10, dimple_spacing=4, seed=504),
        "bottom": dimple_texture((55, 55, 55), variation=10, dimple_spacing=4, seed=505),
    })


def generate_ice():
    """Ice: Light blue with crystalline crack patterns."""
    print("Generating: ice")
    save_face_set("ice", {
        "side_n": cracked_ice_texture((180, 210, 240), variation=6, seed=600),
        "side_s": cracked_ice_texture((180, 210, 240), variation=6, seed=601),
        "side_e": cracked_ice_texture((180, 210, 240), variation=6, seed=602),
        "side_w": cracked_ice_texture((180, 210, 240), variation=6, seed=603),
        "top": cracked_ice_texture((200, 225, 250), variation=4, seed=604),
        "bottom": smooth_texture((160, 190, 220), variation=5, seed=605),
    })


def generate_cork():
    """Cork: Tan/beige with dark pore spots."""
    print("Generating: cork")
    save_face_set("cork", {
        "side_n": porous_texture((190, 160, 110), variation=22, pore_chance=0.06, seed=700),
        "side_s": porous_texture((190, 160, 110), variation=22, pore_chance=0.06, seed=701),
        "side_e": porous_texture((190, 160, 110), variation=22, pore_chance=0.06, seed=702),
        "side_w": porous_texture((190, 160, 110), variation=22, pore_chance=0.06, seed=703),
        "top": porous_texture((200, 170, 120), variation=18, pore_chance=0.05, seed=704),
        "bottom": porous_texture((175, 145, 100), variation=18, pore_chance=0.05, seed=705),
    })


def generate_glow():
    """Glow: White/yellow emissive, bright center."""
    print("Generating: glow")
    save_face_set("glow", {
        "side_n": emissive_texture((255, 245, 200), glow_center=True, seed=800),
        "side_s": emissive_texture((255, 245, 200), glow_center=True, seed=801),
        "side_e": emissive_texture((255, 245, 200), glow_center=True, seed=802),
        "side_w": emissive_texture((255, 245, 200), glow_center=True, seed=803),
        "top": emissive_texture((255, 250, 220), glow_center=True, seed=804),
        "bottom": emissive_texture((255, 240, 180), glow_center=True, seed=805),
    })


def generate_default():
    """Default: Neutral gray, simple noise — generic building block."""
    print("Generating: default")
    save_face_set("default", {
        "side_n": noise_texture((180, 180, 180), variation=15, seed=900),
        "side_s": noise_texture((180, 180, 180), variation=15, seed=901),
        "side_e": noise_texture((180, 180, 180), variation=15, seed=902),
        "side_w": noise_texture((180, 180, 180), variation=15, seed=903),
        "top": noise_texture((195, 195, 195), variation=12, seed=904),
        "bottom": noise_texture((165, 165, 165), variation=12, seed=905),
    })


def generate_dirt():
    """Dirt: Dark brown earth, noisy with occasional pebble-like spots."""
    print("Generating: dirt")
    save_face_set("dirt", {
        "side_n": noise_texture((120, 85, 55), variation=18, seed=1000),
        "side_s": noise_texture((120, 85, 55), variation=18, seed=1001),
        "side_e": noise_texture((120, 85, 55), variation=18, seed=1002),
        "side_w": noise_texture((120, 85, 55), variation=18, seed=1003),
        "top": noise_texture((130, 92, 58), variation=20, seed=1004),
        "bottom": noise_texture((105, 75, 48), variation=15, seed=1005),
    })


if __name__ == "__main__":
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    print(f"Generating material textures in {OUTPUT_DIR}/")
    print(f"Texture size: {SIZE}x{SIZE}")
    print()

    generate_stone()
    generate_wood()
    generate_metal()
    generate_glass()
    generate_rubber()
    generate_ice()
    generate_cork()
    generate_glow()
    generate_default()
    generate_dirt()

    print()
    print("Done! Now run:")
    print("  python tools/texture_atlas_builder.py textures/ cube_atlas.png")
    print("to rebuild the atlas.")
