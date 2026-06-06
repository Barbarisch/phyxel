---
name: phyxel-assets
description: Use when adding voxel objects, props, furniture, or buildings to a Phyxel game — spawning existing templates, searching the template catalog, or generating brand-new voxel models from a text prompt via BlockSmith. Invoke for "add a chair / tree / castle / barrel / generate a <thing>" tasks.
---

# Assets: templates & AI generation

## Spawning existing objects
- `spawn_template` — place a `.voxel` template at a position (static, or dynamic-physics).
- `list_templates` / `search_templates` / `list_generated_templates` — find what exists.
- In a game definition, use a structure: `{"type":"template","name":"tree.voxel","position":{...}}`.
- Stock templates include `tree.voxel`, `tree2.voxel`, `sphere.voxel`, and generated furniture/
  buildings cached in `resources/templates/` (catalog: `template_catalog.json`).

## Generating new models from text (BlockSmith)
Text prompt → LLM → `.bbmodel` → `.voxel`. Two routes:
- MCP: `generate_template`, `build_building`, `search_templates`, `list_generated_templates`.
- CLI (in the engine repo): `python tools/blocksmith_generate.py "a wooden chair" --name chair
  --size 2 --material Wood`; buildings via `--building --building-type tavern --style medieval
  --width 14 --depth 18 --stories 2 --materials '{"wall":"Stone","floor":"Wood","roof":"Wood"}'`.
  Sizes: furniture 2–3, buildings 8–15. Needs `PHYXEL_AI_API_KEY` or `ANTHROPIC_API_KEY`.
  Generated templates are cached permanently in `resources/templates/`.

## Placement tips
Templates drop with their origin at the given position — place on top of terrain (Y =
surface + 1). Use `spawn_template` then `screenshot` to check scale/orientation; furniture is
small (2–3 voxels), buildings span many chunks. `rotate_placed_object` / `move_placed_object`
to adjust; `list_placed_objects` to inspect.

Inspect a model before using it: `inspect_template` / `critique_template` (screenshots +
evaluation). Refine generations with `refine_template`.
