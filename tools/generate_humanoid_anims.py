#!/usr/bin/env python3
"""
Generate new animation clips for humanoid.anim.

Creates:
  1. attack       - Right-hand punch (one-shot, ~0.7s)
  2. crouch_idle  - Breathing/weight-shift loop while crouched (~3.0s)
  3. wave         - NPC friendly wave gesture (~2.0s)
  4. talk         - Subtle gesticulation during conversation (~3.5s)
  5. point        - Point forward gesture (~1.5s)

All clips are authored as explicit keyframe JSON, then added to the .anim file
via anim_editor.py's add command.
"""

import json
import math
import sys
import os

# Add tools dir to path so we can import anim_editor helpers
sys.path.insert(0, os.path.join(os.path.dirname(__file__)))
from anim_editor import (
    parse_anim_file, clip_to_json, clip_from_json, Clip, Channel,
    PosKey, RotKey, ScaleKey, quat_identity, quat_from_axis_angle,
    quat_mul, quat_normalize, quat_slerp, MIXAMO_BONES
)

HUMANOID_FILE = "resources/animated_characters/humanoid.anim"
OUTPUT_DIR = "tools/generated_clips"


def load_skeleton():
    """Load skeleton and build bone name→id map."""
    af = parse_anim_file(HUMANOID_FILE)
    bone_map = af.bone_map  # name → id
    bones = af.bones  # list of Bone objects with .pos and .rot (bind pose)
    return af, bone_map, bones


def resolve_bone(bone_map, semantic_name):
    """Resolve semantic bone name to bone ID."""
    actual = MIXAMO_BONES.get(semantic_name, semantic_name)
    bid = bone_map.get(actual)
    if bid is None:
        print(f"Warning: bone '{semantic_name}' ({actual}) not found!")
    return bid


def get_bind_rot(bones, bone_id):
    """Get bind-pose rotation for a bone."""
    if bone_id is not None and bone_id < len(bones):
        return bones[bone_id].rot
    return quat_identity()


def get_bind_pos(bones, bone_id):
    """Get bind-pose position for a bone."""
    if bone_id is not None and bone_id < len(bones):
        return bones[bone_id].pos
    return (0.0, 0.0, 0.0)


def make_rot_channel(bone_id, keyframes, duration):
    """Create a rotation channel from explicit (time_frac, quat) keyframes.
    
    keyframes: list of (time_fraction, (x,y,z,w)) where time_fraction is 0..1
    """
    ch = Channel(bone_id=bone_id)
    for t_frac, q in keyframes:
        t = t_frac * duration
        ch.rot_keys.append(RotKey(t, q[0], q[1], q[2], q[3]))
    return ch


def make_pos_channel(bone_id, keyframes, duration):
    """Create a position channel from explicit (time_frac, (x,y,z)) keyframes."""
    ch = Channel(bone_id=bone_id)
    for t_frac, p in keyframes:
        t = t_frac * duration
        ch.pos_keys.append(PosKey(t, p[0], p[1], p[2]))
    return ch


def rot_offset(base_rot, axis, angle_deg):
    """Apply a rotation offset to a base quaternion."""
    offset = quat_from_axis_angle(axis, angle_deg)
    result = quat_mul(base_rot, offset)
    return quat_normalize(result)


def lerp3(a, b, t):
    """Linear interpolation between two 3-tuples."""
    return (a[0] + (b[0]-a[0])*t, a[1] + (b[1]-a[1])*t, a[2] + (b[2]-a[2])*t)


def slerp(q1, q2, t):
    """Slerp between two quaternions."""
    return quat_slerp(q1, q2, t)


def sine_ease(t):
    """Sine easing: smooth start and end."""
    return 0.5 - 0.5 * math.cos(t * math.pi)


def breathing_offset(t, cycle_dur, amplitude):
    """Subtle breathing oscillation."""
    return amplitude * math.sin(2 * math.pi * t / cycle_dur)


