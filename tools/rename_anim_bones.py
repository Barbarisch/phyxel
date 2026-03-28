#!/usr/bin/env python3
"""
Rename bones in .anim files to use consistent English naming conventions.

Usage:
    python tools/rename_anim_bones.py <input.anim> [--dry-run]
    python tools/rename_anim_bones.py --all [--dry-run]

Overwrites the file in-place (unless --dry-run).
"""

import re
import sys
import os
from pathlib import Path

# ============================================================================
# Wolf bone name mapping (German -> English)
# ============================================================================
WOLF_BONES = {
    "Becken": "pelvis",
    "Bauch": "belly",
    "Bauch.001": "belly_upper",
    "Bauch.003": "shoulder_base",
    "Brust": "chest",
    "Hals": "neck",
    "Hals_fett": "neck_fat",
    "Hals_fett_end": "neck_fat_end",
    "Kopf.002": "head_base",
    "Kopf": "head",
    "Unterkiefer": "lower_jaw",
    "Maulunten": "mouth_lower",
    "Maulunten_end": "mouth_lower_end",
    "Mauloben": "mouth_upper",
    "Mauloben_end": "mouth_upper_end",
    "MundW_L": "mouth_corner_L",
    "MundW_L_end": "mouth_corner_L_end",
    "MundW_R": "mouth_corner_R",
    "MundW_R_end": "mouth_corner_R_end",
    "Aug_R": "eye_R",
    "Aug_R_end": "eye_R_end",
    "aug_L": "eye_L",
    "aug_L_end": "eye_L_end",
    "Ohr_L": "ear_L",
    "Ohr_L_end": "ear_L_end",
    "Ohr_R": "ear_R",
    "Ohr_R_end": "ear_R_end",
    "Braun_O_L": "eyebrow_L",
    "Braun_O_L_end": "eyebrow_L_end",
    "Braun_O_R": "eyebrow_R",
    "Braun_O_R_end": "eyebrow_R_end",
    "aug_lied_O_L": "eyelid_upper_L",
    "aug_lied_O_L_end": "eyelid_upper_L_end",
    "aug_lied_O_R": "eyelid_upper_R",
    "aug_lied_O_R_end": "eyelid_upper_R_end",
    "aug_lied_U_L": "eyelid_lower_L",
    "aug_lied_U_L_end": "eyelid_lower_L_end",
    "aug_lied_U_R": "eyelid_lower_R",
    "aug_lied_U_R_end": "eyelid_lower_R_end",
    "Schalterplatte_L": "shoulder_plate_L",
    "Schalterplatte_R": "shoulder_plate_R",
    "Oberarm_L": "upper_leg_front_L",
    "Oberarm_R": "upper_leg_front_R",
    "Unterarm_L": "lower_leg_front_L",
    "Unterarm_R": "lower_leg_front_R",
    "Vorderpfote_L": "front_paw_L",
    "Vorderpfote_L_end": "front_paw_L_end",
    "Vorderpfote_R": "front_paw_R",
    "Vorderpfote_R_end": "front_paw_R_end",
    "Schwanz": "tail",
    "Schwanz.001": "tail_1",
    "Schwanz.002": "tail_2",
    "Schwanz.003": "tail_3",
    "Schwanz.003_end": "tail_3_end",
    "Oberschenkel_L": "upper_leg_rear_L",
    "Oberschenkel_R": "upper_leg_rear_R",
    "Unterschenkel_L": "lower_leg_rear_L",
    "Unterschenkel_R": "lower_leg_rear_R",
    "Pfote1_L": "rear_ankle_L",
    "Pfote1_R": "rear_ankle_R",
    "Pfote2_L": "rear_paw_L",
    "Pfote2_L_end": "rear_paw_L_end",
    "Pfote2_R": "rear_paw_R",
    "Pfote2_R_end": "rear_paw_R_end",
}

