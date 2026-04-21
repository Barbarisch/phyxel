#!/usr/bin/env python3
"""
package_game.py — Package a Phyxel game into a standalone distributable directory.

Creates a self-contained game directory with only the files needed to run:
  - Game executable (built from game project C++ source, or copied from engine)
  - Compiled shaders (.spv)
  - Texture atlas
  - Pre-baked world database (worlds/default.db)
  - Game definition JSON (for NPC/story loading at runtime)
  - Only the resources actually referenced by the game

The output directory can be zipped and distributed — no engine source,
build system, development tools, MCP server, or Python scripts included.

Usage:
  # Full workflow: scaffold → pre-bake → build → package
  python tools/package_game.py MyGame --from-engine --prebake-world

  # From a game project directory (has its own CMakeLists.txt)
  python tools/package_game.py MyGame --project-dir path/to/MyGame

  # Quick: use engine exe directly (legacy)
  python tools/package_game.py MyGame --definition game.json

Can also be invoked via MCP tool: package_game
"""

import argparse
import json
import os
import shutil
import sys
from pathlib import Path


# ============================================================================
# Configuration
# ============================================================================

PHYXEL_ROOT = Path(__file__).resolve().parent.parent

# Compiled shaders required by the engine (SPIR-V only)
REQUIRED_SHADERS = [
    "static_voxel.vert.spv",
    "voxel.frag.spv",
    "dynamic_voxel.vert.spv",
    "debug_voxel.vert.spv",
    "debug_voxel.frag.spv",
    "debug_line.vert.spv",
    "debug_line.frag.spv",
    "character.vert.spv",
    "character.frag.spv",
    "character_instanced.vert.spv",
    "post_process.vert.spv",
    "post_process.frag.spv",
    "blur.frag.spv",
    "shadow.vert.spv",
    "shadow.frag.spv",
    "debris.vert.spv",
    "debris.frag.spv",
]

# Always-required resource files
REQUIRED_RESOURCES = [
    "resources/textures/cube_atlas.png",
    "resources/textures/cube_atlas.json",
]

# Animation files available to include
ALL_ANIM_FILES = [
    "character.anim",
    "character_box.anim",
    "character_complete.anim",
    "resources/animated_characters/character_dragon.anim",
    "resources/animated_characters/character_female.anim",
    "resources/animated_characters/character_female2.anim",
    "resources/animated_characters/character_female3.anim",
    "resources/animated_characters/humanoid.anim",
    "resources/animated_characters/character_spider.anim",
    "resources/animated_characters/character_spider2.anim",
    "resources/animated_characters/character_spider3.anim",
    "resources/animated_characters/character_wolf.anim",
]


def find_engine_binary(config: str = "Debug") -> Path:
    """Locate the compiled engine executable."""
    candidates = [
        PHYXEL_ROOT / "build" / "game" / config / "phyxel.exe",
        PHYXEL_ROOT / "build" / "game" / "phyxel.exe",
        PHYXEL_ROOT / "build" / config / "phyxel.exe",
    ]
    for p in candidates:
        if p.exists():
            return p
    raise FileNotFoundError(
        f"Engine binary not found. Checked:\n"
        + "\n".join(f"  {c}" for c in candidates)
        + "\nBuild the engine first: cmake --build build --config " + config
    )


def find_game_binary(project_dir: Path, name: str, config: str = "Debug") -> Path:
    """Locate a compiled game project executable."""
    candidates = [
        project_dir / "build" / config / f"{name}.exe",
        project_dir / "build" / f"{name}.exe",
    ]
    for p in candidates:
        if p.exists():
            return p
    raise FileNotFoundError(
        f"Game binary not found in {project_dir}/build/. "
        f"Build the game project first:\n"
        f"  cd {project_dir}\n"
        f"  cmake -B build -S .\n"
        f"  cmake --build build --config {config}"
    )


def prebake_world(engine_url: str = "http://localhost:8090") -> Path:
    """
    Save the current engine world to SQLite via the HTTP API.
    Returns the path to the saved database file.
    Requires the engine to be running with the game loaded.
    """
    import httpx

    # Save all chunks (not just dirty ones)
    resp = httpx.post(
        f"{engine_url}/api/world/save",
        json={"all": True},
        timeout=60,
    )
    resp.raise_for_status()
    result = resp.json()
    if not result.get("success"):
        raise RuntimeError(f"Failed to save world: {result}")

    # The engine saves to its configured worlds/default.db
    db_path = PHYXEL_ROOT / "worlds" / "default.db"
    if not db_path.exists():
        raise RuntimeError(f"World database not found after save: {db_path}")

    return db_path


