#!/usr/bin/env python3
"""
BlockSmith → Phyxel Template Bridge

Generates a block-based 3D model from a text prompt using BlockSmith (LLM-powered),
converts it to a Phyxel voxel template (.voxel), and optionally registers it with
the running engine.

Usage:
    python tools/blocksmith_generate.py "a wooden chair" --name chair
    python tools/blocksmith_generate.py "medieval torch" --name torch --size 2 --material Wood
    python tools/blocksmith_generate.py "a table" --name table --model openai/gpt-4o

Environment variables:
    ANTHROPIC_API_KEY   - For Claude models (anthropic/claude-sonnet-4-20250514)
    PHYXEL_AI_API_KEY   - Alias for ANTHROPIC_API_KEY (engine convention)
    GEMINI_API_KEY      - For Gemini models (gemini/gemini-2.5-pro)
    OPENAI_API_KEY      - For OpenAI models (openai/gpt-4o)
"""

import argparse
import getpass
import json
import os
import sys
import tempfile
import subprocess
from datetime import datetime, timezone

# Ensure blocksmith and tools modules are importable
_repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_blocksmith_path = os.path.join(_repo_root, "external", "blocksmith")
if _blocksmith_path not in sys.path:
    sys.path.insert(0, _blocksmith_path)
_tools_path = os.path.join(_repo_root, "tools")
if _tools_path not in sys.path:
    sys.path.insert(0, _tools_path)

def setup_api_keys(model="anthropic/claude-sonnet-4-20250514", json_mode=False):
    """Ensure the required API key is set for the chosen model provider.

    Propagates PHYXEL_AI_API_KEY → ANTHROPIC_API_KEY as a convenience alias.
    If the required key is still missing, prompts the user to enter it
    interactively (or exits with a clear error in --json mode).
    """
    # Convenience alias
    phyxel_key = os.environ.get("PHYXEL_AI_API_KEY")
    if phyxel_key and not os.environ.get("ANTHROPIC_API_KEY"):
        os.environ["ANTHROPIC_API_KEY"] = phyxel_key

    # Determine which env var is needed for the selected provider
    model_lower = model.lower()
    if model_lower.startswith("anthropic/"):
        env_var = "ANTHROPIC_API_KEY"
        provider = "Anthropic (Claude)"
    elif model_lower.startswith("gemini/"):
        env_var = "GEMINI_API_KEY"
        provider = "Google (Gemini)"
    elif model_lower.startswith("openai/"):
        env_var = "OPENAI_API_KEY"
        provider = "OpenAI"
    else:
        # Unknown provider — nothing we can prompt for
        return

    if os.environ.get(env_var):
        return  # Key already present, nothing to do

    if json_mode:
        print(json.dumps({
            "success": False,
            "error": f"Missing API key: {env_var} is not set. "
                     f"Set it in your environment before running in --json mode.",
        }))
        sys.exit(1)

    print(f"\n{provider} API key not found (env var: {env_var}).")
    try:
        key = getpass.getpass(f"Enter your {provider} API key: ").strip()
    except (EOFError, KeyboardInterrupt):
        print("\nAborted.")
        sys.exit(1)

    if not key:
        print("ERROR: No key entered. Aborting.")
        sys.exit(1)

    os.environ[env_var] = key
    # Also set the Phyxel alias so the engine picks it up
    if env_var == "ANTHROPIC_API_KEY":
        os.environ["PHYXEL_AI_API_KEY"] = key
    print(f"Key accepted (session only — set {env_var} in your environment to avoid this prompt).\n")


def generate_bbmodel(prompt, model, output_path, system_prompt=None):
    """Generate a .bbmodel file from a text prompt using BlockSmith.

    Args:
        prompt: User prompt text
        model: LLM model identifier
        output_path: Path for the .bbmodel output
        system_prompt: Optional system prompt override (for building mode)
    """
    from blocksmith import Blocksmith

    bs = Blocksmith(default_model=model)
    if system_prompt:
        # Use generator directly to pass custom system prompt
        print(f"Generating building with {model}...")
        gen_response = bs.generator.generate(prompt, system_prompt=system_prompt)
        # Build a GenerationResult from the raw response
        from blocksmith.client import GenerationResult
        result = GenerationResult(
            dsl=gen_response.code,
            tokens=gen_response.tokens,
            cost=gen_response.cost,
            model=gen_response.model,
            _bs=bs,
        )
    else:
        print(f"Generating model with {model}...")
        result = bs.generate(prompt)

    print(f"  Tokens used: {result.tokens}")
    if result.cost is not None:
        print(f"  Cost: ${result.cost:.4f}")

    result.save(output_path)
    print(f"  Saved .bbmodel to: {output_path}")
    return result