# ===========================================================================
# Clip 1: ATTACK (right-hand punch, one-shot)
# ===========================================================================
def generate_attack(bone_map, bones):
    """Generate a right-hand forward punch animation.
    
    Phases:
      0.00-0.15: Wind-up (pull right arm back, twist torso right)
      0.15-0.35: Strike (right arm extends forward, torso twists left)
      0.35-0.55: Hold/Impact 
      0.55-0.70: Recovery (return to idle pose)
    
    Duration: 0.7s
    """
    duration = 0.7
    clip = Clip(name="attack", duration=duration, speed=None)
    
    # Bone IDs
    hips = resolve_bone(bone_map, "hips")
    spine = resolve_bone(bone_map, "spine")
    spine1 = resolve_bone(bone_map, "spine1")
    spine2 = resolve_bone(bone_map, "spine2")
    r_shoulder = resolve_bone(bone_map, "right_shoulder")
    r_arm = resolve_bone(bone_map, "right_arm")
    r_forearm = resolve_bone(bone_map, "right_forearm")
    r_hand = resolve_bone(bone_map, "right_hand")
    l_shoulder = resolve_bone(bone_map, "left_shoulder")
    l_arm = resolve_bone(bone_map, "left_arm")
    l_forearm = resolve_bone(bone_map, "left_forearm")
    head = resolve_bone(bone_map, "head")
    neck = resolve_bone(bone_map, "neck")
    
    # Base (bind) rotations
    br = {bid: get_bind_rot(bones, bid) for bid in 
          [hips, spine, spine1, spine2, r_shoulder, r_arm, r_forearm, r_hand,
           l_shoulder, l_arm, l_forearm, head, neck] if bid is not None}
    bp = {bid: get_bind_pos(bones, bid) for bid in [hips] if bid is not None}
    
    # --- Hips: slight forward lunge ---
    if hips is not None:
        base_pos = bp[hips]
        clip.channels.append(make_pos_channel(hips, [
            (0.0,  base_pos),                                              # Start
            (0.15, (base_pos[0], base_pos[1], base_pos[2] - 0.02)),       # Wind-up: pull back
            (0.35, (base_pos[0], base_pos[1] - 0.02, base_pos[2] + 0.04)), # Strike: lunge forward
            (0.55, (base_pos[0], base_pos[1] - 0.01, base_pos[2] + 0.03)), # Hold
            (1.0,  base_pos),                                              # Recovery
        ], duration))
        
        # Hips rotation: slight twist
        clip.channels.append(make_rot_channel(hips, [
            (0.0,  br[hips]),
            (0.15, rot_offset(br[hips], (0, 1, 0), 10)),    # Wind-up: twist right
            (0.35, rot_offset(br[hips], (0, 1, 0), -15)),   # Strike: twist left
            (0.55, rot_offset(br[hips], (0, 1, 0), -10)),   # Hold
            (1.0,  br[hips]),                                # Recovery
        ], duration))
    
    # --- Spine chain: amplify the twist ---
    for bid, wind_angle, strike_angle in [
        (spine, 3, -5), (spine1, 5, -8), (spine2, 8, -12)
    ]:
        if bid is not None:
            clip.channels.append(make_rot_channel(bid, [
                (0.0,  br[bid]),
                (0.15, rot_offset(br[bid], (0, 1, 0), wind_angle)),
                (0.30, rot_offset(br[bid], (0, 1, 0), strike_angle)),
                (0.50, rot_offset(br[bid], (0, 1, 0), strike_angle * 0.7)),
                (1.0,  br[bid]),
            ], duration))
    
    # --- Right arm: the punch ---
    if r_shoulder is not None:
        clip.channels.append(make_rot_channel(r_shoulder, [
            (0.0,  br[r_shoulder]),
            (0.15, rot_offset(br[r_shoulder], (0, 0, 1), -15)),   # Pull shoulder back
            (0.30, rot_offset(br[r_shoulder], (0, 0, 1), 20)),    # Push forward
            (0.55, rot_offset(br[r_shoulder], (0, 0, 1), 15)),
            (1.0,  br[r_shoulder]),
        ], duration))
    
    if r_arm is not None:
        clip.channels.append(make_rot_channel(r_arm, [
            (0.0,  br[r_arm]),
            (0.12, rot_offset(br[r_arm], (1, 0, 0), 30)),         # Wind-up: arm back
            (0.15, rot_offset(br[r_arm], (1, 0, 0), 40)),         # Peak wind-up
            (0.28, rot_offset(br[r_arm], (1, 0, 0), -70)),        # Strike forward (upper arm rotates)
            (0.35, rot_offset(br[r_arm], (1, 0, 0), -80)),        # Full extension
            (0.55, rot_offset(br[r_arm], (1, 0, 0), -60)),        # Hold
            (1.0,  br[r_arm]),                                     # Recovery
        ], duration))
    
    if r_forearm is not None:
        clip.channels.append(make_rot_channel(r_forearm, [
            (0.0,  br[r_forearm]),
            (0.12, rot_offset(br[r_forearm], (1, 0, 0), -70)),    # Wind-up: forearm bends back
            (0.15, rot_offset(br[r_forearm], (1, 0, 0), -90)),    # Peak bend
            (0.28, rot_offset(br[r_forearm], (1, 0, 0), -20)),    # Strike: extend
            (0.35, rot_offset(br[r_forearm], (1, 0, 0), -10)),    # Near full extension
            (0.55, rot_offset(br[r_forearm], (1, 0, 0), -15)),    # Hold
            (1.0,  br[r_forearm]),                                  # Recovery
        ], duration))
    
    if r_hand is not None:
        # Fist: curl hand closed
        clip.channels.append(make_rot_channel(r_hand, [
            (0.0,  br[r_hand]),
            (0.10, rot_offset(br[r_hand], (1, 0, 0), -30)),       # Start making fist
            (0.15, rot_offset(br[r_hand], (1, 0, 0), -45)),       # Fist tight
            (0.55, rot_offset(br[r_hand], (1, 0, 0), -45)),       # Hold fist
            (0.80, rot_offset(br[r_hand], (1, 0, 0), -20)),       # Release
            (1.0,  br[r_hand]),
        ], duration))
    
    # --- Left arm: guard position (slight raise) ---
    if l_arm is not None:
        clip.channels.append(make_rot_channel(l_arm, [
            (0.0,  br[l_arm]),
            (0.15, rot_offset(br[l_arm], (1, 0, 0), -15)),        # Raise guard
            (0.20, rot_offset(br[l_arm], (1, 0, 0), -25)),        # Guard up
            (0.55, rot_offset(br[l_arm], (1, 0, 0), -20)),        # Hold guard
            (1.0,  br[l_arm]),
        ], duration))
    
    if l_forearm is not None:
        clip.channels.append(make_rot_channel(l_forearm, [
            (0.0,  br[l_forearm]),
            (0.15, rot_offset(br[l_forearm], (1, 0, 0), -40)),    # Bend guard arm
            (0.20, rot_offset(br[l_forearm], (1, 0, 0), -55)),    # Guard tight
            (0.55, rot_offset(br[l_forearm], (1, 0, 0), -50)),    # Hold
            (1.0,  br[l_forearm]),
        ], duration))
    
    # --- Head/Neck: look toward target ---
    if neck is not None:
        clip.channels.append(make_rot_channel(neck, [
            (0.0, br[neck]),
            (0.20, rot_offset(br[neck], (0, 1, 0), -8)),    # Look toward punch direction
            (0.55, rot_offset(br[neck], (0, 1, 0), -5)),
            (1.0, br[neck]),
        ], duration))
    
    if head is not None:
        clip.channels.append(make_rot_channel(head, [
            (0.0, br[head]),
            (0.20, rot_offset(br[head], (1, 0, 0), -5)),    # Slight head tilt down
            (0.55, rot_offset(br[head], (1, 0, 0), -3)),
            (1.0, br[head]),
        ], duration))
    
    return clip


