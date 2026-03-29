#!/usr/bin/env python3
"""
Batch-import Mixamo FBX animations into humanoid.anim.

Handles:
  1. FBX → .anim conversion via extract_animation.py
  2. Bone ID remapping (source has RootNode as bone 0, target doesn't)
  3. Clip renaming to FSM-compatible names
  4. Merging into the master humanoid.anim file
"""
import os
import sys
import subprocess
import json
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from anim_editor import parse_anim_file, write_anim_file, Clip, Channel

HUMANOID_FILE = "resources/animated_characters/humanoid.anim"
IMPORT_DIR = "resources/mixamo_imports"
EXTRACT_SCRIPT = "tools/asset_pipeline/extract_animation.py"

# Map FBX filenames (without extension) → desired clip names in humanoid.anim
CLIP_NAME_MAP = {
    "Punching": "attack",
    "Boxing": "boxing",
    "Elbow Punch": "elbow_punch",
    "Headbutt": "headbutt",
    "Body Block": "body_block",
    "Standing Melee Attack Downward": "melee_attack_down",
    "Standing Melee Attack Horizontal": "melee_attack_horizontal",
    "Start Walking": "start_walking",
    "Talking": "talk",
    "Taunt": "taunt",
    "Waving": "wave",
}


def convert_fbx_to_anim(fbx_path, output_anim_path):
    """Convert a single FBX file to .anim using extract_animation.py."""
    cmd = [
        sys.executable, EXTRACT_SCRIPT,
        str(fbx_path), str(output_anim_path),
        "--style", "box"
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"  ERROR converting {fbx_path.name}:")
        print(f"  {result.stderr}")
        return False
    return True


def build_bone_remap(source_af, target_af):
    """Build a bone ID remap table: source_bone_id → target_bone_id.
    
    Matches by bone name. Source may have extra bones (like RootNode)
    that don't exist in the target — those channels get dropped.
    """
    # Build target name→id map
    target_name_to_id = {}
    for i, bone in enumerate(target_af.bones):
        target_name_to_id[bone.name] = i
    
    # Build source_id → target_id map
    remap = {}
    for i, bone in enumerate(source_af.bones):
        if bone.name in target_name_to_id:
            remap[i] = target_name_to_id[bone.name]
        # else: bone not in target (e.g., RootNode), skip
    
    return remap


def remap_clip(clip, bone_remap, new_name):
    """Create a new clip with remapped bone IDs and a new name."""
    remapped = Clip(name=new_name, duration=clip.duration, speed=clip.speed)
    dropped = 0
    for ch in clip.channels:
        new_id = bone_remap.get(ch.bone_id)
        if new_id is None:
            dropped += 1
            continue
        new_ch = Channel(bone_id=new_id)
        new_ch.pos_keys = ch.pos_keys[:]
        new_ch.rot_keys = ch.rot_keys[:]
        new_ch.scale_keys = ch.scale_keys[:]
        remapped.channels.append(new_ch)
    
    if dropped > 0:
        print(f"    Dropped {dropped} channels (unmapped bones like RootNode)")
    
    return remapped


def main():
    import_dir = Path(IMPORT_DIR)
    fbx_files = sorted(import_dir.glob("*.fbx"))
    
    if not fbx_files:
        print(f"No FBX files found in {IMPORT_DIR}")
        return
    
    print(f"Found {len(fbx_files)} FBX files in {IMPORT_DIR}")
    print(f"Target: {HUMANOID_FILE}\n")
    
    # Load master file
    master = parse_anim_file(HUMANOID_FILE)
    print(f"Master has {len(master.clips)} clips, {len(master.bones)} bones\n")
    
    imported_count = 0
    
    for fbx_path in fbx_files:
        stem = fbx_path.stem  # Filename without extension
        clip_name = CLIP_NAME_MAP.get(stem, stem.lower().replace(" ", "_"))
        
        print(f"--- {fbx_path.name} → '{clip_name}' ---")
        
        # Step 1: Convert FBX → temp .anim
        temp_anim = import_dir / f"temp_{stem.lower().replace(' ', '_')}.anim"
        
        if not convert_fbx_to_anim(fbx_path, temp_anim):
            print(f"  SKIPPED (conversion failed)\n")
            continue
        
        # Step 2: Load the temp .anim and extract the clip
        try:
            source = parse_anim_file(str(temp_anim))
        except Exception as e:
            print(f"  SKIPPED (parse failed: {e})\n")
            continue
        
        if not source.clips:
            print(f"  SKIPPED (no clips found)\n")
            continue
        
        # Step 3: Build bone remap
        bone_remap = build_bone_remap(source, master)
        mapped_count = len(bone_remap)
        total_source = len(source.bones)
        print(f"  Bone mapping: {mapped_count}/{total_source} bones matched")
        
        # Step 4: Remap and import the clip
        src_clip = source.clips[0]  # Mixamo FBX files have one clip each
        print(f"  Source clip: '{src_clip.name}' ({src_clip.duration:.3f}s, {len(src_clip.channels)} channels)")
        
        remapped_clip = remap_clip(src_clip, bone_remap, clip_name)
        
        # Remove old version if exists
        master.remove_clip(clip_name)
        master.clips.append(remapped_clip)
        
        print(f"  Imported as '{clip_name}' ({remapped_clip.duration:.3f}s, {len(remapped_clip.channels)} channels)")
        imported_count += 1
        
        # Clean up temp file
        try:
            temp_anim.unlink()
            # Also clean up any .glb file created by FBX2glTF
            glb_file = import_dir / f"{stem}.glb"
            if glb_file.exists():
                glb_file.unlink()
        except Exception:
            pass
        
        print()
    
    # Step 5: Write the updated master file
    if imported_count > 0:
        write_anim_file(master, HUMANOID_FILE)
        print(f"=== Imported {imported_count} clips into {HUMANOID_FILE} ===")
        print(f"Master now has {len(master.clips)} clips")
    else:
        print("No clips were imported.")


if __name__ == "__main__":
    main()
