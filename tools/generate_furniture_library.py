#!/usr/bin/env python3
"""
Furniture Library Generator

Batch-generates a library of furniture and prop templates using BlockSmith.
Templates are cached — only missing items are generated on each run.

Usage:
    python tools/generate_furniture_library.py                # Generate all missing
    python tools/generate_furniture_library.py --tier 1       # Tier 1 only (essential)
    python tools/generate_furniture_library.py --list         # Show status of all items
    python tools/generate_furniture_library.py --item chair   # Generate one item
    python tools/generate_furniture_library.py --dry-run      # Show what would be generated
"""

import argparse
import json
import os
import subprocess
import sys

# ---------------------------------------------------------------------------
# Furniture definitions: name, prompt, size, material, category, tags
# ---------------------------------------------------------------------------

FURNITURE_LIBRARY = [
    # --- Tier 1: Essential furniture ---
    {
        "name": "chair",
        "prompt": "a simple wooden chair with four legs and a backrest, blocky voxel style",
        "size": 1.5,
        "material": "Wood",
        "category": "seating",
        "tags": ["tavern", "house", "shop", "common"],
        "tier": 1,
    },
    {
        "name": "stool",
        "prompt": "a small round wooden bar stool with three legs, simple blocky style",
        "size": 1.0,
        "material": "Wood",
        "category": "seating",
        "tags": ["tavern", "shop", "common"],
        "tier": 1,
    },
    {
        "name": "table_round",
        "prompt": "a small round wooden table with a single center pedestal leg, tavern style",
        "size": 2.0,
        "material": "Wood",
        "category": "table",
        "tags": ["tavern", "house", "common"],
        "tier": 1,
    },
    {
        "name": "table_long",
        "prompt": "a long rectangular wooden dining table with four sturdy legs, seats 6-8 people",
        "size": 4.0,
        "material": "Wood",
        "category": "table",
        "tags": ["tavern", "house", "feast_hall"],
        "tier": 1,
    },
    {
        "name": "bed_single",
        "prompt": "a single bed with wooden frame, headboard, and mattress, simple medieval style",
        "size": 2.5,
        "material": "Wood",
        "category": "bedroom",
        "tags": ["house", "inn", "common"],
        "tier": 1,
    },
    {
        "name": "bed_double",
        "prompt": "a wide double bed with wooden frame, tall headboard, and thick mattress",
        "size": 3.0,
        "material": "Wood",
        "category": "bedroom",
        "tags": ["house", "inn", "master"],
        "tier": 1,
    },
    {
        "name": "bar_counter",
        "prompt": "a long L-shaped wooden bar counter for a tavern, waist height with a flat top",
        "size": 4.0,
        "material": "Wood",
        "category": "counter",
        "tags": ["tavern"],
        "tier": 1,
    },
    {
        "name": "barrel",
        "prompt": "a wooden storage barrel with metal bands, cylindrical and upright",
        "size": 1.5,
        "material": "Wood",
        "category": "storage",
        "tags": ["tavern", "cellar", "warehouse", "common"],
        "tier": 1,
    },
    {
        "name": "bookshelf",
        "prompt": "a tall wooden bookshelf with multiple shelves full of books, flat against a wall",
        "size": 2.5,
        "material": "Wood",
        "category": "furniture",
        "tags": ["house", "library", "shop", "study"],
        "tier": 1,
    },
    {
        "name": "chest",
        "prompt": "a wooden treasure chest with metal bands and a slightly rounded lid",
        "size": 1.5,
        "material": "Wood",
        "category": "storage",
        "tags": ["house", "dungeon", "common"],
        "tier": 1,
    },
    {
        "name": "fireplace",
        "prompt": "a stone fireplace with a mantle, recessed hearth opening, and chimney bricks above",
        "size": 3.0,
        "material": "Stone",
        "category": "fixture",
        "tags": ["tavern", "house", "common"],
        "tier": 1,
    },
    {
        "name": "torch_wall",
        "prompt": "a wall-mounted torch sconce with a wooden handle and flame holder bracket",
        "size": 1.0,
        "material": "Wood",
        "category": "lighting",
        "tags": ["tavern", "dungeon", "common"],
        "tier": 1,
    },
    {
        "name": "chandelier",
        "prompt": "a hanging chandelier with a circular metal frame and candle holders, medieval style",
        "size": 2.0,
        "material": "Metal",
        "category": "lighting",
        "tags": ["tavern", "feast_hall", "temple"],
        "tier": 1,
    },

    # --- Tier 2: Themed props ---
    {
        "name": "throne",
        "prompt": "a grand stone throne with armrests and a tall ornate back, fit for a king",
        "size": 2.5,
        "material": "Stone",
        "category": "seating",
        "tags": ["throne_room", "temple"],
        "tier": 2,
    },
    {
        "name": "anvil",
        "prompt": "a blacksmith's iron anvil on a wooden stump base, classic shape with horn",
        "size": 1.5,
        "material": "Metal",
        "category": "workstation",
        "tags": ["forge", "workshop"],
        "tier": 2,
    },
    {
        "name": "workbench",
        "prompt": "a sturdy wooden workbench with a thick top and shelf underneath, craftsman style",
        "size": 3.0,
        "material": "Wood",
        "category": "workstation",
        "tags": ["workshop", "forge"],
        "tier": 2,
    },
    {
        "name": "cauldron",
        "prompt": "a large round iron cauldron on three legs, witch's brewing pot style",
        "size": 2.0,
        "material": "Metal",
        "category": "fixture",
        "tags": ["kitchen", "alchemy", "camp"],
        "tier": 2,
    },
    {
        "name": "market_stall",
        "prompt": "a wooden market stall with a counter, awning roof, and open front for displaying goods",
        "size": 4.0,
        "material": "Wood",
        "category": "structure",
        "tags": ["market", "outdoor"],
        "tier": 2,
    },
    {
        "name": "well",
        "prompt": "a stone well with a circular wall, wooden beam support and bucket rope, medieval village style",
        "size": 3.0,
        "material": "Stone",
        "category": "structure",
        "tags": ["outdoor", "village"],
        "tier": 2,
    },
    {
        "name": "signpost",
        "prompt": "a tall wooden signpost with two angled wooden signs pointing in different directions",
        "size": 2.0,
        "material": "Wood",
        "category": "outdoor",
        "tags": ["outdoor", "village", "road"],
        "tier": 2,
    },
    {
        "name": "weapon_rack",
        "prompt": "a wooden weapon rack standing against a wall, with slots for swords and axes",
        "size": 2.5,
        "material": "Wood",
        "category": "furniture",
        "tags": ["barracks", "armory", "guard"],
        "tier": 2,
    },
    {
        "name": "armor_stand",
        "prompt": "a wooden armor display stand shaped like a humanoid torso on a pole base",
        "size": 2.0,
        "material": "Wood",
        "category": "furniture",
        "tags": ["barracks", "armory", "shop"],
        "tier": 2,
    },
    {
        "name": "banner",
        "prompt": "a tall standing banner on a wooden pole with a rectangular cloth flag",
        "size": 2.5,
        "material": "Wood",
        "category": "decoration",
        "tags": ["feast_hall", "throne_room", "outdoor"],
        "tier": 2,
    },
    {
        "name": "crate",
        "prompt": "a simple wooden storage crate, cubic shape with visible plank slats",
        "size": 1.5,
        "material": "Wood",
        "category": "storage",
        "tags": ["warehouse", "cellar", "dock", "common"],
        "tier": 2,
    },
    {
        "name": "ladder",
        "prompt": "a tall wooden ladder with two side rails and rungs, leaning against a wall",
        "size": 2.5,
        "material": "Wood",
        "category": "fixture",
        "tags": ["common", "cellar", "attic"],
        "tier": 2,
    },
]


