# Sample Game Definitions

These JSON files are sample game definitions for creating games with the Phyxel engine. They can be passed to `tools/create_project.py` or loaded via the MCP `load_game_definition` tool.

## Files

| File | Description |
|------|-------------|
| `testing_baseline.json` | Standard test definition exercising all core features: Perlin terrain, physics player, NPC patrol, dialogue, story, structures. Use for regression testing. |
| `mountains_rpg.json` | Mountains-themed RPG with multiple NPCs, story arcs, and structures. Demonstrates a fuller game setup. |
| `full_featured.json` | Comprehensive definition exercising every engine feature: player, camera, NPCs, structures, dialogue, and story. |

## Usage

Scaffold a new game project from a sample:
```powershell
python tools/create_project.py MyGame --game-definition samples/game_definitions/testing_baseline.json
```

Or load directly into the running engine via MCP:
```
load_game_definition { "definition": <contents of JSON file> }
```
