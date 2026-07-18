#!/usr/bin/env python3
"""Import the horizontal melee-attack mocap FBX as the axe chop swing.

Replaces the generated `chop_1h` clip in humanoid.anim with the Mixamo
"Standing Melee Attack Horizontal" motion. The `# clip_meta: chop_1h ...`
header (meleeFamily=chop_axe, hitFrameFraction, interrupt flags) is
preserved verbatim by the parser round-trip; tune hitFrameFraction
separately if the new swing's impact lands elsewhere. Run from repo root:
    python tools/bin/import_chop.py
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))

from anim_editor import parse_anim_file, write_anim_file  # noqa: E402
import batch_import_mixamo as bim  # noqa: E402

HUMANOID = REPO / "resources" / "animated_characters" / "humanoid.anim"
IMPORT_DIR = REPO / "resources" / "mixamo_imports"

TARGETS = {
    "Standing Melee Attack Horizontal": "chop_1h",
}


def main():
    master = parse_anim_file(str(HUMANOID))
    print(f"master: {len(master.clips)} clips, {len(master.bones)} bones")
    imported = 0
    for stem, clip_name in TARGETS.items():
        fbx = IMPORT_DIR / f"{stem}.fbx"
        if not fbx.exists():
            print(f"  MISSING {fbx}")
            continue
        print(f"--- {fbx.name} -> '{clip_name}' ---")
        temp = IMPORT_DIR / f"temp_{clip_name}.anim"
        if not bim.convert_fbx_to_anim(fbx, temp):
            print("  conversion FAILED"); continue
        src = parse_anim_file(str(temp))
        if not src.clips:
            print("  no clips"); continue
        remap = bim.build_bone_remap(src, master)
        # Some Mixamo FBX export TWO animations — an empty "Take 001" plus the
        # real "mixamo.com" clip. Pick the richest (most channels), not clips[0].
        sc = max(src.clips, key=lambda c: len(c.channels))
        print(f"  source: '{sc.name}' {sc.duration:.3f}s {len(sc.channels)} ch; "
              f"bones matched {len(remap)}/{len(src.bones)}")
        rc = bim.remap_clip(sc, remap, clip_name)
        master.remove_clip(clip_name)
        master.clips.append(rc)
        print(f"  imported '{clip_name}' {rc.duration:.3f}s {len(rc.channels)} ch")
        imported += 1
        try:
            temp.unlink()
            glb = IMPORT_DIR / f"{stem}.glb"
            if glb.exists():
                glb.unlink()
        except Exception:
            pass
    if imported:
        write_anim_file(master, str(HUMANOID))
        print(f"=== wrote {imported} clip(s) into {HUMANOID} ({len(master.clips)} total) ===")
    else:
        print("nothing imported")


if __name__ == "__main__":
    main()