def get_script_path():
    """Get path to blocksmith_generate.py."""
    return os.path.join(os.path.dirname(__file__), "blocksmith_generate.py")


def get_templates_dir():
    """Get the templates output directory."""
    return os.path.join(
        os.path.dirname(os.path.dirname(__file__)), "resources", "templates"
    )


def template_exists(name, templates_dir):
    """Check if a template file already exists."""
    return os.path.exists(os.path.join(templates_dir, f"{name}.voxel"))


def generate_item(item, model, templates_dir):
    """Generate a single furniture template using blocksmith_generate.py."""
    cmd = [
        sys.executable, get_script_path(),
        item["prompt"],
        "--name", item["name"],
        "--material", item["material"],
        "--size", str(item["size"]),
        "--model", model,
        "--json",
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        return {"success": False, "error": result.stderr.strip()}
    try:
        return json.loads(result.stdout.strip())
    except json.JSONDecodeError:
        return {"success": False, "error": result.stdout.strip()}


def update_catalog_metadata(item, templates_dir):
    """Add category and tags to catalog entry if missing."""
    catalog_path = os.path.join(templates_dir, "template_catalog.json")
    if not os.path.exists(catalog_path):
        return
    with open(catalog_path, "r", encoding="utf-8") as f:
        catalog = json.load(f)
    if item["name"] in catalog:
        entry = catalog[item["name"]]
        changed = False
        if "category" not in entry:
            entry["category"] = item["category"]
            changed = True
        if "tags" not in entry:
            entry["tags"] = item["tags"]
            changed = True
        if changed:
            with open(catalog_path, "w", encoding="utf-8") as f:
                json.dump(catalog, f, indent=2)


def main():
    parser = argparse.ArgumentParser(
        description="Generate furniture template library using BlockSmith"
    )
    parser.add_argument(
        "--tier", type=int, choices=[1, 2], default=None,
        help="Generate only this tier (1=essential, 2=themed)"
    )
    parser.add_argument(
        "--item", type=str, default=None,
        help="Generate a single item by name"
    )
    parser.add_argument(
        "--list", action="store_true",
        help="Show status of all library items"
    )
    parser.add_argument(
        "--dry-run", action="store_true",
        help="Show what would be generated without actually generating"
    )
    parser.add_argument(
        "--model", default="anthropic/claude-sonnet-4-20250514",
        help="LLM model to use"
    )
    parser.add_argument(
        "--force", action="store_true",
        help="Regenerate even if template exists"
    )
    args = parser.parse_args()

    templates_dir = get_templates_dir()

    # Filter items
    items = FURNITURE_LIBRARY
    if args.tier:
        items = [i for i in items if i["tier"] == args.tier]
    if args.item:
        items = [i for i in items if i["name"] == args.item]
        if not items:
            print(f"Unknown item: {args.item}")
            print(f"Available: {', '.join(i['name'] for i in FURNITURE_LIBRARY)}")
            return

    # --list mode
    if args.list:
        print(f"{'Name':<20} {'Tier':>4} {'Category':<12} {'Material':<8} {'Status':<10} {'Tags'}")
        print("-" * 90)
        for item in FURNITURE_LIBRARY:
            exists = template_exists(item["name"], templates_dir)
            status = "CACHED" if exists else "missing"
            tags = ", ".join(item["tags"][:3])
            print(f"{item['name']:<20} {item['tier']:>4} {item['category']:<12} "
                  f"{item['material']:<8} {status:<10} {tags}")
        cached = sum(1 for i in FURNITURE_LIBRARY if template_exists(i["name"], templates_dir))
        print(f"\n{cached}/{len(FURNITURE_LIBRARY)} templates cached")
        return

    # Find items that need generation
    if args.force:
        to_generate = items
    else:
        to_generate = [i for i in items if not template_exists(i["name"], templates_dir)]

    if not to_generate:
        print("All requested templates are already cached.")
        return

    # --dry-run mode
    if args.dry_run:
        print(f"Would generate {len(to_generate)} templates:")
        for item in to_generate:
            print(f"  {item['name']}: {item['prompt'][:50]}... (size={item['size']}, mat={item['material']})")
        return

    # Generate missing items
    print(f"Generating {len(to_generate)} furniture templates...")
    print(f"Model: {args.model}\n")

    success_count = 0
    fail_count = 0

    for i, item in enumerate(to_generate, 1):
        print(f"[{i}/{len(to_generate)}] {item['name']}...")
        result = generate_item(item, args.model, templates_dir)

        if result.get("success"):
            success_count += 1
            total = result.get("total_primitives", "?")
            print(f"  OK ({total} primitives)")
            # Update catalog with category/tags
            update_catalog_metadata(item, templates_dir)
        else:
            fail_count += 1
            error = result.get("error", "unknown error")
            print(f"  FAILED: {error}")

    print(f"\nDone: {success_count} generated, {fail_count} failed")


if __name__ == "__main__":
    main()