# ===========================================================================
# Clip 2: CROUCH_IDLE (breathing loop while crouched)
# ===========================================================================
def generate_crouch_idle(bone_map, bones):
    """Generate a subtle idle loop in crouched position.
    
    Uses the final pose from standing_to_crouched as the base,
    then adds subtle breathing (spine flex) and weight shifting.
    
    Duration: 3.0s (looping)
    """
    duration = 3.0
    clip = Clip(name="crouch_idle", duration=duration, speed=None)
    
    # Load the crouched pose from the exported clip
    with open("standing_to_crouched_export.json") as f:
        crouch_data = json.load(f)
    
    # Build a map of bone_id → final rotation/position from the crouched clip
    crouch_pose_rot = {}
    crouch_pose_pos = {}
    for ch in crouch_data["channels"]:
        bid = ch["bone_id"]
        if ch.get("rot_keys"):
            rk = ch["rot_keys"][-1]  # Last keyframe = final crouched pose
            crouch_pose_rot[bid] = (rk["x"], rk["y"], rk["z"], rk["w"])
        if ch.get("pos_keys"):
            pk = ch["pos_keys"][-1]
            crouch_pose_pos[bid] = (pk["x"], pk["y"], pk["z"])
    
    # Key bones
    hips = resolve_bone(bone_map, "hips")
    spine = resolve_bone(bone_map, "spine")
    spine1 = resolve_bone(bone_map, "spine1")
    spine2 = resolve_bone(bone_map, "spine2")
    head = resolve_bone(bone_map, "head")
    neck = resolve_bone(bone_map, "neck")
    r_arm = resolve_bone(bone_map, "right_arm")
    r_forearm = resolve_bone(bone_map, "right_forearm")
    l_arm = resolve_bone(bone_map, "left_arm")
    l_forearm = resolve_bone(bone_map, "left_forearm")
    r_shoulder = resolve_bone(bone_map, "right_shoulder")
    l_shoulder = resolve_bone(bone_map, "left_shoulder")
    
    num_keys = 40  # Smooth loop
    
    # For each bone that had data in the crouched pose, create a channel
    # with subtle breathing oscillation
    for ch_data in crouch_data["channels"]:
        bid = ch_data["bone_id"]
        base_rot = crouch_pose_rot.get(bid)
        base_pos = crouch_pose_pos.get(bid)
        
        ch = Channel(bone_id=bid)
        
        if base_rot:
            for i in range(num_keys):
                t = (i / (num_keys - 1)) * duration
                # Breathing cycle: subtle spine extension
                breath = math.sin(2 * math.pi * t / duration)  # One full breath cycle
                
                # Different bones get different amounts of breathing
                if bid == spine or bid == spine1:
                    angle = 1.5 * breath  # Very subtle spine flex
                    offset_q = quat_from_axis_angle((1, 0, 0), angle)
                    q = quat_normalize(quat_mul(base_rot, offset_q))
                elif bid == spine2:
                    angle = 2.0 * breath
                    offset_q = quat_from_axis_angle((1, 0, 0), angle)
                    q = quat_normalize(quat_mul(base_rot, offset_q))
                elif bid == head:
                    # Subtle head look around
                    look_angle = 3.0 * math.sin(2 * math.pi * t / duration * 0.5)  # Slower
                    offset_q = quat_from_axis_angle((0, 1, 0), look_angle)
                    q = quat_normalize(quat_mul(base_rot, offset_q))
                elif bid == r_arm or bid == l_arm:
                    # Subtle arm sway
                    sway = 1.0 * breath
                    offset_q = quat_from_axis_angle((1, 0, 0), sway)
                    q = quat_normalize(quat_mul(base_rot, offset_q))
                else:
                    q = base_rot
                
                ch.rot_keys.append(RotKey(t, q[0], q[1], q[2], q[3]))
        
        if base_pos:
            for i in range(num_keys):
                t = (i / (num_keys - 1)) * duration
                breath = math.sin(2 * math.pi * t / duration)
                
                if bid == hips:
                    # Hips rise slightly on inhale
                    py = base_pos[1] + 0.008 * breath
                    ch.pos_keys.append(PosKey(t, base_pos[0], py, base_pos[2]))
                else:
                    ch.pos_keys.append(PosKey(t, base_pos[0], base_pos[1], base_pos[2]))
        
        if ch.rot_keys or ch.pos_keys:
            clip.channels.append(ch)
    
    return clip


