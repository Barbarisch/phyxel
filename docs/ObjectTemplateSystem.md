# Object Template System

The Object Template System allows for importing 3D models (OBJ/STL/PLY) into the voxel world as optimized voxel structures. It supports both static placement (merged into chunks) and dynamic spawning (physics-enabled).

## Tooling: `obj_to_template.py`

Located in `tools/obj_to_template.py`, this script converts standard 3D mesh files into Phyxel's custom voxel template format (`.txt`).

### Usage

```bash
python tools/obj_to_template.py input_model.obj output_template.txt [options]
```

### Options

- `--size <float>`: Target size in World Cubes (default: 5.0). Scales the model to fit within this bounding box.
- `--material <name>`: Material name to assign to voxels (default: "Stone").
- `--resolution <mode>`: Voxel resolution strategy.
  - `auto`: (Default) Adaptive. Uses Cubes (1x1x1) for bulk volume, Subcubes (1/3) for detail, and Microcubes (1/9) for fine detail.
  - `cube`: Forces 1x1x1 resolution.
  - `subcube`: Forces 1/3 resolution.
  - `microcube`: Forces 1/9 resolution.
- `--hollow`: If set, the interior of the model will NOT be filled. Default behavior fills holes to create solid objects.
- `--fill-threshold <float>`: Threshold (0.0-1.0) for optimization. Determines how full a parent voxel must be to be promoted to a larger voxel type.
  - Example: If a Cube contains enough Microcubes to meet the threshold, it is converted into a single solid Cube voxel, saving memory and rendering cost.

### Dependencies
- `trimesh`
- `numpy`
- `scipy` (for robust hole filling)

## Engine Integration: `ObjectTemplateManager`

The `ObjectTemplateManager` handles loading templates and spawning them into the world.

### Sequential Spawning

To prevent frame rate drops when spawning large objects (which may contain thousands of voxels), the system supports **Sequential Spawning**.

- **Method**: `spawnTemplateSequentially(name, position, isStatic)`
- **Behavior**: Voxels are placed in batches over multiple frames.
- **Configuration**:
  - `setSpawnSpeed(int voxelsPerFrame)`: Controls how many voxels are placed per frame.
  - Default: 200 voxels/frame.

### Automatic Chunk Creation

When spawning static templates, the manager automatically detects if target chunks exist. If a template extends into empty space (e.g., high in the air), the necessary chunks are created on-the-fly to ensure the object is fully rendered.

## Controls

- **T**: Spawn "my_model" sequentially at the cursor location (or in front of player).
- **Shift + T**: Spawn "my_model" as dynamic physics objects.
- **[**: Decrease spawn speed.
- **]**: Increase spawn speed.
