# structure_pipeline (P0)

Pure-Python spec authoring + static validation for the functional structure/furniture/item
generation pipeline. No engine, no voxels — catch a bad design before realizing it.

Full design: [`docs/structure-generation/StructureGenerationPipeline.md`](../../docs/structure-generation/StructureGenerationPipeline.md).

## Modules

| File           | Purpose |
|----------------|---------|
| `spec.py`      | `ScaledSpec` base + `BuildingSpec` (rooms/portals/stairs/fixtures) dataclasses; `from_dict`/`to_dict`. |
| `scale.py`     | `ScaleCanon` — loads `resources/character_design_constraints.json` (character 1.751 cubes) and derives clearances (`ceiling_min`, `door_clear_min`, …). The ruler everything is sized against. |
| `validator.py` | `validate(spec, canon)` → `ValidationReport`: geometry (bounds/overlap/wall adjacency), function (entrance + reachability graph, stair linkage), and scale clearances. |
| `examples/`    | `house_good.json` (clean) and `house_broken.json` (multi-error) fixtures. |

## Use

```bash
# Validate a spec from the CLI (exit 0 = ok, 1 = errors)
python -m structure_pipeline structure_pipeline/examples/house_good.json   # run from tools/

# Run the tests
python tools/structure_pipeline/test_structure_pipeline.py
```

```python
from structure_pipeline import BuildingSpec, load_canon, validate
rep = validate(BuildingSpec.from_dict(spec_dict))   # canon loaded from disk by default
print(rep.summary());  rep.ok  # bool
```

## Conventions

- Dimensions are **cubes** (1 cube ≈ 1 m). No absolute literals — size relative to the canon.
- `rect = [x, z, w, d]` (XZ footprint, min corner + size). `Y` is up.
- Portal `pos = [x, z]` is the **min corner of the opening along its wall**; it runs `width` cubes
  along the wall direction.
- Connectivity: only `door`/`arch` portals and stairs are passable (windows are not). Every room on
  every story must be reachable from `"exterior"`.

## Next (P1)

Spec-driven realizer in the C++ `StructureGenerator` (`generateFromSpec`) that composes existing
primitives per room/portal/story and **places + registers functional doors** via `DoorManager`.