# ===========================================================================
# Clip 3: WAVE (NPC friendly wave)
# ===========================================================================
def generate_wave(bone_map, bones):
    """Generate a friendly wave animation for NPCs.
    
    Phases:
      0.00-0.25: Raise right arm
      0.25-0.75: Wave hand back and forth (3 oscillations)
      0.75-1.00: Lower arm
    
    Duration: 2.0s
    """
    duration = 2.0
    clip = Clip(name="wave", duration=duration, speed=None)
    
    r_shoulder = resolve_bone(bone_map, "right_shoulder")
    r_arm = resolve_bone(bone_map, "right_arm")
    r_forearm = resolve_bone(bone_map, "right_forearm")
    r_hand = resolve_bone(bone_map, "right_hand")
    spine2 = resolve_bone(bone_map, "spine2")
    head = resolve_bone(bone_map, "head")
    
    br = {bid: get_bind_rot(bones, bid) for bid in 
          [r_shoulder, r_arm, r_forearm, r_hand, spine2, head] if bid is not None}
    
    # Right shoulder: raise
    if r_shoulder is not None:
        clip.channels.append(make_rot_channel(r_shoulder, [
            (0.0,  br[r_shoulder]),
            (0.20, rot_offset(br[r_shoulder], (0, 0, 1), 15)),
            (0.25, rot_offset(br[r_shoulder], (0, 0, 1), 20)),
            (0.75, rot_offset(br[r_shoulder], (0, 0, 1), 20)),
            (0.85, rot_offset(br[r_shoulder], (0, 0, 1), 10)),
            (1.0,  br[r_shoulder]),
        ], duration))
    
    # Right arm: raise to waving position
    if r_arm is not None:
        clip.channels.append(make_rot_channel(r_arm, [
            (0.0,  br[r_arm]),
            (0.20, rot_offset(br[r_arm], (1, 0, 0), -120)),   # Raise arm up
            (0.25, rot_offset(br[r_arm], (1, 0, 0), -140)),   # Full raise
            (0.75, rot_offset(br[r_arm], (1, 0, 0), -140)),   # Hold while waving
            (0.85, rot_offset(br[r_arm], (1, 0, 0), -80)),    # Lower
            (1.0,  br[r_arm]),
        ], duration))
    
    # Right forearm: bend at elbow
    if r_forearm is not None:
        # Create wave oscillation during the wave phase
        keys = [
            (0.0,  br[r_forearm]),
            (0.20, rot_offset(br[r_forearm], (1, 0, 0), -30)),
            (0.25, rot_offset(br[r_forearm], (1, 0, 0), -45)),
        ]
        # Wave oscillations (3 back-and-forth)
        wave_start = 0.25
        wave_end = 0.75
        num_waves = 3
        num_wave_keys = num_waves * 4 + 1
        for i in range(num_wave_keys):
            t_frac = wave_start + (wave_end - wave_start) * (i / (num_wave_keys - 1))
            wave_angle = 25 * math.sin(2 * math.pi * num_waves * (i / (num_wave_keys - 1)))
            keys.append((t_frac, rot_offset(br[r_forearm], (0, 1, 0), wave_angle + (-45 if True else 0))))
        
        keys.extend([
            (0.80, rot_offset(br[r_forearm], (1, 0, 0), -20)),
            (1.0,  br[r_forearm]),
        ])
        # Deduplicate by sorting and ensuring unique time fractions
        seen_times = set()
        unique_keys = []
        for t_frac, q in sorted(keys, key=lambda x: x[0]):
            t_rounded = round(t_frac, 4)
            if t_rounded not in seen_times:
                seen_times.add(t_rounded)
                unique_keys.append((t_frac, q))
        
        clip.channels.append(make_rot_channel(r_forearm, unique_keys, duration))
    
    # Right hand: wave rotation
    if r_hand is not None:
        keys = [(0.0, br[r_hand])]
        wave_start = 0.25
        wave_end = 0.75
        num_waves = 3
        num_wave_keys = num_waves * 4 + 1
        for i in range(num_wave_keys):
            t_frac = wave_start + (wave_end - wave_start) * (i / (num_wave_keys - 1))
            wave_angle = 35 * math.sin(2 * math.pi * num_waves * (i / (num_wave_keys - 1)))
            keys.append((t_frac, rot_offset(br[r_hand], (0, 0, 1), wave_angle)))
        keys.append((1.0, br[r_hand]))
        
        seen_times = set()
        unique_keys = []
        for t_frac, q in sorted(keys, key=lambda x: x[0]):
            t_rounded = round(t_frac, 4)
            if t_rounded not in seen_times:
                seen_times.add(t_rounded)
                unique_keys.append((t_frac, q))
        
        clip.channels.append(make_rot_channel(r_hand, unique_keys, duration))
    
    # Spine2: slight lean toward waving arm
    if spine2 is not None:
        clip.channels.append(make_rot_channel(spine2, [
            (0.0, br[spine2]),
            (0.25, rot_offset(br[spine2], (0, 0, 1), 5)),   # Lean slightly right
            (0.75, rot_offset(br[spine2], (0, 0, 1), 5)),
            (1.0, br[spine2]),
        ], duration))
    
    # Head: look toward the person being waved at
    if head is not None:
        clip.channels.append(make_rot_channel(head, [
            (0.0, br[head]),
            (0.15, rot_offset(br[head], (1, 0, 0), -3)),     # Slight nod
            (0.50, rot_offset(br[head], (1, 0, 0), -5)),
            (0.85, rot_offset(br[head], (1, 0, 0), -3)),
            (1.0, br[head]),
        ], duration))
    
    return clip


