# Object Template System

The Object Template System allows for importing 3D models (OBJ/STL/PLY) into the voxel world as optimized voxel structures. It supports both static placement (merged into chunks) and dynamic spawning (physics-enabled).

## Tooling: `obj_to_template.py`

Located in `tools/obj_to_template.py`, this script converts standard 3D mesh files into Phyxel's custom voxel template format (`.voxel`).

### Usage

```bash
python tools/obj_to_template.py input_model.obj output_template.voxel [options]
```

### Options

- `--size <float>`: Target size in World Cubes (default: 5.0). Scales the model to fit within this bounding box.
- `--material <name>`: Material name to assign to voxels (default: "Stone").
- `--resolution <mode>`: Voxel resolution strategy.
  - `auto`: (Default) Adaptive. Uses Cubes (1x1x1) for bulk volume, Subcubes (1/3) for detail, and Microcubes (1/9) for fine detail.
  - `cube`: Forces 1x1x1 resolution.
  - `subcube`: Forces 1/3 resolution.
  - `microcube`: Forces 1/9 resolution.
- `--hollow`: If set, the interior of the model will NOT be filled (keeps original voxelization). Default behavior fills holes to create solid objects.
- `--fill-threshold <float>`: Threshold (0.0-1.0) for optimization. Determines how full a parent voxel must be to be promoted to a larger voxel type.
  - Example: If a Cube contains enough Microcubes to meet the threshold, it is converted into a single solid Cube voxel, saving memory and rendering cost.
- `--thicken <int>`: Applies binary dilation (thickening) to the voxel grid. Useful for thin objects (like leaves) to make them thick enough to be optimized into Subcubes.
- `--shell`: Forces the generation of a hollow shell from the outer surface. Calculates `Solid Volume - Inner Volume`.
- `--shell-thickness <int>`: Wall thickness (in voxels) for shell generation (default: 1).

### Output & Performance

The tool now uses vectorized `numpy` operations for high-speed processing of large models.
It outputs a **Compression Ratio** (avg voxels per primitive) to help you tune parameters.

### Dependencies
- `trimesh`
- `numpy`
- `scipy` (for robust hole filling and morphological operations)

## Engine Integration: `ObjectTemplateManager`

The `ObjectTemplateManager` handles loading templates and spawning them into the world.

### Sequential Spawning

To prevent frame rate drops when spawning large objects (which may contain thousands of voxels), the system supports **Sequential Spawning**.

- **Method**: `spawnTemplateSequentially(name, position, isStatic)`
- **Behavior**: Voxels are placed in batches over multiple frames.
- **Configuration**:
  - `setSpawnSpeed(int voxelsPerFrame)`: Controls how many voxels are placed per frame.
  - Default: 200 voxels/frame.

### Performance Warning: Dynamic Objects

**⚠️ CRITICAL WARNING**: Spawning complex templates as **Dynamic** objects (`isStatic=false`) can cause severe performance issues or application hangs.

- **Static Spawning (`isStatic=true`)**: Highly optimized. Voxels are merged into the chunk mesh and rendered efficiently. Safe for large objects (thousands of voxels).
- **Dynamic Spawning (`isStatic=false`)**: **Expensive**. Each voxel in the template becomes an individual physics body with its own collision detection overhead.
  - **Risk**: Spawning a template with high voxel counts (e.g., >500 voxels, especially microcubes) as dynamic will freeze the engine while it initializes thousands of physics bodies.
  - **Recommendation**: Only use dynamic spawning for simple objects (e.g., < 100 voxels), debris, or small interactive props. For large structures, always use static spawning.

### Automatic Chunk Creation

When spawning static templates, the manager automatically detects if target chunks exist. If a template extends into empty space (e.g., high in the air), the necessary chunks are created on-the-fly to ensure the object is fully rendered.

## Controls

- **T**: Spawn "my_model" sequentially at the cursor location (or in front of player).
- **Shift + T**: Spawn "my_model" as dynamic physics objects.
- **[**: Decrease spawn speed.
- **]**: Increase spawn speed.