# ============================================================================
# Spider bone name mapping (abbreviated -> English)
# ============================================================================
SPIDER_BONES = {
    "Body.003": "root",
    "Body.002": "cephalothorax",
    "Body": "thorax",
    "Body2": "abdomen",
    "Body2_end": "abdomen_end",
    # Leg pair 1 (front)
    "v_B1_L": "leg1_coxa_L",
    "v_B2_L": "leg1_femur_L",
    "v_B3_L": "leg1_tibia_L",
    "v_B3_L_end": "leg1_tibia_L_end",
    "v_B1_R": "leg1_coxa_R",
    "v_B2_R": "leg1_femur_R",
    "v_B3_R": "leg1_tibia_R",
    "v_B3_R_end": "leg1_tibia_R_end",
    # Leg pair 2
    "v2_B1_L": "leg2_coxa_L",
    "v2_B2_L": "leg2_femur_L",
    "v2_B3_L": "leg2_tibia_L",
    "v2_B3_L_end": "leg2_tibia_L_end",
    "v2_B1_R": "leg2_coxa_R",
    "v2_B2_R": "leg2_femur_R",
    "v2_B3_R": "leg2_tibia_R",
    "v2_B3_R_end": "leg2_tibia_R_end",
    # Leg pair 3
    "v3_B1_L": "leg3_coxa_L",
    "v3_B2_L": "leg3_femur_L",
    "v3_B3_L": "leg3_tibia_L",
    "v3_B3_L_end": "leg3_tibia_L_end",
    "v3_B1_R": "leg3_coxa_R",
    "v3_B2_R": "leg3_femur_R",
    "v3_B3_R": "leg3_tibia_R",
    "v3_B3_R_end": "leg3_tibia_R_end",
    # Leg pair 4 (rear)
    "v4_B1_L": "leg4_coxa_L",
    "v4_B2_L": "leg4_femur_L",
    "v4_B3_L": "leg4_tibia_L",
    "v4_B3_L_end": "leg4_tibia_L_end",
    "v4_B1_R": "leg4_coxa_R",
    "v4_B2_R": "leg4_femur_R",
    "v4_B3_R": "leg4_tibia_R",
    "v4_B3_R_end": "leg4_tibia_R_end",
    # Pedipalps / chelicerae
    "FU_1_L": "pedipalp_base_L",
    "FU_2_L": "pedipalp_mid_L",
    "FU_3_L": "pedipalp_tip_L",
    "FU_3_L_end": "pedipalp_tip_L_end",
    "FU_1_R": "pedipalp_base_R",
    "FU_2_R": "pedipalp_mid_R",
    "FU_3_R": "pedipalp_tip_R",
    "FU_3_R_end": "pedipalp_tip_R_end",
    # Fangs
    "Zahn_L": "fang_L",
    "Zahn_L_end": "fang_L_end",
    "Zahn_R": "fang_R",
    "Zahn_R_end": "fang_R_end",
    # IK targets (leg tips pinned to world)
    "v_B3_L.001": "ik_leg1_L",
    "v_B3_L.001_end": "ik_leg1_L_end",
    "v_B3_R.001": "ik_leg1_R",
    "v_B3_R.001_end": "ik_leg1_R_end",
    "v2_B3_L.001": "ik_leg2_L",
    "v2_B3_L.001_end": "ik_leg2_L_end",
    "v2_B3_R.001": "ik_leg2_R",
    "v2_B3_R.001_end": "ik_leg2_R_end",
    "v3_B3_L.001": "ik_leg3_L",
    "v3_B3_L.001_end": "ik_leg3_L_end",
    "v3_B3_R.001": "ik_leg3_R",
    "v3_B3_R.001_end": "ik_leg3_R_end",
    "v4_B3_L.001": "ik_leg4_L",
    "v4_B3_L.001_end": "ik_leg4_L_end",
    "v4_B3_R.001": "ik_leg4_R",
    "v4_B3_R.001_end": "ik_leg4_R_end",
}