def convert_bbmodel_to_template(bbmodel_path, template_path, material, size, optimize, resolution="auto", facing_yaw=None):
    """Convert a .bbmodel file to a Phyxel voxel template using the existing pipeline."""
    pipeline_dir = os.path.join(os.path.dirname(__file__), "asset_pipeline")
    converter = os.path.join(pipeline_dir, "bbmodel_to_template.py")

    cmd = [
        sys.executable, converter,
        bbmodel_path, template_path,
        "--material", material,
        "--size", str(size),
        "--resolution", resolution,
    ]
    if optimize:
        cmd.append("--optimize")
    if facing_yaw is not None:
        cmd += ["--facing-yaw", str(facing_yaw)]

    print(f"Converting to Phyxel template (size={size}, material={material})...")
    result = subprocess.run(cmd, capture_output=True, text=True)

    if result.returncode != 0:
        print(f"  Conversion FAILED:\n{result.stderr}")
        return False

    for line in result.stdout.strip().split("\n"):
        print(f"  {line}")
    return True


def classify_element_material(element_name, material_map):
    """Infer game material from bbmodel element name prefix.

    The building system prompt instructs the LLM to name elements with
    descriptive prefixes (wall_, floor, roof_, etc.) and use mat-* material
    kwargs. Since BlockSmith's cuboid() ignores the material kwarg, we
    classify by element name instead.
    """
    name = element_name.lower()
    if name.startswith(("wall", "facade")):
        return material_map.get("mat-wall", "Stone")
    elif name.startswith(("floor", "foundation")):
        return material_map.get("mat-floor", "Wood")
    elif name.startswith("roof"):
        return material_map.get("mat-roof", "Wood")
    elif name.startswith(("trim", "beam", "pillar", "column")):
        return material_map.get("mat-trim", "Wood")
    elif name.startswith("door"):
        return material_map.get("mat-door", "Wood")
    elif name.startswith("window"):
        return material_map.get("mat-window", "Glass")
    elif name.startswith("chimney"):
        return material_map.get("mat-chimney", "Stone")
    elif name.startswith(("accent", "decor", "ornament")):
        return material_map.get("mat-accent", "Stone")
    elif name.startswith(("step", "stair")):
        return material_map.get("mat-floor", "Wood")
    else:
        return material_map.get("mat-wall", "Stone")


# BlockSmith texel_density: 1 DSL unit = 16 bbmodel pixels
TEXEL_DENSITY = 16


