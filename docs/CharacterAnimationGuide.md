# Character Animation Guide

This document outlines the standard animation states supported by the `AnimatedVoxelCharacter` system and the naming conventions required for animation files to be correctly recognized by the engine.

## Animation Pipeline Overview

1.  **Source Files**: Animations are typically sourced as `.fbx` or `.gltf` files (e.g., from Mixamo).
2.  **Import Tool**: The `tools/asset_pipeline/batch_import_anims.py` script combines a skin (T-Pose) and multiple animation files into a single `.anim` binary file.
3.  **Runtime**: The engine loads the `.anim` file and maps internal state names to the animation names found in the file.

## Naming Conventions

The engine performs a case-insensitive search for animations. It first looks for an **exact match**, and if that fails, it looks for a **partial match** (substring).

To ensure your animations are picked up correctly, rename your source files (e.g., `walk.fbx`, `jump.fbx`) to match the **Recommended Filename** column below before running the import script.

### Movement States

| State | Internal Key | Recommended Filename | Description |
| :--- | :--- | :--- | :--- |
| **Idle** | `idle` | `idle.fbx` | Standing still. |
| **Start Walk** | `start_walking` | `start_walking.fbx` | Transition from Idle to Walk (optional, engine falls back to Walk). |
| **Walk** | `walk` | `walk.fbx` | Standard forward movement. |
| **Run** | `run` | `run.fbx` | Faster forward movement. |
| **Fast Run** | `fast_run` | `fast_run.fbx` | High-speed sprinting (triggered if speed > 6.0). |
| **Crouch Walk** | `crouched_walking` | `crouched_walking.fbx` | Moving while crouched. |

### Action States

| State | Internal Key | Recommended Filename | Description |
| :--- | :--- | :--- | :--- |
| **Jump** | `jump` | `jump.fbx` | Initial jump impulse (going up). |
| **Fall** | `jump_down` | `jump_down.fbx` | Falling loop (going down). |
| **Land** | `landing` | `landing.fbx` | Impact with ground. |
| **Attack** | `attack` | `attack.fbx` | Primary action/attack. |

### Stance Transitions

| State | Internal Key | Recommended Filename | Description |
| :--- | :--- | :--- | :--- |
| **Crouch** | `standing_to_crouched` | `standing_to_crouched.fbx` | Transition down to crouch. Also used for Crouch Idle currently. |
| **Stand Up** | `crouch_to_stand` | `crouch_to_stand.fbx` | Transition up from crouch. |

### Directional / Strafing

| State | Internal Key | Recommended Filename | Description |
| :--- | :--- | :--- | :--- |
| **Turn Left** | `left_turn` | `left_turn.fbx` | Rotating left in place. |
| **Turn Right** | `right_turn` | `right_turn.fbx` | Rotating right in place. |
| **Strafe Left (Run)** | `left_strafe` | `left_strafe.fbx` | Fast sidestepping (Run speed). |
| **Strafe Right (Run)** | `right_strafe` | `right_strafe.fbx` | Fast sidestepping (Run speed). |
| **Strafe Left (Walk)** | `left_strafe_walk` | `left_strafe_walk.fbx` | Slow sidestepping (Walk speed). |
| **Strafe Right (Walk)** | `right_strafe_walk` | `right_strafe_walk.fbx` | Slow sidestepping (Walk speed). |
| **Walk Left (Diag)** | `left_strafe_walk` | `left_strafe_walk.fbx` | Diagonal movement (Forward + Left). Often same as Strafe Walk. |
| **Walk Right (Diag)** | `right_strafe_walk` | `right_strafe_walk.fbx` | Diagonal movement (Forward + Right). Often same as Strafe Walk. |

## Importing Animations

To create a compatible character asset:

1.  **Gather Files**: Collect your `.fbx` files in a single folder. Rename them according to the table above.
2.  **Identify Skin**: Ensure you have a T-Pose or Bind-Pose file (e.g., `t_pose.fbx`) for the mesh geometry.
3.  **Run Script**:
    ```powershell
    python tools/asset_pipeline/batch_import_anims.py "path/to/fbx/folder" --out "resources/my_character.anim" --skin "path/to/t_pose.fbx"
    ```

## In-Engine Anim Editor

For visual bone resizing without touching code, use the built-in anim editor:

```powershell
phyxel.exe --anim-editor resources/animated_characters/humanoid.anim
```

The editor opens a flat scene with the character loaded. An ImGui panel on the right provides:

- **Preview Animation** — a dropdown + Prev/Next buttons to cycle through all clips.
- **Bone Scales** — a slider (0.1× – 3.0×) per body bone; drag to resize the bone's voxel boxes live.
- **Reset All Scales** — reloads the original sizes from the file.
- **Save Model (Ctrl+S)** — rewrites the `MODEL` section of the `.anim` file with the new box sizes while leaving the skeleton hierarchy and all animation data untouched.

Only body-relevant bones (Hips, Spine, Neck, Head, Shoulder, Arm, ForeArm, Hand, UpLeg, Leg, Foot) appear in the list; finger, toe, and end-effector bones are filtered out.

---

## Fine-Tuning

If animations look incorrect in-game (e.g., character floating during a jump, or rotated 90 degrees), you can apply offsets in `src/scene/AnimatedVoxelCharacter.cpp` inside the `configureAnimationFixes()` function.

```cpp
void AnimatedVoxelCharacter::configureAnimationFixes() {
    // Fix Rotation (e.g., if model faces sideways)
    // animationRotationOffsets["walk"] = -90.0f;

    // Fix Position (e.g., if model floats too high during fall)
    // animationPositionOffsets["jump_down"] = glm::vec3(0.0f, -0.5f, 0.0f);
}
```