def scan_game_definition(definition: dict) -> dict:
    """Analyze a game definition to determine which resources are needed."""
    needs = {
        "templates": set(),
        "anim_files": set(),
        "dialogues": set(),
        "sounds": set(),
        "has_npcs": False,
        "has_player": False,
        "has_story": False,
        "has_audio": False,
        "is_multi_scene": False,
        "scene_databases": [],
    }

    # Multi-scene: recurse into each scene's definition
    if "scenes" in definition:
        needs["is_multi_scene"] = True
        for scene in definition["scenes"]:
            db = scene.get("worldDatabase", f"worlds/{scene.get('id', 'unknown')}.db")
            needs["scene_databases"].append(db)
            scene_def = scene.get("definition", {})
            sub = scan_game_definition(scene_def)
            needs["templates"] |= sub["templates"]
            needs["anim_files"] |= sub["anim_files"]
            needs["dialogues"] |= sub["dialogues"]
            needs["sounds"] |= sub["sounds"]
            needs["has_npcs"] = needs["has_npcs"] or sub["has_npcs"]
            needs["has_player"] = needs["has_player"] or sub["has_player"]
            needs["has_story"] = needs["has_story"] or sub["has_story"]
            needs["has_audio"] = needs["has_audio"] or sub["has_audio"]
        if definition.get("playerDefaults"):
            needs["has_player"] = True
        if definition.get("globalStory"):
            needs["has_story"] = True
        return needs

    # Check structures for template references
    for struct in definition.get("structures", []):
        if struct.get("type") == "template":
            name = struct.get("name", "")
            if name:
                needs["templates"].add(name)

    # Check NPCs
    npcs = definition.get("npcs", [])
    if npcs:
        needs["has_npcs"] = True
    for npc in npcs:
        anim = npc.get("animFile", "")
        if anim:
            needs["anim_files"].add(anim)
        if npc.get("storyCharacter"):
            needs["has_story"] = True

    # Check player
    player = definition.get("player", {})
    if player:
        needs["has_player"] = True
        ptype = player.get("type", "physics")
        anim = player.get("animFile", "")
        if anim:
            needs["anim_files"].add(anim)
        elif ptype == "animated":
            needs["anim_files"].add("character_complete.anim")

    # Check story
    if definition.get("story"):
        needs["has_story"] = True

    return needs


def copy_file_safe(src: Path, dst: Path) -> bool:
    """Copy a file, creating parent directories as needed.  Skip if same file."""
    if not src.exists():
        return False
    dst.parent.mkdir(parents=True, exist_ok=True)
    if dst.exists() and os.path.samefile(src, dst):
        return True
    shutil.copy2(src, dst)
    return True


