# /generate — Full Asset Generation Pipeline (Meshy route)

**BlockSmith was removed 2026-10-14** (owner: "not adequate for our uses"; `docs/WorldForgeV2Plan.md`
§5 M0 row 11). The model source for voxelized assets is now the owner's **Meshy AI** account
(§14 of that plan): text-to-3D / image-to-3D meshes (GLB/FBX/OBJ) that are voxelized into the
engine's tiers.

## Status

The import tool (`tools/meshy_import.py`: GLB → `trimesh` voxelize at the fine-item or furniture
micro grid → material map → `lint_voxel_detail.py` → `asset_index.py --validate` → in-engine
screenshot → catalog provenance `source: meshy`) is **planned, not built**. Until it lands:

- New props, furniture and items are authored with `/asset` (hand-written `.voxel`, sub-voxel
  tiers) or `tools/gen_items.py` / `tools/regen_furniture.py`, exactly as before.
- Meshy models are used as **reference** (study the form, author the voxel asset) — never placed
  as generator output (CLAUDE.md provenance rule).

## When the import tool exists, the pipeline is

1. Generate or download the model in Meshy (GLB).
2. `python tools/meshy_import.py <model.glb> --name <stem> --tier fine|furniture|architecture --material-map <json>`
3. `python tools/lint_voxel_detail.py resources/templates/<cat>/<stem>.voxel`
4. `python tools/asset_index.py --validate`
5. Spawn it (`spawn_item` / `spawn_template`), screenshot it at true scale, fix, repeat.
6. The catalog entry records `source: meshy`, the prompt/image and the date.