# ============================================================================
# Dragon bone name mapping (German/mixed -> English)
# ============================================================================
DRAGON_BONES = {
    "pelvic_2": "pelvis",
    "pelvic_1": "pelvis_front",
    "Oberschenkel_L": "upper_leg_L",
    "Oberschenkel_R": "upper_leg_R",
    "kneecap_Act_L": "kneecap_L",
    "kneecap_Act_L_end": "kneecap_L_end",
    "kneecap_Act_R": "kneecap_R",
    "kneecap_Act_R_end": "kneecap_R_end",
    "lowerleg_muscle_L": "calf_muscle_L",
    "lowerleg_muscle_L_end": "calf_muscle_L_end",
    "lowerleg_muscle_R": "calf_muscle_R",
    "lowerleg_muscle_R_end": "calf_muscle_R_end",
    "paunch": "abdomen",
    "paunch.002": "belly",
    "paunch.002_end": "belly_end",
    "ACT_neck_4": "neck_4",
    "ACT_neck_3": "neck_3",
    "ACT_neck_2": "neck_2",
    "ACT_neck_1": "neck_1",
    "IK_head": "head_ik",
    "Bone": "jaw_base",
    "Bone.003": "snout",
    "ik_tongue": "tongue_ik",
    "ik_tongue_end": "tongue_ik_end",
    "Cont_tongue": "tongue_control",
    "Cont_tongue_end": "tongue_control_end",
    "Augen_Control": "eye_control",
    "IK_Target_Auge_L": "eye_target_L",
    "IK_Target_Auge_L_end": "eye_target_L_end",
    "IK_Target_Auge_R": "eye_target_R",
    "IK_Target_Auge_R_end": "eye_target_R_end",
    "IK_Auge_L": "eye_L",
    "IK_Auge_R": "eye_R",
    "Auge_Lens_L": "eye_lens_L",
    "Auge_Lens_L_end": "eye_lens_L_end",
    "Auge_Lens_R": "eye_lens_R",
    "Auge_Lens_R_end": "eye_lens_R_end",
    "Augenlid_oben_L": "eyelid_upper_L",
    "Augenlid_oben_L_end": "eyelid_upper_L_end",
    "Augenlid_oben_R": "eyelid_upper_R",
    "Augenlid_oben_R_end": "eyelid_upper_R_end",
    "Augenlid_unten_L": "eyelid_lower_L",
    "Augenlid_unten_L_end": "eyelid_lower_L_end",
    "Augenlid_unten_R": "eyelid_lower_R",
    "Augenlid_unten_R_end": "eyelid_lower_R_end",
    "Augenbraue_L": "eyebrow_L",
    "Augenbraue_L_end": "eyebrow_L_end",
    "Augenbraue_R": "eyebrow_R",
    "Augenbraue_R_end": "eyebrow_R_end",
    "ACT_neck_skin": "neck_skin",
    "ACT_neck_skin_end": "neck_skin_end",
    "Nase_L": "nostril_L",
    "Nase_L_end": "nostril_L_end",
    "Nase_R": "nostril_R",
    "Nase_R_end": "nostril_R_end",
    "mouth_Cont": "mouth_control",
    "lips_Cont": "lips_control",
    "lips_Cont_end": "lips_control_end",
    "ik_ACT_tail_5": "tail_ik",
    "ik_ACT_tail_5_end": "tail_ik_end",
    "ik_underarm_L": "forearm_L",
    "ik_underarm_L_end": "forearm_L_end",
    "ik_underarm_R": "forearm_R",
    "ik_underarm_R_end": "forearm_R_end",
    # Wing controls
    "w_C_L.001": "wing_control_outer_L",
    "w_C_L": "wing_control_L",
    "w_C_R.001": "wing_control_outer_R",
    "w_C_R": "wing_control_R",
    # Wing bones L
    "w1_L": "wing_root_L",
    "w2_L": "wing_2_L",
    "w3_L": "wing_3_L",
    "w3_L.001": "wing_tip_L",
    "w3_L.001_end": "wing_tip_L_end",
    "w4_L": "wing_4_L",
    "w5_L": "wing_5_L",
    "w6_L": "wing_6_L",
    "w6_L_end": "wing_6_L_end",
    "w7_L": "wing_7_L",
    "w8_L": "wing_8_L",
    "w9_L": "wing_9_L",
    "w9_L_end": "wing_9_L_end",
    # Wing bones R
    "w1_R": "wing_root_R",
    "w2_R": "wing_2_R",
    "w3_R": "wing_3_R",
    "w3_R.001": "wing_tip_R",
    "w3_R.001_end": "wing_tip_R_end",
    "w4_R": "wing_4_R",
    "w5_R": "wing_5_R",
    "w6_R": "wing_6_R",
    "w6_R_end": "wing_6_R_end",
    "w7_R": "wing_7_R",
    "w8_R": "wing_8_R",
    "w9_R": "wing_9_R",
    "w9_R_end": "wing_9_R_end",
    "cheek_muscle_L": "cheek_L",
    "cheek_muscle_L_end": "cheek_L_end",
    "cheek_muscle_R": "cheek_R",
    "cheek_muscle_R_end": "cheek_R_end",
}