def package_game(
    name: str,
    output_dir: Path,
    definition: dict | None = None,
    definition_path: Path | None = None,
    config: str = "Debug",
    include_all_resources: bool = False,
    include_mcp: bool = False,
    window_title: str | None = None,
    project_dir: Path | None = None,
    world_db_path: Path | None = None,
) -> dict:
    """
    Package a complete standalone game directory.

    Args:
        project_dir: Path to a scaffolded game project (has its own CMakeLists.txt
                     and compiled executable). If None, uses the engine binary.
        world_db_path: Path to a pre-baked worlds/default.db. Copied into the package
                       so the game loads instantly without regenerating terrain.

    Returns a summary dict with counts and paths.
    """
    result = {
        "success": False,
        "output_dir": str(output_dir),
        "files_copied": 0,
        "errors": [],
        "warnings": [],
    }

    # Create output directory
    output_dir.mkdir(parents=True, exist_ok=True)

    files_copied = 0
    is_game_project = project_dir is not None

    # ── 1. Game binary ──────────────────────────────────────────────────
    game_exe_name = f"{name}.exe"
    try:
        if is_game_project:
            src_bin = find_game_binary(project_dir, name, config)
        else:
            src_bin = find_engine_binary(config)
        dst = output_dir / game_exe_name
        shutil.copy2(src_bin, dst)
        files_copied += 1
    except FileNotFoundError as e:
        result["errors"].append(str(e))
        return result

    # Copy PDB if it exists (useful for debugging)
    pdb = src_bin.with_suffix(".pdb")
    if pdb.exists():
        shutil.copy2(pdb, output_dir / f"{name}.pdb")
        files_copied += 1

    # ── 2. Compiled shaders ─────────────────────────────────────────────
    shaders_dir = output_dir / "shaders"
    shaders_dir.mkdir(exist_ok=True)
    for shader in REQUIRED_SHADERS:
        src = PHYXEL_ROOT / "shaders" / shader
        if copy_file_safe(src, shaders_dir / shader):
            files_copied += 1
        else:
            result["warnings"].append(f"Shader not found: {shader}")

    # ── 3. Required resources (texture atlas) ───────────────────────────
    for res in REQUIRED_RESOURCES:
        src = PHYXEL_ROOT / res
        dst = output_dir / res
        if copy_file_safe(src, dst):
            files_copied += 1
        else:
            result["errors"].append(f"Required resource missing: {res}")

    # ── 4. Game definition ──────────────────────────────────────────────
    if definition:
        game_def_path = output_dir / "game.json"
        game_def_path.write_text(json.dumps(definition, indent=2), encoding="utf-8")
        files_copied += 1
    elif definition_path and definition_path.exists():
        dst = output_dir / "game.json"
        shutil.copy2(definition_path, dst)
        # Load it so we can scan for dependencies
        definition = json.loads(definition_path.read_text(encoding="utf-8"))
        files_copied += 1

    # ── 5. Scan definition for resource dependencies ────────────────────
    needs = scan_game_definition(definition) if definition else {}

    # ── 6. Templates ────────────────────────────────────────────────────
    templates_to_copy = set()
    if include_all_resources:
        templates_src = PHYXEL_ROOT / "resources" / "templates"
        if templates_src.exists():
            templates_to_copy = {f.name for f in templates_src.iterdir() if f.is_file()}
    elif needs.get("templates"):
        templates_to_copy = needs["templates"]

    for tpl in templates_to_copy:
        src = PHYXEL_ROOT / "resources" / "templates" / tpl
        dst = output_dir / "resources" / "templates" / tpl
        if copy_file_safe(src, dst):
            files_copied += 1
        else:
            result["warnings"].append(f"Template not found: {tpl}")

    # ── 7. Animation files ──────────────────────────────────────────────
    anim_files_to_copy = set()
    if include_all_resources:
        for af in ALL_ANIM_FILES:
            anim_files_to_copy.add(af)
    elif needs.get("anim_files"):
        anim_files_to_copy = needs["anim_files"]
    # Always include default if we have animated entities
    if needs.get("has_npcs") or (needs.get("has_player") and definition and
                                  definition.get("player", {}).get("type") == "animated"):
        anim_files_to_copy.add("character_complete.anim")

    for af in anim_files_to_copy:
        # Animation files can be at root or in resources/animated_characters/
        src = PHYXEL_ROOT / af
        if not src.exists():
            src = PHYXEL_ROOT / "resources" / "animated_characters" / af
        if not src.exists():
            src = PHYXEL_ROOT / af.replace("resources/animated_characters/", "")
        dst = output_dir / af
        if copy_file_safe(src, dst):
            files_copied += 1
        else:
            result["warnings"].append(f"Animation file not found: {af}")

    # ── 8. Dialogue files ───────────────────────────────────────────────
    if include_all_resources or needs.get("has_npcs"):
        dialogues_src = PHYXEL_ROOT / "resources" / "dialogues"
        if dialogues_src.exists():
            for f in dialogues_src.iterdir():
                if f.is_file() and f.suffix == ".json":
                    dst = output_dir / "resources" / "dialogues" / f.name
                    if copy_file_safe(f, dst):
                        files_copied += 1

    # ── 9. Sound files ──────────────────────────────────────────────────
    if include_all_resources:
        sounds_src = PHYXEL_ROOT / "resources" / "sounds"
        if sounds_src.exists():
            for f in sounds_src.iterdir():
                if f.is_file():
                    dst = output_dir / "resources" / "sounds" / f.name
                    if copy_file_safe(f, dst):
                        files_copied += 1

    # ── 10. Recipe files (story system) ─────────────────────────────────
    if include_all_resources or needs.get("has_story"):
        recipes_src = PHYXEL_ROOT / "resources" / "recipes"
        if recipes_src.exists():
            for root, dirs, files_list in os.walk(recipes_src):
                for f in files_list:
                    src = Path(root) / f
                    rel = src.relative_to(PHYXEL_ROOT)
                    dst = output_dir / rel
                    if copy_file_safe(src, dst):
                        files_copied += 1

    # ── 11. Pre-baked world database ───────────────────────────────────
    worlds_dir = output_dir / "worlds"
    worlds_dir.mkdir(exist_ok=True)

    if needs.get("is_multi_scene"):
        # Multi-scene: copy each scene's database
        for db_rel in needs.get("scene_databases", []):
            db_name = Path(db_rel).name
            # Try project dir first, then engine root
            candidates = []
            if is_game_project and project_dir:
                candidates.append(project_dir / "worlds" / db_name)
            candidates.append(PHYXEL_ROOT / "worlds" / db_name)
            copied = False
            for src in candidates:
                if src.exists():
                    dst = worlds_dir / db_name
                    if not (dst.exists() and os.path.samefile(src, dst)):
                        shutil.copy2(src, dst)
                    files_copied += 1
                    copied = True
                    break
            if not copied:
                result["warnings"].append(
                    f"Scene database '{db_name}' not found. "
                    "Run the engine and save each scene, or use --prebake-world."
                )
    elif world_db_path and world_db_path.exists():
        shutil.copy2(world_db_path, worlds_dir / "default.db")
        files_copied += 1
    elif is_game_project and (project_dir / "worlds" / "default.db").exists():
        # Game project may already have a pre-baked DB
        src_db = project_dir / "worlds" / "default.db"
        dst_db = worlds_dir / "default.db"
        if not (dst_db.exists() and os.path.samefile(src_db, dst_db)):
            shutil.copy2(src_db, dst_db)
        files_copied += 1
    else:
        result["warnings"].append(
            "No pre-baked world database. Game will start with empty world "
            "or regenerate from game.json (slow). Use --prebake-world to fix."
        )

    # ── 12. Engine config ───────────────────────────────────────────────
    engine_config = {
        "window": {
            "width": 1280,
            "height": 720,
            "title": window_title or name,
        },
        "rendering": {
            "max_chunk_render_distance": 96.0,
            "chunk_inclusion_distance": 128.0,
        },
    }
    # Only the engine-exe path needs the auto-load hint; game projects load directly
    if not is_game_project:
        engine_config["game"] = {"definition_file": "game.json"}
    config_path = output_dir / "engine.json"
    config_path.write_text(json.dumps(engine_config, indent=2), encoding="utf-8")
    files_copied += 1

    # ── 13. Launcher script ─────────────────────────────────────────────
    launcher = output_dir / f"Play {name}.bat"
    launcher.write_text(
        f'@echo off\r\ncd /d "%~dp0"\r\nstart "" "{game_exe_name}"\r\n',
        encoding="utf-8",
    )
    files_copied += 1

    # ── 14. Python startup script (only for engine-exe packaging) ──────
    if not is_game_project:
        scripts_dir = output_dir / "scripts"
        scripts_dir.mkdir(exist_ok=True)
        startup_py = scripts_dir / "startup.py"
        startup_py.write_text(
            '"""Auto-generated startup script for packaged game."""\n'
            "import phyxel\n"
            "\n"
            "# Game definition is auto-loaded by the engine from game.json\n"
            "# This script runs during engine init for any additional setup.\n"
            'phyxel.Logger.info("startup", "Packaged game starting...")\n',
            encoding="utf-8",
        )
        files_copied += 1

    # ── 15. Optional: MCP server for AI iteration ───────────────────────
    if include_mcp:
        mcp_src = PHYXEL_ROOT / "scripts" / "mcp"
        if mcp_src.exists():
            mcp_dst = output_dir / "scripts" / "mcp"
            mcp_dst.mkdir(parents=True, exist_ok=True)
            for f in mcp_src.iterdir():
                if f.is_file() and f.suffix == ".py":
                    shutil.copy2(f, mcp_dst / f.name)
                    files_copied += 1
            result["warnings"].append("MCP server included — requires 'pip install mcp httpx'")

    # ── 16. README ──────────────────────────────────────────────────────
    readme = output_dir / "README.md"
    readme.write_text(
        f"# {name}\n\n"
        f"A voxel game built with the Phyxel Engine.\n\n"
        f"## How to Play\n\n"
        f'Double-click `Play {name}.bat` or run `{game_exe_name}` directly.\n\n'
        f"## Controls\n\n"
        f"| Key | Action |\n"
        f"|-----|--------|\n"
        f"| WASD | Move |\n"
        f"| Mouse | Look around |\n"
        f"| Space | Jump |\n"
        f"| Shift | Sprint |\n"
        f"| K | Toggle character control |\n"
        f"| V | Toggle camera mode |\n"
        f"| Left Click | Attack / Break voxel |\n"
        f"| T | Spawn template object |\n"
        f"| ESC | Exit |\n\n"
        f"## Requirements\n\n"
        f"- Windows 10/11\n"
        f"- Vulkan-compatible GPU\n"
        f"- [Vulkan Runtime](https://vulkan.lunarg.com/sdk/home) (usually pre-installed with GPU drivers)\n",
        encoding="utf-8",
    )
    files_copied += 1

    # ── Done ────────────────────────────────────────────────────────────
    result["success"] = len(result["errors"]) == 0
    result["files_copied"] = files_copied
    return result