# ===========================================================================
# Clip 4: TALK (subtle gesticulation during conversation)
# ===========================================================================
def generate_talk(bone_map, bones):
    """Generate subtle talking gesticulation for NPCs.
    
    Both hands move gently with varied timing to simulate natural speech gestures.
    Head nods occasionally. Weight shifts side to side.
    
    Duration: 3.5s (loops well)
    """
    duration = 3.5
    clip = Clip(name="talk", duration=duration, speed=None)
    
    hips = resolve_bone(bone_map, "hips")
    spine1 = resolve_bone(bone_map, "spine1")
    spine2 = resolve_bone(bone_map, "spine2")
    head = resolve_bone(bone_map, "head")
    neck = resolve_bone(bone_map, "neck")
    r_arm = resolve_bone(bone_map, "right_arm")
    r_forearm = resolve_bone(bone_map, "right_forearm")
    r_hand = resolve_bone(bone_map, "right_hand")
    l_arm = resolve_bone(bone_map, "left_arm")
    l_forearm = resolve_bone(bone_map, "left_forearm")
    l_hand = resolve_bone(bone_map, "left_hand")
    
    br = {}
    bp = {}
    all_bones = [hips, spine1, spine2, head, neck, r_arm, r_forearm, r_hand,
                 l_arm, l_forearm, l_hand]
    for bid in all_bones:
        if bid is not None:
            br[bid] = get_bind_rot(bones, bid)
            bp[bid] = get_bind_pos(bones, bid)
    
    num_keys = 50
    
    # Hips: subtle weight shift
    if hips is not None:
        base_pos = bp[hips]
        ch = Channel(bone_id=hips)
        for i in range(num_keys):
            t = (i / (num_keys - 1)) * duration
            shift = 0.01 * math.sin(2 * math.pi * t / duration)
            ch.pos_keys.append(PosKey(t, base_pos[0] + shift, base_pos[1], base_pos[2]))
        clip.channels.append(ch)
    
    # Head: nod and slight turns
    if head is not None:
        ch = Channel(bone_id=head)
        for i in range(num_keys):
            t = (i / (num_keys - 1)) * duration
            # Nod (pitch): two nods per cycle
            nod = 4.0 * math.sin(4 * math.pi * t / duration)
            # Turn (yaw): slow side to side
            turn = 6.0 * math.sin(2 * math.pi * t / duration * 0.7)
            q = br[head]
            q = quat_normalize(quat_mul(q, quat_from_axis_angle((1, 0, 0), nod)))
            q = quat_normalize(quat_mul(q, quat_from_axis_angle((0, 1, 0), turn)))
            ch.rot_keys.append(RotKey(t, q[0], q[1], q[2], q[3]))
        clip.channels.append(ch)
    
    # Neck: subtle countermotion
    if neck is not None:
        ch = Channel(bone_id=neck)
        for i in range(num_keys):
            t = (i / (num_keys - 1)) * duration
            angle = 2.0 * math.sin(2 * math.pi * t / duration * 0.5)
            q = quat_normalize(quat_mul(br[neck], quat_from_axis_angle((0, 1, 0), angle)))
            ch.rot_keys.append(RotKey(t, q[0], q[1], q[2], q[3]))
        clip.channels.append(ch)
    
    # Spine: subtle sway
    if spine1 is not None:
        ch = Channel(bone_id=spine1)
        for i in range(num_keys):
            t = (i / (num_keys - 1)) * duration
            sway = 2.0 * math.sin(2 * math.pi * t / duration)
            q = quat_normalize(quat_mul(br[spine1], quat_from_axis_angle((0, 0, 1), sway)))
            ch.rot_keys.append(RotKey(t, q[0], q[1], q[2], q[3]))
        clip.channels.append(ch)
    
    # Right arm: gesture up and down
    for arm_bid, forearm_bid, hand_bid, phase_offset in [
        (r_arm, r_forearm, r_hand, 0.0),
        (l_arm, l_forearm, l_hand, 0.4),  # Left is offset in phase
    ]:
        if arm_bid is not None:
            ch = Channel(bone_id=arm_bid)
            for i in range(num_keys):
                t = (i / (num_keys - 1)) * duration
                # Asymmetric gesture: raise and lower
                gesture = -12.0 * max(0, math.sin(2 * math.pi * (t / duration + phase_offset)))
                q = quat_normalize(quat_mul(br[arm_bid], quat_from_axis_angle((1, 0, 0), gesture)))
                ch.rot_keys.append(RotKey(t, q[0], q[1], q[2], q[3]))
            clip.channels.append(ch)
        
        if forearm_bid is not None:
            ch = Channel(bone_id=forearm_bid)
            for i in range(num_keys):
                t = (i / (num_keys - 1)) * duration
                bend = -20.0 * max(0, math.sin(2 * math.pi * (t / duration + phase_offset)))
                q = quat_normalize(quat_mul(br[forearm_bid], quat_from_axis_angle((1, 0, 0), bend)))
                ch.rot_keys.append(RotKey(t, q[0], q[1], q[2], q[3]))
            clip.channels.append(ch)
        
        if hand_bid is not None:
            ch = Channel(bone_id=hand_bid)
            for i in range(num_keys):
                t = (i / (num_keys - 1)) * duration
                # Open hand gesture
                spread = 8.0 * math.sin(2 * math.pi * (t / duration + phase_offset) * 1.5)
                q = quat_normalize(quat_mul(br[hand_bid], quat_from_axis_angle((0, 0, 1), spread)))
                ch.rot_keys.append(RotKey(t, q[0], q[1], q[2], q[3]))
            clip.channels.append(ch)
    
    return clip