def convert_bbmodel_building_direct(bbmodel_path, template_path, material_map):
    """Convert a building .bbmodel directly to a multi-material template.

    For buildings, the LLM generates cuboids in block units. The bbmodel stores
    coordinates scaled by texel_density (16). We reverse the scaling and map
    each cuboid directly to voxels, classifying material from element names.

    This bypasses the mesh→voxelizer pipeline entirely, producing pixel-perfect
    multi-material results.
    """
    with open(bbmodel_path, "r", encoding="utf-8") as f:
        data = json.load(f)

    elements = data.get("elements", [])
    if not elements:
        print("  ERROR: No elements found in bbmodel")
        return False

    # Collect voxels as {(x,y,z): material}
    voxels = {}
    element_stats = {}  # material → count of elements

    for elem in elements:
        name = elem.get("name", "unknown")
        pos_from = elem.get("from", [0, 0, 0])
        pos_to = elem.get("to", [0, 0, 0])

        material = classify_element_material(name, material_map)
        element_stats[material] = element_stats.get(material, 0) + 1

        # Convert pixel coords back to block coords
        x0 = int(round(min(pos_from[0], pos_to[0]) / TEXEL_DENSITY))
        y0 = int(round(min(pos_from[1], pos_to[1]) / TEXEL_DENSITY))
        z0 = int(round(min(pos_from[2], pos_to[2]) / TEXEL_DENSITY))
        x1 = int(round(max(pos_from[0], pos_to[0]) / TEXEL_DENSITY))
        y1 = int(round(max(pos_from[1], pos_to[1]) / TEXEL_DENSITY))
        z1 = int(round(max(pos_from[2], pos_to[2]) / TEXEL_DENSITY))

        # Skip degenerate elements
        if x0 == x1 or y0 == y1 or z0 == z1:
            continue

        for x in range(x0, x1):
            for y in range(y0, y1):
                for z in range(z0, z1):
                    voxels[(x, y, z)] = material

    if not voxels:
        print("  ERROR: No voxels generated from building elements")
        return False

    # Normalize coordinates so minimum is (0, 0, 0)
    min_x = min(v[0] for v in voxels)
    min_y = min(v[1] for v in voxels)
    min_z = min(v[2] for v in voxels)

    # Write template
    with open(template_path, "w", encoding="utf-8") as f:
        f.write("# Building template (direct conversion)\n")
        f.write(f"# Elements: {len(elements)}\n")
        f.write(f"# Voxels: {len(voxels)}\n")
        for mat, count in sorted(element_stats.items()):
            f.write(f"# Material group: {mat} ({count} elements)\n")
        f.write("\n")

        # Sort by Y, Z, X for consistent output
        sorted_voxels = sorted(voxels.items(), key=lambda v: (v[0][1], v[0][2], v[0][0]))

        for (x, y, z), material in sorted_voxels:
            f.write(f"C {x - min_x} {y - min_y} {z - min_z} {material}\n")

    print(f"  Direct conversion: {len(voxels)} voxels from {len(elements)} elements")
    for mat, count in sorted(element_stats.items()):
        print(f"    {mat}: {count} elements")
    return True


def count_template_primitives(template_path):
    """Count cubes, subcubes, microcubes in a template file."""
    cubes = subcubes = microcubes = 0
    with open(template_path, "r") as f:
        for line in f:
            line = line.strip()
            if line.startswith("C "):
                cubes += 1
            elif line.startswith("S "):
                subcubes += 1
            elif line.startswith("M "):
                microcubes += 1
    return cubes, subcubes, microcubes


CATALOG_FILENAME = "template_catalog.json"

def get_catalog_path(templates_dir):
    return os.path.join(templates_dir, CATALOG_FILENAME)

def load_catalog(templates_dir):
    path = get_catalog_path(templates_dir)
    if os.path.exists(path):
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    return {}

def save_catalog(templates_dir, catalog):
    path = get_catalog_path(templates_dir)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(catalog, f, indent=2)

def catalog_add(templates_dir, name, prompt, model, material, size, cubes, subcubes, microcubes, cost=None, building_meta=None):
    catalog = load_catalog(templates_dir)
    catalog[name] = {
        "prompt": prompt,
        "model": model,
        "material": material,
        "size": size,
        "cubes": cubes,
        "subcubes": subcubes,
        "microcubes": microcubes,
        "total": cubes + subcubes + microcubes,
        "cost": cost,
        "created": datetime.now(timezone.utc).isoformat(),
    }
    if building_meta:
        catalog[name]["building"] = building_meta
    save_catalog(templates_dir, catalog)

def catalog_list(templates_dir):
    catalog = load_catalog(templates_dir)
    if not catalog:
        print("No generated templates in catalog.")
        return
    print(f"{'Name':<25} {'Primitives':>10} {'Material':<10} {'Size':>5} {'Prompt'}")
    print("-" * 90)
    for name, info in sorted(catalog.items()):
        total = info.get("total", "?")
        mat = info.get("material", "?")
        size = info.get("size", "?")
        prompt = info.get("prompt", "?")
        if len(prompt) > 40:
            prompt = prompt[:37] + "..."
        print(f"{name:<25} {total:>10} {mat:<10} {size:>5} {prompt}")