# ============================================================================
# CLI
# ============================================================================

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Package a Phyxel game into a standalone distributable directory."
    )
    parser.add_argument("name", help="Game name (used for executable and folder)")
    parser.add_argument(
        "--definition", "-d",
        help="Path to game definition JSON file",
        default=None,
    )
    parser.add_argument(
        "--from-engine",
        help="Export current game state from running engine (localhost:8090)",
        action="store_true",
    )
    parser.add_argument(
        "--project-dir",
        help="Path to a scaffolded game project directory (with its own CMakeLists.txt and compiled exe)",
        default=None,
    )
    parser.add_argument(
        "--prebake-world",
        help="Save the running engine's world to SQLite first, then include the pre-baked DB",
        action="store_true",
    )
    parser.add_argument(
        "--world-db",
        help="Path to an existing pre-baked worlds/default.db to include",
        default=None,
    )
    parser.add_argument(
        "--output", "-o",
        help="Output directory (default: ~/Documents/PhyxelProjects/<name>)",
        default=None,
    )
    parser.add_argument(
        "--config", "-c",
        help="Build configuration (Debug/Release, default: Debug)",
        default="Debug",
    )
    parser.add_argument(
        "--all-resources",
        help="Include all resources (not just those referenced by the definition)",
        action="store_true",
    )
    parser.add_argument(
        "--include-mcp",
        help="Include MCP server for AI agent iteration (dev only)",
        action="store_true",
    )
    parser.add_argument(
        "--title",
        help="Window title (default: game name)",
        default=None,
    )

    args = parser.parse_args()

    if args.output:
        output_dir = Path(args.output)
    else:
        docs = Path(os.environ.get("USERPROFILE", os.path.expanduser("~"))) / "Documents" / "PhyxelProjects" / args.name
        output_dir = docs
    output_dir = output_dir.resolve()

    definition = None
    definition_path = None

    if args.definition:
        definition_path = Path(args.definition).resolve()
        if not definition_path.exists():
            print(f"Error: Definition file not found: {definition_path}", file=sys.stderr)
            sys.exit(1)
        definition = json.loads(definition_path.read_text(encoding="utf-8-sig"))
    elif args.from_engine:
        try:
            import httpx
            resp = httpx.get("http://localhost:8090/api/game/export_definition", timeout=10)
            resp.raise_for_status()
            definition = resp.json()
            print(f"Exported game definition from running engine")
        except Exception as e:
            print(f"Error: Could not export from engine: {e}", file=sys.stderr)
            sys.exit(1)

    # Pre-bake world if requested
    world_db_path = None
    if args.prebake_world:
        print("Pre-baking world (saving engine state to SQLite)...")
        try:
            world_db_path = prebake_world()
            print(f"  World saved to {world_db_path}")
        except Exception as e:
            print(f"Error: Failed to pre-bake world: {e}", file=sys.stderr)
            sys.exit(1)
    elif args.world_db:
        world_db_path = Path(args.world_db).resolve()
        if not world_db_path.exists():
            print(f"Error: World database not found: {world_db_path}", file=sys.stderr)
            sys.exit(1)

    project_dir = Path(args.project_dir).resolve() if args.project_dir else None

    print(f"Packaging game '{args.name}' -> {output_dir}")
    if project_dir:
        print(f"  Source: game project at {project_dir}")
    result = package_game(
        name=args.name,
        output_dir=output_dir,
        definition=definition,
        definition_path=definition_path,
        config=args.config,
        include_all_resources=args.all_resources,
        include_mcp=args.include_mcp,
        window_title=args.title,
        project_dir=project_dir,
        world_db_path=world_db_path,
    )

    if result["warnings"]:
        for w in result["warnings"]:
            print(f"  Warning: {w}")

    if result["errors"]:
        for e in result["errors"]:
            print(f"  ERROR: {e}", file=sys.stderr)
        sys.exit(1)

    print(f"  {result['files_copied']} files copied")
    print(f"  Output: {output_dir}")
    print()
    exe_path = output_dir / (args.name + ".exe")
    print(f"To run: {exe_path}")


if __name__ == "__main__":
    main()