# ===========================================================================
# Clip 5: POINT (point forward gesture)
# ===========================================================================
def generate_point(bone_map, bones):
    """Generate a pointing gesture animation.
    
    Phases:
      0.00-0.30: Raise right arm to point
      0.30-0.70: Hold point
      0.70-1.00: Lower arm
    
    Duration: 1.5s
    """
    duration = 1.5
    clip = Clip(name="point", duration=duration, speed=None)
    
    r_shoulder = resolve_bone(bone_map, "right_shoulder")
    r_arm = resolve_bone(bone_map, "right_arm")
    r_forearm = resolve_bone(bone_map, "right_forearm")
    r_hand = resolve_bone(bone_map, "right_hand")
    spine2 = resolve_bone(bone_map, "spine2")
    head = resolve_bone(bone_map, "head")
    
    br = {bid: get_bind_rot(bones, bid) for bid in 
          [r_shoulder, r_arm, r_forearm, r_hand, spine2, head] if bid is not None}
    
    # Right arm: raise to pointing position (horizontal)
    if r_arm is not None:
        clip.channels.append(make_rot_channel(r_arm, [
            (0.0,  br[r_arm]),
            (0.25, rot_offset(br[r_arm], (1, 0, 0), -85)),    # Raise to horizontal
            (0.30, rot_offset(br[r_arm], (1, 0, 0), -90)),    # Fully horizontal
            (0.70, rot_offset(br[r_arm], (1, 0, 0), -90)),    # Hold
            (0.80, rot_offset(br[r_arm], (1, 0, 0), -50)),    # Start lowering
            (1.0,  br[r_arm]),
        ], duration))
    
    # Right forearm: extend straight
    if r_forearm is not None:
        clip.channels.append(make_rot_channel(r_forearm, [
            (0.0,  br[r_forearm]),
            (0.25, rot_offset(br[r_forearm], (1, 0, 0), -5)),
            (0.30, rot_offset(br[r_forearm], (1, 0, 0), -5)),  # Straight
            (0.70, rot_offset(br[r_forearm], (1, 0, 0), -5)),
            (1.0,  br[r_forearm]),
        ], duration))
    
    # Right shoulder: slight raise
    if r_shoulder is not None:
        clip.channels.append(make_rot_channel(r_shoulder, [
            (0.0,  br[r_shoulder]),
            (0.25, rot_offset(br[r_shoulder], (0, 0, 1), 10)),
            (0.70, rot_offset(br[r_shoulder], (0, 0, 1), 10)),
            (1.0,  br[r_shoulder]),
        ], duration))
    
    # Right hand: point index finger (slight curl)
    if r_hand is not None:
        clip.channels.append(make_rot_channel(r_hand, [
            (0.0,  br[r_hand]),
            (0.25, rot_offset(br[r_hand], (1, 0, 0), -10)),    # Extend hand
            (0.70, rot_offset(br[r_hand], (1, 0, 0), -10)),
            (1.0,  br[r_hand]),
        ], duration))
    
    # Spine2: slight twist toward pointing direction
    if spine2 is not None:
        clip.channels.append(make_rot_channel(spine2, [
            (0.0,  br[spine2]),
            (0.30, rot_offset(br[spine2], (0, 1, 0), -8)),
            (0.70, rot_offset(br[spine2], (0, 1, 0), -8)),
            (1.0,  br[spine2]),
        ], duration))
    
    # Head: look toward pointing direction
    if head is not None:
        clip.channels.append(make_rot_channel(head, [
            (0.0,  br[head]),
            (0.25, rot_offset(br[head], (0, 1, 0), -10)),
            (0.30, rot_offset(br[head], (0, 1, 0), -12)),
            (0.70, rot_offset(br[head], (0, 1, 0), -12)),
            (0.85, rot_offset(br[head], (0, 1, 0), -5)),
            (1.0,  br[head]),
        ], duration))
    
    return clip