def main():
    parser = argparse.ArgumentParser(
        description="Generate a Phyxel voxel template from a text prompt using BlockSmith"
    )
    parser.add_argument("prompt", nargs="?", default="", help="Text description of the model to generate")
    parser.add_argument("--name", default=None, help="Template name (used as filename)")
    parser.add_argument(
        "--model",
        default="anthropic/claude-sonnet-4-20250514",
        help="LLM model to use (default: anthropic/claude-sonnet-4-20250514)",
    )
    parser.add_argument(
        "--material", default="Wood", help="Default material (default: Wood)"
    )
    parser.add_argument(
        "--size", type=float, default=3.0,
        help="Target size in world cubes (default: 3.0)",
    )
    parser.add_argument("--optimize", action="store_true", default=True, help="Optimize grid alignment (default: True)")
    parser.add_argument("--no-optimize", dest="optimize", action="store_false", help="Disable grid alignment optimization")
    parser.add_argument("--facing-yaw", type=float, default=None,
                        help="Canonical facing yaw in radians (0=+Z, π≈3.14159=-Z). Stored in template header.")
    parser.add_argument(
        "--output-dir",
        default=None,
        help="Output directory (default: resources/templates/)",
    )
    parser.add_argument(
        "--keep-bbmodel", action="store_true", help="Keep intermediate .bbmodel file"
    )
    parser.add_argument(
        "--json", action="store_true", help="Output result as JSON (for scripting)"
    )
    parser.add_argument(
        "--force", action="store_true", help="Overwrite existing template with same name"
    )
    parser.add_argument(
        "--list", action="store_true", help="List all previously generated templates and exit"
    )
    parser.add_argument(
        "--search", type=str, default=None, help="Search catalog by name or prompt substring"
    )

    # Building mode arguments
    parser.add_argument(
        "--building", action="store_true",
        help="Building mode: generate a hollow building structure using structured prompts"
    )
    parser.add_argument("--width", type=int, default=10, help="Building width in blocks (X axis, building mode)")
    parser.add_argument("--depth", type=int, default=12, help="Building depth in blocks (Z axis, building mode)")
    parser.add_argument("--height", type=int, default=4, help="Interior height per story in blocks (building mode)")
    parser.add_argument("--stories", type=int, default=1, help="Number of stories (building mode)")
    parser.add_argument("--building-type", default="house", help="Building type: house, tavern, tower, shop, temple, barn")
    parser.add_argument("--style", default="medieval", help="Architectural style (e.g. medieval, elven, gothic)")
    parser.add_argument("--door-facing", default="front", help="Which wall has the door: front, back, left, right")
    parser.add_argument("--windows", type=int, default=2, help="Windows per side wall per story")
    parser.add_argument(
        "--materials", type=str, default=None,
        help='Material palette as JSON: \'{"wall":"Stone","floor":"Wood","roof":"Wood"}\''
    )

    args = parser.parse_args()

    # Determine output directory early (needed for --list and --search)
    if args.output_dir:
        output_dir = args.output_dir
    else:
        output_dir = os.path.join(
            os.path.dirname(os.path.dirname(__file__)), "resources", "templates"
        )

    # Handle --list
    if args.list:
        catalog_list(output_dir)
        return

    # Handle --search
    if args.search:
        catalog = load_catalog(output_dir)
        query = args.search.lower()
        found = {k: v for k, v in catalog.items()
                 if query in k.lower() or query in v.get("prompt", "").lower()}
        if not found:
            print(f"No templates matching '{args.search}'")
        else:
            for name, info in found.items():
                print(f"  {name}: {info.get('prompt', '?')} ({info.get('total', '?')} primitives, {info.get('material', '?')})")
        return

    # Validate required args for generation
    if not args.name:
        parser.error("--name is required for generation")
    if not args.prompt and not args.building:
        parser.error("prompt is required for generation (or use --building)")

    # Building mode: construct structured prompt and compute size
    building_system_prompt = None
    building_material_map = None
    if args.building:
        from building_prompts import (
            build_building_prompt, BUILDING_SYSTEM_PROMPT, make_material_map
        )
        mat_palette = json.loads(args.materials) if args.materials else None
        user_prompt = build_building_prompt(
            building_type=args.building_type,
            style=args.style,
            width=args.width,
            depth=args.depth,
            height=args.height,
            stories=args.stories,
            door_wall=args.door_facing,
            windows_per_wall=args.windows,
            materials=mat_palette,
            extra_notes=args.prompt,  # user prompt becomes extra style notes
        )
        building_system_prompt = BUILDING_SYSTEM_PROMPT
        building_material_map = make_material_map(mat_palette)
        # Size = largest dimension so 1 cuboid unit = 1 game block
        total_height = args.stories * (args.height + 1) + 1  # floors + roof
        args.size = float(max(args.width, args.depth, total_height))
        args.prompt = user_prompt
        # Use cube resolution for buildings (walls are full blocks)
        building_resolution = "cube"
        if not args.json:
            print(f"Building mode: {args.style} {args.building_type} "
                  f"({args.width}×{args.depth}×{args.height}, {args.stories} stories)")
    else:
        building_resolution = None

    setup_api_keys(model=args.model, json_mode=args.json)

    os.makedirs(output_dir, exist_ok=True)

    template_path = os.path.join(output_dir, f"{args.name}.voxel")
    bbmodel_path = os.path.join(output_dir, f"{args.name}.bbmodel")

    # Check if template already exists
    if os.path.exists(template_path) and not args.force:
        cubes, subcubes, microcubes = count_template_primitives(template_path)
        # Ensure it's in the catalog even if it was generated before catalog existed
        catalog = load_catalog(output_dir)
        if args.name not in catalog:
            catalog_add(output_dir, args.name, args.prompt, args.model, args.material,
                        args.size, cubes, subcubes, microcubes)
        if args.json:
            print(json.dumps({
                "success": True, "cached": True, "template_path": template_path,
                "name": args.name, "cubes": cubes, "subcubes": subcubes,
                "microcubes": microcubes, "total_primitives": cubes + subcubes + microcubes,
            }))
        else:
            print(f"Template '{args.name}' already exists: {template_path}")
            print(f"  Primitives: {cubes}C + {subcubes}S + {microcubes}M = {cubes + subcubes + microcubes} total")
            print(f"  Use --force to regenerate")
        return

    # Step 1: Generate .bbmodel via BlockSmith
    try:
        result = generate_bbmodel(args.prompt, args.model, bbmodel_path,
                                  system_prompt=building_system_prompt)
    except Exception as e:
        if args.json:
            print(json.dumps({"success": False, "error": str(e)}))
        else:
            print(f"ERROR: Generation failed: {e}")
        sys.exit(1)

    # Step 2: Convert .bbmodel → Phyxel .voxel template
    if args.building and building_material_map:
        # Direct conversion for buildings: element names → materials, no mesh/voxelizer
        success = convert_bbmodel_building_direct(
            bbmodel_path, template_path, building_material_map
        )
    else:
        resolution = building_resolution or "auto"
        success = convert_bbmodel_to_template(
            bbmodel_path, template_path, args.material, args.size, args.optimize,
            resolution=resolution, facing_yaw=args.facing_yaw
        )

    if not success:
        if args.json:
            print(json.dumps({"success": False, "error": "bbmodel conversion failed"}))
        else:
            print("ERROR: Conversion failed")
        sys.exit(1)

    # Step 3: Count primitives
    cubes, subcubes, microcubes = count_template_primitives(template_path)

    # Clean up .bbmodel unless --keep-bbmodel
    if not args.keep_bbmodel and os.path.exists(bbmodel_path):
        os.remove(bbmodel_path)

    # Save to catalog
    building_meta = None
    if args.building:
        building_meta = {
            "type": args.building_type,
            "style": args.style,
            "width": args.width,
            "depth": args.depth,
            "height": args.height,
            "stories": args.stories,
            "door_facing": args.door_facing,
            "windows": args.windows,
        }
    catalog_add(output_dir, args.name, args.prompt, args.model, args.material,
                args.size, cubes, subcubes, microcubes, building_meta=building_meta)

    # Output result
    result_info = {
        "success": True,
        "template_path": template_path,
        "name": args.name,
        "cubes": cubes,
        "subcubes": subcubes,
        "microcubes": microcubes,
        "total_primitives": cubes + subcubes + microcubes,
        "model_used": args.model,
    }

    if args.json:
        print(json.dumps(result_info))
    else:
        print(f"\nTemplate ready: {template_path}")
        print(f"  Primitives: {cubes}C + {subcubes}S + {microcubes}M = {cubes + subcubes + microcubes} total")
        print(f"  Use: spawn_template with name '{args.name}'")


if __name__ == "__main__":
    main()
