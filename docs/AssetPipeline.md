# Asset Pipeline Guide

This guide explains how to convert 3D models and animations into formats supported by the Phyxel engine.

## 1. Static Mesh Import (Voxel Templates)

The engine uses a custom voxel template format (`.txt`) to define static objects and physics props. The asset pipeline includes tools to convert standard 3D formats (`.obj`, `.bbmodel`) into these templates.

### Supported Formats
*   **Wavefront OBJ (`.obj`)**: Standard 3D mesh format.
*   **Blockbench (`.bbmodel`)**: Native format for Blockbench, popular for voxel art.

### Tools

#### `obj_to_template.py`
Converts a standard 3D mesh into a voxel template.

**Usage:**
```bash
python tools/asset_pipeline/obj_to_template.py <input_file> <output_file> [options]
```

**Options:**
*   `--size <float>`: Target size in game units (default: 5.0).
*   `--material <name>`: Material name to assign to voxels (e.g., "Stone", "Wood").
*   `--resolution <mode>`: Voxel resolution (`auto`, `cube`, `subcube`, `microcube`). Default is `auto`.
*   `--fill-threshold <0.0-1.0>`: Threshold to treat a block as solid (default: 1.0). Lower values (e.g., 0.95) allow for lossy compression.
*   `--optimize`: **(Recommended)** Enables grid alignment optimization to reduce primitive count.
*   `--hollow`: Keeps the interior empty (shell only).
*   `--shell`: Forces generation of a hollow shell from the outer surface.
*   `--shell-thickness <int>`: Thickness of the shell in micro-voxels.

**Example:**
```bash
python tools/asset_pipeline/obj_to_template.py models/castle.obj resources/templates/castle.txt --size 20 --optimize --fill-threshold 0.95
```

#### `bbmodel_to_template.py`
Converts a Blockbench model directly to a voxel template.

**Usage:**
```bash
python tools/asset_pipeline/bbmodel_to_template.py <input_file> <output_file> [options]
```
*Supports the same options as `obj_to_template.py`.*

#### `optimize_template.py`
Optimizes an **existing** template file (`.txt`) without needing the source model. Useful for legacy assets.

**Usage:**
```bash
python tools/asset_pipeline/optimize_template.py <input_file> <output_file> --fill-threshold 0.95
```

---

## 2. Character Animation Import

The engine uses a custom binary format (`.anim`) for animated characters. This file contains the voxel skin (T-Pose) and multiple animation sequences.

### Prerequisites
*   **FBX2glTF**: You must have the `FBX2glTF` tool installed or in your PATH to convert FBX files.
*   **Python Dependencies**: `pip install pygltflib numpy scipy trimesh`

### Workflow
1.  **Prepare Source Files**:
    *   Create a folder for your character (e.g., `my_character/`).
    *   Place the **Skin** (T-Pose) file in it (e.g., `skin.fbx` or `tpose.fbx`).
    *   Place **Animation** files in it. Rename them according to the [Naming Conventions](CharacterAnimationGuide.md) (e.g., `idle.fbx`, `walk.fbx`, `run.fbx`).
2.  **Run Import Tool**:
    Use `batch_import_anims.py` to process the folder.

**Usage:**
```bash
python tools/asset_pipeline/batch_import_anims.py <input_directory> <output_file.anim> [options]
```

**Options:**
*   `--skin <filename>`: Name of the file to use as the base skin (default: looks for `t-pose`, `skin`, or `idle`).
*   `--scale <float>`: Scale factor for the model (default: 1.0).
*   `--converter <path>`: Path to `FBX2glTF` executable (if not in PATH).

**Example:**
```bash
python tools/asset_pipeline/batch_import_anims.py raw_assets/knight resources/animated_characters/knight.anim --scale 0.1
```

### How it Works
1.  **Conversion**: Converts all `.fbx` files in the folder to `.glb` (glTF binary) using `FBX2glTF`.
2.  **Voxelization**: Voxelizes the skin mesh to create the character's voxel representation.
3.  **Extraction**: Extracts bone transforms for every frame of every animation.
4.  **Packaging**: Combines the voxel data, skeleton, and animation tracks into a single `.anim` file.

---

## 3. Directory Structure

To ensure the engine finds your assets, place them in the correct `resources/` subdirectories:

*   **Static Templates**: `resources/templates/`
    *   *Loaded by `ObjectTemplateManager`.*
*   **Animated Characters**: `resources/animated_characters/`
    *   *Loaded by `AnimatedVoxelCharacter`.*
*   **Textures**: `resources/textures/`
    *   *Used by the material system.*

## 4. Optimization Tips

*   **Grid Alignment**: Always use the `--optimize` flag when creating templates. It shifts the model to align with the 9x9x9 voxel grid, significantly reducing the number of physics bodies and render instances.
*   **Fill Threshold**: For organic shapes or noisy scans, use `--fill-threshold 0.9` or `0.95`. This allows "mostly full" blocks to be simplified into single cubes.
*   **Scale**: Ensure your input models are scaled correctly. 1 Unit in Phyxel = 1 Chunk Voxel (9x9x9 micro-voxels).