# ===========================================================================
# Main
# ===========================================================================
def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    
    print("Loading skeleton from", HUMANOID_FILE)
    af, bone_map, bones = load_skeleton()
    
    clips = []
    
    # Generate all clips
    print("\n--- Generating attack clip ---")
    attack = generate_attack(bone_map, bones)
    clips.append(attack)
    print(f"  {attack.name}: {attack.duration:.1f}s, {len(attack.channels)} channels")
    
    print("\n--- Generating crouch_idle clip ---")
    crouch_idle = generate_crouch_idle(bone_map, bones)
    clips.append(crouch_idle)
    print(f"  {crouch_idle.name}: {crouch_idle.duration:.1f}s, {len(crouch_idle.channels)} channels")
    
    print("\n--- Generating wave clip ---")
    wave = generate_wave(bone_map, bones)
    clips.append(wave)
    print(f"  {wave.name}: {wave.duration:.1f}s, {len(wave.channels)} channels")
    
    print("\n--- Generating talk clip ---")
    talk = generate_talk(bone_map, bones)
    clips.append(talk)
    print(f"  {talk.name}: {talk.duration:.1f}s, {len(talk.channels)} channels")
    
    print("\n--- Generating point clip ---")
    point = generate_point(bone_map, bones)
    clips.append(point)
    print(f"  {point.name}: {point.duration:.1f}s, {len(point.channels)} channels")
    
    # Write all clips as a single JSON array (add command supports this)
    all_clips_data = [clip_to_json(c) for c in clips]
    output_path = os.path.join(OUTPUT_DIR, "new_clips.json")
    with open(output_path, "w") as f:
        json.dump(all_clips_data, f, indent=2)
    
    print(f"\n=== Generated {len(clips)} clips → {output_path} ===")
    print("To add to humanoid.anim:")
    print(f"  python tools/anim_editor.py add {HUMANOID_FILE} {output_path}")


if __name__ == "__main__":
    main()