def detect_skeleton_type(bone_names):
    """Detect which skeleton type based on bone names present."""
    name_set = set(bone_names)
    if "Becken" in name_set or "Schwanz" in name_set:
        return "wolf", WOLF_BONES
    if "Body.003" in name_set or "v_B1_L" in name_set or "Zahn_L" in name_set:
        return "spider", SPIDER_BONES
    if "pelvic_2" in name_set or "ACT_neck_4" in name_set or "w1_L" in name_set:
        return "dragon", DRAGON_BONES
    return None, {}


def rename_bones_in_file(filepath, dry_run=False):
    """Rename bones in a .anim file. Returns (type, rename_count)."""
    with open(filepath, "r", encoding="utf-8") as f:
        content = f.read()

    lines = content.split("\n")

    # Extract bone names from Bone lines
    bone_names = []
    for line in lines:
        if line.startswith("Bone "):
            parts = line.split()
            if len(parts) >= 3:
                bone_names.append(parts[2])

    skel_type, mapping = detect_skeleton_type(bone_names)
    if skel_type is None:
        print(f"  Skipping {filepath}: no known non-English bone names detected")
        return None, 0

    print(f"  Detected: {skel_type} skeleton ({len(bone_names)} bones)")

    # Build a set of renames that actually apply
    renames = {}
    for old_name, new_name in mapping.items():
        if old_name in bone_names:
            renames[old_name] = new_name

    if not renames:
        print(f"  No renames needed for {filepath}")
        return skel_type, 0

    # Also rename in Channel headers and any other bone-name references.
    # The .anim format references bones by ID (integer), not name,
    # in Channel, Box, PosKey, RotKey lines. Only "Bone" lines and
    # "ANIMATION" names (which use armature names, not bone names) need renaming.
    # But animation names sometimes embed the armature name, e.g.
    # "Spider_Armature|Attack" — leave those as-is.

    new_lines = []
    rename_count = 0
    for line in lines:
        if line.startswith("Bone "):
            parts = line.split(maxsplit=3)  # "Bone", id, name, rest
            if len(parts) >= 4:
                bone_name = parts[2]
                if bone_name in renames:
                    new_name = renames[bone_name]
                    line = f"Bone {parts[1]} {new_name} {parts[3]}"
                    rename_count += 1
            elif len(parts) == 3:
                # Edge case: Bone line with just "Bone id name" (no more data?)
                bone_name = parts[2]
                if bone_name in renames:
                    line = f"Bone {parts[1]} {renames[bone_name]}"
                    rename_count += 1
        new_lines.append(line)

    new_content = "\n".join(new_lines)

    if dry_run:
        print(f"  Would rename {rename_count} bones in {filepath}")
        for old, new in sorted(renames.items()):
            print(f"    {old} -> {new}")
    else:
        with open(filepath, "w", encoding="utf-8") as f:
            f.write(new_content)
        print(f"  Renamed {rename_count} bones in {filepath}")

    return skel_type, rename_count


def main():
    import argparse
    parser = argparse.ArgumentParser(description="Rename bones in .anim files to English")
    parser.add_argument("files", nargs="*", help=".anim files to process")
    parser.add_argument("--all", action="store_true", help="Process all non-humanoid .anim files")
    parser.add_argument("--dry-run", action="store_true", help="Preview changes without writing")
    args = parser.parse_args()

    if args.all:
        anim_dir = Path(__file__).parent.parent / "resources" / "animated_characters"
        targets = [
            anim_dir / "character_wolf.anim",
            anim_dir / "character_spider.anim",
            anim_dir / "character_spider2.anim",
            anim_dir / "character_spider3.anim",
            anim_dir / "character_dragon.anim",
        ]
    elif args.files:
        targets = [Path(f) for f in args.files]
    else:
        parser.print_help()
        return

    total = 0
    for path in targets:
        if not path.exists():
            print(f"  File not found: {path}")
            continue
        print(f"\nProcessing: {path.name}")
        stype, count = rename_bones_in_file(str(path), dry_run=args.dry_run)
        total += count

    print(f"\nTotal bones renamed: {total}")
    if args.dry_run:
        print("(dry run — no files were modified)")


if __name__ == "__main__":
    main()
