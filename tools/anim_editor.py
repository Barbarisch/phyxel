#!/usr/bin/env python3
"""
Phyxel .anim File Editor — Read, create, modify, and merge animation files.

Usage:
  python tools/anim_editor.py list <file>                      — List clips in a .anim file
  python tools/anim_editor.py info <file> <clip>               — Show clip details
  python tools/anim_editor.py add <file> <clip.json>           — Add clip from JSON definition
  python tools/anim_editor.py remove <file> <clip_name>        — Remove a clip
  python tools/anim_editor.py merge <base> <overlay> -o out    — Merge clips from overlay into base
  python tools/anim_editor.py export <file> <clip> -o clip.json — Export clip as JSON
  python tools/anim_editor.py generate <template> -s <file> -o clip.json — Generate clip from template
  python tools/anim_editor.py bones <file>                     — List skeleton bones
  python tools/anim_editor.py mirror <file> <clip> <new_name>  — Mirror left↔right bones
  python tools/anim_editor.py scale-time <file> <clip> <factor> — Scale clip duration
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional


# ===========================================================================
# Data Structures
# ===========================================================================

@dataclass
class PosKey:
    time: float
    x: float
    y: float
    z: float

@dataclass
class RotKey:
    time: float
    x: float
    y: float
    z: float
    w: float

@dataclass
class ScaleKey:
    time: float
    x: float
    y: float
    z: float

@dataclass
class Channel:
    bone_id: int
    pos_keys: list[PosKey] = field(default_factory=list)
    rot_keys: list[RotKey] = field(default_factory=list)
    scale_keys: list[ScaleKey] = field(default_factory=list)

@dataclass
class Clip:
    name: str
    duration: float
    speed: Optional[float] = None
    channels: list[Channel] = field(default_factory=list)

@dataclass
class Bone:
    id: int
    name: str
    parent_id: int
    pos: tuple[float, float, float] = (0.0, 0.0, 0.0)
    rot: tuple[float, float, float, float] = (0.0, 0.0, 0.0, 1.0)
    scale: tuple[float, float, float] = (1.0, 1.0, 1.0)

@dataclass
class Box:
    bone_id: int
    size: tuple[float, float, float]
    offset: tuple[float, float, float]

@dataclass
class AnimFile:
    bones: list[Bone] = field(default_factory=list)
    boxes: list[Box] = field(default_factory=list)
    clips: list[Clip] = field(default_factory=list)
    bone_map: dict[str, int] = field(default_factory=dict)

    def get_clip(self, name: str) -> Optional[Clip]:
        for c in self.clips:
            if c.name == name:
                return c
        return None

    def remove_clip(self, name: str) -> bool:
        for i, c in enumerate(self.clips):
            if c.name == name:
                self.clips.pop(i)
                return True
        return False


# ===========================================================================
# Parser
# ===========================================================================

def parse_anim_file(path: str | Path) -> AnimFile:
    """Parse a .anim file into an AnimFile structure."""
    af = AnimFile()
    path = Path(path)

    with open(path, "r") as f:
        lines = f.readlines()

    i = 0
    while i < len(lines):
        line = lines[i].strip()

        if line == "SKELETON":
            i += 1
            # BoneCount N
            parts = lines[i].strip().split()
            bone_count = int(parts[1])
            i += 1
            for _ in range(bone_count):
                parts = lines[i].strip().split()
                # Bone <ID> <NAME> <PARENT> <PX> <PY> <PZ> <RX> <RY> <RZ> <RW> <SX> <SY> <SZ>
                bone = Bone(
                    id=int(parts[1]),
                    name=parts[2],
                    parent_id=int(parts[3]),
                    pos=(float(parts[4]), float(parts[5]), float(parts[6])),
                    rot=(float(parts[7]), float(parts[8]), float(parts[9]), float(parts[10])),
                    scale=(float(parts[11]), float(parts[12]), float(parts[13]))
                )
                af.bones.append(bone)
                af.bone_map[bone.name] = bone.id
                i += 1

        elif line == "MODEL":
            i += 1
            parts = lines[i].strip().split()
            box_count = int(parts[1])
            i += 1
            for _ in range(box_count):
                parts = lines[i].strip().split()
                # Box <BONE_ID> <SX> <SY> <SZ> <OX> <OY> <OZ>
                box = Box(
                    bone_id=int(parts[1]),
                    size=(float(parts[2]), float(parts[3]), float(parts[4])),
                    offset=(float(parts[5]), float(parts[6]), float(parts[7]))
                )
                af.boxes.append(box)
                i += 1

        elif line.startswith("ANIMATION"):
            clip_name = line.split(None, 1)[1] if len(line.split(None, 1)) > 1 else "unnamed"
            i += 1
            # Duration
            parts = lines[i].strip().split()
            duration = float(parts[1])
            i += 1

            # Optional Speed line
            speed = None
            parts = lines[i].strip().split()
            if parts[0] == "Speed":
                speed = float(parts[1])
                i += 1
                parts = lines[i].strip().split()

            # BoneChannelCount
            channel_count = int(parts[1])
            i += 1

            clip = Clip(name=clip_name, duration=duration, speed=speed)

            for _ in range(channel_count):
                parts = lines[i].strip().split()
                # Channel <BONE_ID> <POS_COUNT> <ROT_COUNT> <SCALE_COUNT>
                bone_id = int(parts[1])
                pos_count = int(parts[2])
                rot_count = int(parts[3])
                scale_count = int(parts[4])
                i += 1

                ch = Channel(bone_id=bone_id)

                for _ in range(pos_count):
                    parts = lines[i].strip().split()
                    ch.pos_keys.append(PosKey(float(parts[1]), float(parts[2]), float(parts[3]), float(parts[4])))
                    i += 1

                for _ in range(rot_count):
                    parts = lines[i].strip().split()
                    ch.rot_keys.append(RotKey(float(parts[1]), float(parts[2]), float(parts[3]), float(parts[4]), float(parts[5])))
                    i += 1

                for _ in range(scale_count):
                    parts = lines[i].strip().split()
                    ch.scale_keys.append(ScaleKey(float(parts[1]), float(parts[2]), float(parts[3]), float(parts[4])))
                    i += 1

                clip.channels.append(ch)

            af.clips.append(clip)
        else:
            i += 1

    return af


# ===========================================================================
# Writer
# ===========================================================================

def write_anim_file(af: AnimFile, path: str | Path):
    """Write an AnimFile to disk in .anim format."""
    path = Path(path)
    with open(path, "w", newline="\n") as f:
        # SKELETON
        f.write("SKELETON\n")
        f.write(f"BoneCount {len(af.bones)}\n")
        for b in af.bones:
            f.write(f"Bone {b.id} {b.name} {b.parent_id} "
                    f"{b.pos[0]} {b.pos[1]} {b.pos[2]} "
                    f"{b.rot[0]} {b.rot[1]} {b.rot[2]} {b.rot[3]} "
                    f"{b.scale[0]} {b.scale[1]} {b.scale[2]}\n")

        # MODEL
        f.write("MODEL\n")
        f.write(f"BoxCount {len(af.boxes)}\n")
        for box in af.boxes:
            f.write(f"Box {box.bone_id} {box.size[0]} {box.size[1]} {box.size[2]} "
                    f"{box.offset[0]} {box.offset[1]} {box.offset[2]}\n")

        # ANIMATIONS
        for clip in af.clips:
            f.write(f"ANIMATION {clip.name}\n")
            f.write(f"Duration {clip.duration}\n")
            if clip.speed is not None:
                f.write(f"Speed {clip.speed}\n")
            f.write(f"BoneChannelCount {len(clip.channels)}\n")
            for ch in clip.channels:
                f.write(f"Channel {ch.bone_id} {len(ch.pos_keys)} {len(ch.rot_keys)} {len(ch.scale_keys)}\n")
                for k in ch.pos_keys:
                    f.write(f"PosKey {k.time} {k.x} {k.y} {k.z}\n")
                for k in ch.rot_keys:
                    f.write(f"RotKey {k.time} {k.x} {k.y} {k.z} {k.w}\n")
                for k in ch.scale_keys:
                    f.write(f"ScaleKey {k.time} {k.x} {k.y} {k.z}\n")


# ===========================================================================
# Quaternion Utilities
# ===========================================================================

def quat_identity() -> tuple[float, float, float, float]:
    return (0.0, 0.0, 0.0, 1.0)

def quat_from_axis_angle(axis: tuple[float, float, float], angle_deg: float) -> tuple[float, float, float, float]:
    """Create quaternion from axis and angle (degrees)."""
    rad = math.radians(angle_deg)
    s = math.sin(rad / 2.0)
    c = math.cos(rad / 2.0)
    mag = math.sqrt(axis[0]**2 + axis[1]**2 + axis[2]**2)
    if mag < 1e-10:
        return quat_identity()
    ax = axis[0] / mag
    ay = axis[1] / mag
    az = axis[2] / mag
    return (ax * s, ay * s, az * s, c)

def quat_mul(q1: tuple, q2: tuple) -> tuple[float, float, float, float]:
    """Multiply two quaternions."""
    x1, y1, z1, w1 = q1
    x2, y2, z2, w2 = q2
    return (
        w1*x2 + x1*w2 + y1*z2 - z1*y2,
        w1*y2 - x1*z2 + y1*w2 + z1*x2,
        w1*z2 + x1*y2 - y1*x2 + z1*w2,
        w1*w2 - x1*x2 - y1*y2 - z1*z2
    )

def quat_normalize(q: tuple) -> tuple[float, float, float, float]:
    x, y, z, w = q
    mag = math.sqrt(x*x + y*y + z*z + w*w)
    if mag < 1e-10:
        return quat_identity()
    return (x/mag, y/mag, z/mag, w/mag)

def quat_slerp(q1: tuple, q2: tuple, t: float) -> tuple[float, float, float, float]:
    """Spherical linear interpolation between two quaternions."""
    x1, y1, z1, w1 = q1
    x2, y2, z2, w2 = q2
    dot = x1*x2 + y1*y2 + z1*z2 + w1*w2
    if dot < 0:
        x2, y2, z2, w2 = -x2, -y2, -z2, -w2
        dot = -dot
    if dot > 0.9995:
        # Linear interp for very close quats
        return quat_normalize((
            x1 + t*(x2-x1), y1 + t*(y2-y1),
            z1 + t*(z2-z1), w1 + t*(w2-w1)
        ))
    theta = math.acos(min(dot, 1.0))
    sin_theta = math.sin(theta)
    s1 = math.sin((1-t)*theta) / sin_theta
    s2 = math.sin(t*theta) / sin_theta
    return (s1*x1 + s2*x2, s1*y1 + s2*y2, s1*z1 + s2*z2, s1*w1 + s2*w2)

def quat_conjugate(q: tuple) -> tuple[float, float, float, float]:
    """Conjugate (inverse for unit quaternions)."""
    return (-q[0], -q[1], -q[2], q[3])


# ===========================================================================
# Keyframe Generation Helpers
# ===========================================================================

def make_rotation_keyframes(
    bone_id: int,
    base_rot: tuple[float, float, float, float],
    axis: tuple[float, float, float],
    angle_deg: float,
    duration: float,
    num_keys: int,
    wave: str = "sine"
) -> Channel:
    """Generate oscillating rotation keyframes for a bone.

    wave: "sine" (oscillate back/forth), "half" (rotate and return),
          "hold" (rotate and hold)
    """
    ch = Channel(bone_id=bone_id)
    for i in range(num_keys):
        t = (i / max(num_keys - 1, 1)) * duration
        if wave == "sine":
            factor = math.sin(t / duration * 2 * math.pi)
        elif wave == "half":
            factor = math.sin(t / duration * math.pi)
        elif wave == "hold":
            factor = min(t / (duration * 0.25), 1.0) if t < duration * 0.75 else max(1.0 - (t - duration * 0.75) / (duration * 0.25), 0.0)
        else:
            factor = 0.0

        rot_offset = quat_from_axis_angle(axis, angle_deg * factor)
        rot = quat_mul(base_rot, rot_offset)
        rot = quat_normalize(rot)
        ch.rot_keys.append(RotKey(t, rot[0], rot[1], rot[2], rot[3]))
    return ch

def make_position_keyframes(
    bone_id: int,
    base_pos: tuple[float, float, float],
    offset: tuple[float, float, float],
    duration: float,
    num_keys: int,
    wave: str = "sine"
) -> Channel:
    """Generate oscillating position keyframes for a bone."""
    ch = Channel(bone_id=bone_id)
    for i in range(num_keys):
        t = (i / max(num_keys - 1, 1)) * duration
        if wave == "sine":
            factor = math.sin(t / duration * 2 * math.pi)
        elif wave == "half":
            factor = math.sin(t / duration * math.pi)
        elif wave == "hold":
            factor = min(t / (duration * 0.25), 1.0) if t < duration * 0.75 else max(1.0 - (t - duration * 0.75) / (duration * 0.25), 0.0)
        else:
            factor = 0.0

        px = base_pos[0] + offset[0] * factor
        py = base_pos[1] + offset[1] * factor
        pz = base_pos[2] + offset[2] * factor
        ch.pos_keys.append(PosKey(t, px, py, pz))
    return ch

def make_static_channel(bone_id: int, pos: tuple = None, rot: tuple = None, scale: tuple = None, duration: float = 1.0) -> Channel:
    """Create a channel with a single keyframe (static pose)."""
    ch = Channel(bone_id=bone_id)
    if pos:
        ch.pos_keys.append(PosKey(0.0, pos[0], pos[1], pos[2]))
    if rot:
        ch.rot_keys.append(RotKey(0.0, rot[0], rot[1], rot[2], rot[3]))
    if scale:
        ch.scale_keys.append(ScaleKey(0.0, scale[0], scale[1], scale[2]))
    return ch


# ===========================================================================
# Clip Operations
# ===========================================================================

def mirror_clip(clip: Clip, left_right_map: dict[int, int], new_name: str) -> Clip:
    """Mirror an animation clip by swapping left/right bone channels.

    left_right_map: {left_bone_id: right_bone_id, ...}
    Rotation X and Z components are negated to mirror across the YZ plane.
    """
    mirrored = Clip(name=new_name, duration=clip.duration, speed=clip.speed)
    # Build full swap map (bidirectional)
    swap = {}
    for l, r in left_right_map.items():
        swap[l] = r
        swap[r] = l

    for ch in clip.channels:
        target_id = swap.get(ch.bone_id, ch.bone_id)
        new_ch = Channel(bone_id=target_id)
        for k in ch.pos_keys:
            new_ch.pos_keys.append(PosKey(k.time, -k.x, k.y, k.z))
        for k in ch.rot_keys:
            new_ch.rot_keys.append(RotKey(k.time, k.x, -k.y, -k.z, k.w))
        for k in ch.scale_keys:
            new_ch.scale_keys.append(ScaleKey(k.time, k.x, k.y, k.z))
        mirrored.channels.append(new_ch)
    return mirrored

def scale_clip_duration(clip: Clip, factor: float) -> Clip:
    """Scale clip duration and all keyframe times by a factor."""
    scaled = Clip(
        name=clip.name,
        duration=clip.duration * factor,
        speed=clip.speed / factor if clip.speed else None
    )
    for ch in clip.channels:
        new_ch = Channel(bone_id=ch.bone_id)
        for k in ch.pos_keys:
            new_ch.pos_keys.append(PosKey(k.time * factor, k.x, k.y, k.z))
        for k in ch.rot_keys:
            new_ch.rot_keys.append(RotKey(k.time * factor, k.x, k.y, k.z, k.w))
        for k in ch.scale_keys:
            new_ch.scale_keys.append(ScaleKey(k.time * factor, k.x, k.y, k.z))
        scaled.channels.append(new_ch)
    return scaled


# ===========================================================================
# JSON Clip Serialization
# ===========================================================================

def clip_to_json(clip: Clip) -> dict:
    """Convert a Clip to a JSON-serializable dict."""
    return {
        "name": clip.name,
        "duration": clip.duration,
        "speed": clip.speed,
        "channels": [
            {
                "bone_id": ch.bone_id,
                "pos_keys": [{"t": k.time, "x": k.x, "y": k.y, "z": k.z} for k in ch.pos_keys],
                "rot_keys": [{"t": k.time, "x": k.x, "y": k.y, "z": k.z, "w": k.w} for k in ch.rot_keys],
                "scale_keys": [{"t": k.time, "x": k.x, "y": k.y, "z": k.z} for k in ch.scale_keys],
            }
            for ch in clip.channels
        ]
    }

def clip_from_json(data: dict) -> Clip:
    """Create a Clip from a JSON dict."""
    clip = Clip(
        name=data["name"],
        duration=data["duration"],
        speed=data.get("speed")
    )
    for ch_data in data["channels"]:
        ch = Channel(bone_id=ch_data["bone_id"])
        for k in ch_data.get("pos_keys", []):
            ch.pos_keys.append(PosKey(k["t"], k["x"], k["y"], k["z"]))
        for k in ch_data.get("rot_keys", []):
            ch.rot_keys.append(RotKey(k["t"], k["x"], k["y"], k["z"], k["w"]))
        for k in ch_data.get("scale_keys", []):
            ch.scale_keys.append(ScaleKey(k["t"], k["x"], k["y"], k["z"]))
        clip.channels.append(ch)
    return clip


# ===========================================================================
# Humanoid Bone Maps (Mixamo rig — character_complete.anim)
# ===========================================================================

# Semantic name → Mixamo bone name
MIXAMO_BONES = {
    "hips": "mixamorig:Hips",
    "spine": "mixamorig:Spine",
    "spine1": "mixamorig:Spine1",
    "spine2": "mixamorig:Spine2",
    "neck": "mixamorig:Neck",
    "head": "mixamorig:Head",
    "head_top": "mixamorig:HeadTop_End",
    # Right arm
    "right_shoulder": "mixamorig:RightShoulder",
    "right_arm": "mixamorig:RightArm",
    "right_forearm": "mixamorig:RightForeArm",
    "right_hand": "mixamorig:RightHand",
    # Left arm
    "left_shoulder": "mixamorig:LeftShoulder",
    "left_arm": "mixamorig:LeftArm",
    "left_forearm": "mixamorig:LeftForeArm",
    "left_hand": "mixamorig:LeftHand",
    # Right leg
    "right_upleg": "mixamorig:RightUpLeg",
    "right_leg": "mixamorig:RightLeg",
    "right_foot": "mixamorig:RightFoot",
    "right_toe": "mixamorig:RightToeBase",
    # Left leg
    "left_upleg": "mixamorig:LeftUpLeg",
    "left_leg": "mixamorig:LeftLeg",
    "left_foot": "mixamorig:LeftFoot",
    "left_toe": "mixamorig:LeftToeBase",
}

def build_left_right_map(bone_map: dict[str, int]) -> dict[int, int]:
    """Build a left↔right bone ID map from a loaded skeleton's bone_map.

    Matches Mixamo bone names containing 'Left' with their 'Right' counterparts.
    """
    lr_map = {}
    left_bones = {name: bid for name, bid in bone_map.items() if "Left" in name}
    for left_name, left_id in left_bones.items():
        right_name = left_name.replace("Left", "Right")
        if right_name in bone_map:
            lr_map[left_id] = bone_map[right_name]
    return lr_map


# ===========================================================================
# CLI Commands
# ===========================================================================

def cmd_list(args):
    af = parse_anim_file(args.file)
    print(f"File: {args.file}")
    print(f"Bones: {len(af.bones)}, Boxes: {len(af.boxes)}, Clips: {len(af.clips)}")
    print()
    for i, clip in enumerate(af.clips):
        ch_count = len(clip.channels)
        total_keys = sum(len(c.pos_keys) + len(c.rot_keys) + len(c.scale_keys) for c in clip.channels)
        speed_str = f", speed={clip.speed}" if clip.speed else ""
        print(f"  [{i}] {clip.name}  (dur={clip.duration:.3f}s, channels={ch_count}, keys={total_keys}{speed_str})")

def cmd_info(args):
    af = parse_anim_file(args.file)
    clip = af.get_clip(args.clip)
    if not clip:
        print(f"Error: clip '{args.clip}' not found", file=sys.stderr)
        sys.exit(1)
    print(f"Clip: {clip.name}")
    print(f"Duration: {clip.duration:.4f}s")
    if clip.speed:
        print(f"Speed: {clip.speed}")
    print(f"Channels: {len(clip.channels)}")
    for ch in clip.channels:
        bone_name = "?"
        for b in af.bones:
            if b.id == ch.bone_id:
                bone_name = b.name
                break
        print(f"  Bone {ch.bone_id} ({bone_name}): "
              f"pos={len(ch.pos_keys)}, rot={len(ch.rot_keys)}, scale={len(ch.scale_keys)}")

def cmd_add(args):
    af = parse_anim_file(args.file)
    with open(args.clip_json, "r") as f:
        data = json.load(f)

    # Support single clip or array of clips
    clips = data if isinstance(data, list) else [data]
    for clip_data in clips:
        clip = clip_from_json(clip_data)
        # Replace existing clip with same name
        af.remove_clip(clip.name)
        af.clips.append(clip)
        print(f"Added clip: {clip.name} ({clip.duration:.3f}s, {len(clip.channels)} channels)")

    write_anim_file(af, args.file)
    print(f"Written: {args.file}")

def cmd_remove(args):
    af = parse_anim_file(args.file)
    if af.remove_clip(args.clip_name):
        write_anim_file(af, args.file)
        print(f"Removed clip '{args.clip_name}' from {args.file}")
    else:
        print(f"Error: clip '{args.clip_name}' not found", file=sys.stderr)
        sys.exit(1)

def cmd_merge(args):
    base = parse_anim_file(args.base)
    overlay = parse_anim_file(args.overlay)
    output = args.output or args.base

    for clip in overlay.clips:
        base.remove_clip(clip.name)
        base.clips.append(clip)
        print(f"Merged clip: {clip.name}")

    write_anim_file(base, output)
    print(f"Written: {output}")

def cmd_export(args):
    af = parse_anim_file(args.file)
    clip = af.get_clip(args.clip)
    if not clip:
        print(f"Error: clip '{args.clip}' not found", file=sys.stderr)
        sys.exit(1)
    data = clip_to_json(clip)
    output = args.output or f"{args.clip}.json"
    with open(output, "w") as f:
        json.dump(data, f, indent=2)
    print(f"Exported: {output}")

def cmd_bones(args):
    af = parse_anim_file(args.file)
    print(f"Skeleton: {len(af.bones)} bones")
    for b in af.bones:
        parent_name = "ROOT"
        if b.parent_id >= 0:
            for p in af.bones:
                if p.id == b.parent_id:
                    parent_name = p.name
                    break
        print(f"  [{b.id:2d}] {b.name:<40s} parent={parent_name}")

def cmd_mirror(args):
    af = parse_anim_file(args.file)
    clip = af.get_clip(args.clip)
    if not clip:
        print(f"Error: clip '{args.clip}' not found", file=sys.stderr)
        sys.exit(1)
    lr_map = build_left_right_map(af.bone_map)
    if not lr_map:
        print("Warning: no left/right bone pairs found", file=sys.stderr)
    mirrored = mirror_clip(clip, lr_map, args.new_name)
    af.remove_clip(args.new_name)
    af.clips.append(mirrored)
    write_anim_file(af, args.file)
    print(f"Added mirrored clip '{args.new_name}' to {args.file}")

def cmd_scale_time(args):
    af = parse_anim_file(args.file)
    clip = af.get_clip(args.clip)
    if not clip:
        print(f"Error: clip '{args.clip}' not found", file=sys.stderr)
        sys.exit(1)
    scaled = scale_clip_duration(clip, args.factor)
    # Replace in-place
    for i, c in enumerate(af.clips):
        if c.name == args.clip:
            af.clips[i] = scaled
            break
    write_anim_file(af, args.file)
    print(f"Scaled '{args.clip}' by {args.factor}x → {scaled.duration:.3f}s")

def cmd_rename(args):
    af = parse_anim_file(args.file)
    clip = af.get_clip(args.old_name)
    if not clip:
        print(f"Error: clip '{args.old_name}' not found", file=sys.stderr)
        sys.exit(1)
    if af.get_clip(args.new_name):
        print(f"Error: clip '{args.new_name}' already exists", file=sys.stderr)
        sys.exit(1)
    clip.name = args.new_name
    write_anim_file(af, args.file)
    print(f"Renamed '{args.old_name}' → '{args.new_name}'")

def cmd_batch_rename(args):
    """Rename multiple clips using a JSON mapping file.

    JSON format: {"OldName": "new_name", "OtherOld": "other_new", ...}
    """
    af = parse_anim_file(args.file)
    with open(args.mapping, "r") as f:
        mapping = json.load(f)
    renamed = 0
    for old_name, new_name in mapping.items():
        clip = af.get_clip(old_name)
        if clip:
            clip.name = new_name
            print(f"  {old_name} → {new_name}")
            renamed += 1
        else:
            print(f"  {old_name} → SKIP (not found)")
    write_anim_file(af, args.file)
    print(f"Renamed {renamed}/{len(mapping)} clips in {args.file}")

def cmd_import_anim(args):
    """Import animation clips from a source .anim file into the master file.

    Workflow for adding downloaded Mixamo animations:
    1. Extract from FBX: python tools/asset_pipeline/extract_animation.py input.fbx -o temp.anim
    2. Import into master: python tools/anim_editor.py import-anim humanoid.anim temp.anim --clip attack --as attack
       Or import all: python tools/anim_editor.py import-anim humanoid.anim temp.anim --all
    """
    master = parse_anim_file(args.file)
    source = parse_anim_file(args.source)

    # Show what's available in the source
    print(f"Source: {args.source} ({len(source.clips)} clips)")
    for i, clip in enumerate(source.clips):
        print(f"  [{i}] {clip.name} (dur={clip.duration:.3f}s)")

    if args.all:
        # Import all clips, applying prefix/rename
        imported = 0
        for clip in source.clips:
            name = clip.name
            if args.prefix:
                name = args.prefix + name
            # Skip placeholder clips
            if clip.duration <= 0.001:
                print(f"  Skipping '{clip.name}' (zero duration)")
                continue
            clip.name = name
            master.remove_clip(name)
            master.clips.append(clip)
            print(f"  Imported: {name}")
            imported += 1
        print(f"\nImported {imported} clips")
    elif args.clip:
        # Import specific clip(s)
        for clip_name in args.clip:
            src_clip = source.get_clip(clip_name)
            if not src_clip:
                print(f"  Error: clip '{clip_name}' not found in source", file=sys.stderr)
                continue
            target_name = args.rename if args.rename and len(args.clip) == 1 else clip_name
            src_clip.name = target_name
            master.remove_clip(target_name)
            master.clips.append(src_clip)
            print(f"  Imported: {clip_name} → {target_name}")
    else:
        print("\nUse --all to import all clips, or --clip <name> to import specific ones.")
        return

    write_anim_file(master, args.file)
    print(f"Written: {args.file}")
    print(f"Master now has {len(master.clips)} clips")

def cmd_generate(args):
    """Generate a clip from a template JSON definition.

    Template format:
    {
      "name": "attack",
      "duration": 0.8,
      "speed": null,
      "keyframes": [
        {"bone": "right_arm", "type": "rotation", "axis": [1,0,0], "angle": -90, "wave": "half"},
        {"bone": "right_forearm", "type": "rotation", "axis": [1,0,0], "angle": -45, "wave": "half"},
        {"bone": "spine2", "type": "rotation", "axis": [0,1,0], "angle": 30, "wave": "half"}
      ]
    }
    """
    # Load skeleton for bone name resolution
    skeleton_file = args.skeleton
    af = parse_anim_file(skeleton_file)

    with open(args.template, "r") as f:
        tmpl = json.load(f)

    clip_name = tmpl["name"]
    duration = tmpl["duration"]
    speed = tmpl.get("speed")
    num_keys = tmpl.get("num_keys", 30)

    clip = Clip(name=clip_name, duration=duration, speed=speed)

    for kf_def in tmpl.get("keyframes", []):
        bone_name_semantic = kf_def["bone"]
        # Resolve semantic name → actual bone name → bone ID
        actual_name = MIXAMO_BONES.get(bone_name_semantic, bone_name_semantic)
        bone_id = af.bone_map.get(actual_name)
        if bone_id is None:
            print(f"Warning: bone '{bone_name_semantic}' ({actual_name}) not found in skeleton, skipping", file=sys.stderr)
            continue

        # Get base pose from skeleton
        base_bone = af.bones[bone_id] if bone_id < len(af.bones) else None
        base_rot = base_bone.rot if base_bone else quat_identity()
        base_pos = base_bone.pos if base_bone else (0.0, 0.0, 0.0)

        kf_type = kf_def.get("type", "rotation")
        wave = kf_def.get("wave", "sine")
        n_keys = kf_def.get("num_keys", num_keys)

        if kf_type == "rotation":
            axis = tuple(kf_def["axis"])
            angle = kf_def["angle"]
            ch = make_rotation_keyframes(bone_id, base_rot, axis, angle, duration, n_keys, wave)
        elif kf_type == "position":
            offset = tuple(kf_def["offset"])
            ch = make_position_keyframes(bone_id, base_pos, offset, duration, n_keys, wave)
        else:
            print(f"Warning: unknown keyframe type '{kf_type}'", file=sys.stderr)
            continue

        clip.channels.append(ch)

    data = clip_to_json(clip)
    output = args.output or f"{clip_name}.json"
    with open(output, "w") as f:
        json.dump(data, f, indent=2)
    print(f"Generated {len(clip.channels)} channels, {num_keys} keys each → {output}")


# ===========================================================================
# Main
# ===========================================================================

def main():
    parser = argparse.ArgumentParser(
        description="Phyxel .anim file editor — read, create, modify, and merge animation files."
    )
    sub = parser.add_subparsers(dest="command", required=True)

    # list
    p = sub.add_parser("list", help="List clips in a .anim file")
    p.add_argument("file", help="Path to .anim file")

    # info
    p = sub.add_parser("info", help="Show clip details")
    p.add_argument("file", help="Path to .anim file")
    p.add_argument("clip", help="Clip name")

    # add
    p = sub.add_parser("add", help="Add clip from JSON file")
    p.add_argument("file", help="Path to .anim file")
    p.add_argument("clip_json", help="Path to clip JSON file")

    # remove
    p = sub.add_parser("remove", help="Remove a clip")
    p.add_argument("file", help="Path to .anim file")
    p.add_argument("clip_name", help="Clip name to remove")

    # merge
    p = sub.add_parser("merge", help="Merge clips from overlay into base")
    p.add_argument("base", help="Base .anim file")
    p.add_argument("overlay", help="Overlay .anim file (clips to merge in)")
    p.add_argument("-o", "--output", help="Output file (default: overwrites base)")

    # export
    p = sub.add_parser("export", help="Export a clip as JSON")
    p.add_argument("file", help="Path to .anim file")
    p.add_argument("clip", help="Clip name")
    p.add_argument("-o", "--output", help="Output JSON file")

    # bones
    p = sub.add_parser("bones", help="List skeleton bones")
    p.add_argument("file", help="Path to .anim file")

    # mirror
    p = sub.add_parser("mirror", help="Mirror left/right bones in a clip")
    p.add_argument("file", help="Path to .anim file")
    p.add_argument("clip", help="Source clip name")
    p.add_argument("new_name", help="Name for mirrored clip")

    # scale-time
    p = sub.add_parser("scale-time", help="Scale clip duration")
    p.add_argument("file", help="Path to .anim file")
    p.add_argument("clip", help="Clip name")
    p.add_argument("factor", type=float, help="Time scale factor (e.g., 0.5 = half speed)")

    # rename
    p = sub.add_parser("rename", help="Rename a clip")
    p.add_argument("file", help="Path to .anim file")
    p.add_argument("old_name", help="Current clip name")
    p.add_argument("new_name", help="New clip name")

    # batch-rename
    p = sub.add_parser("batch-rename", help="Rename multiple clips from a JSON mapping")
    p.add_argument("file", help="Path to .anim file")
    p.add_argument("mapping", help="JSON file: {\"OldName\": \"new_name\", ...}")

    # import-anim
    p = sub.add_parser("import-anim", help="Import clips from another .anim file into the master")
    p.add_argument("file", help="Master .anim file to import into")
    p.add_argument("source", help="Source .anim file to import from")
    p.add_argument("--clip", nargs="+", help="Specific clip name(s) to import")
    p.add_argument("--rename", help="Rename the imported clip (only with single --clip)")
    p.add_argument("--all", action="store_true", help="Import all clips from source")
    p.add_argument("--prefix", help="Add prefix to all imported clip names")

    # generate
    p = sub.add_parser("generate", help="Generate clip from template")
    p.add_argument("template", help="Template JSON file")
    p.add_argument("-s", "--skeleton", required=True, help="Skeleton .anim file (for bone name resolution)")
    p.add_argument("-o", "--output", help="Output JSON file")

    args = parser.parse_args()

    commands = {
        "list": cmd_list,
        "info": cmd_info,
        "add": cmd_add,
        "remove": cmd_remove,
        "merge": cmd_merge,
        "export": cmd_export,
        "bones": cmd_bones,
        "mirror": cmd_mirror,
        "scale-time": cmd_scale_time,
        "rename": cmd_rename,
        "batch-rename": cmd_batch_rename,
        "import-anim": cmd_import_anim,
        "generate": cmd_generate,
    }
    commands[args.command](args)


if __name__ == "__main__":
    main()
