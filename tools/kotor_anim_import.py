#!/usr/bin/env python3
"""
kotor_anim_import.py — Pure Python: KOTOR game files → Phyxel .anim
No Blender, no external tools, no additional pip packages.
Only Python stdlib + Phyxel's own anim_editor.py.

Based on the binary MDL format as implemented in:
  https://github.com/seedhartha/kotorblender (GPL-2.0)

Usage:
  # List humanoid character models found in the game data
  python tools/kotor_anim_import.py "G:/SteamLibrary/steamapps/common/swkotor" --list

  # Show animations available in a specific model
  python tools/kotor_anim_import.py "G:/SteamLibrary/..." --model n_humanm01 --list-anims

  # Convert all animations from a model
  python tools/kotor_anim_import.py "G:/SteamLibrary/..." --model n_humanm01

  # Convert and rename specific clips to Phyxel FSM names
  python tools/kotor_anim_import.py "G:/SteamLibrary/..." --model n_humanm01 \\
      --map "walk=walk" "run=run" "idle=idle" "attack=attack"

  # Merge into humanoid.anim instead of writing a separate file
  python tools/kotor_anim_import.py "G:/SteamLibrary/..." --model n_humanm01 --merge
"""

import struct
import sys
import math
import argparse
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from anim_editor import (
    parse_anim_file, write_anim_file,
    AnimFile, Clip, Channel, PosKey, RotKey, ScaleKey,
)

# ---------------------------------------------------------------------------
# MDL format constants (from kotorblender/io_scene_kotor/format/mdl/types.py)
# ---------------------------------------------------------------------------
MDL_OFFSET = 12          # all MDL offsets are relative to byte 12

CTRL_BASE_POSITION    = 8
CTRL_BASE_ORIENTATION = 20
CTRL_FLAG_BEZIER      = 0x10

# Node type flags
NODE_LIGHT     = 0x0002
NODE_EMITTER   = 0x0004
NODE_REFERENCE = 0x0010
NODE_MESH      = 0x0020
NODE_SKIN      = 0x0040
NODE_DANGLY    = 0x0100
NODE_AABB      = 0x0200
NODE_SABER     = 0x0800

# Function pointer values used to detect KotOR 2 vs KotOR 1
MODEL_FN_PTR_1_K2_PC   = 4285200
MODEL_FN_PTR_1_K2_XBOX = 4285872

# BIF resource type codes
RES_MDL = 2002
RES_MDX = 2015

# ---------------------------------------------------------------------------
# Bone name mapping: KOTOR/Aurora-Engine → Mixamo
# ---------------------------------------------------------------------------
KOTOR_TO_MIXAMO: dict[str, str] = {
    # ── Hips / pelvis ──────────────────────────────────────────────────────
    "torsol":        "mixamorig:Hips",
    "torso_l":       "mixamorig:Hips",
    "hip":           "mixamorig:Hips",
    "hips":          "mixamorig:Hips",
    "pelvis":        "mixamorig:Hips",
    # ── Spine ──────────────────────────────────────────────────────────────
    "torso":         "mixamorig:Spine",      # KOTOR K1: torso_g (lower spine)
    "torsom":        "mixamorig:Spine",
    "torso_m":       "mixamorig:Spine",
    "spine":         "mixamorig:Spine",
    "torsoupr":      "mixamorig:Spine1",     # KOTOR K1: torsoUpr_g (mid spine)
    "breastbone":    "mixamorig:Spine2",     # KOTOR K1: chest/sternum (upper spine)
    "torsou":        "mixamorig:Spine1",
    "torso_u":       "mixamorig:Spine1",
    "spine1":        "mixamorig:Spine1",
    "spine2":        "mixamorig:Spine2",
    "chest":         "mixamorig:Spine2",
    # ── Neck / head ────────────────────────────────────────────────────────
    "neckl":         "mixamorig:Neck",
    "neck_l":        "mixamorig:Neck",
    "necklwr":       "mixamorig:Neck",       # KOTOR K1: necklwr_g (lower neck)
    "neck":          "mixamorig:Neck",
    "necku":         "mixamorig:Head",
    "neck_u":        "mixamorig:Head",
    "head":          "mixamorig:Head",
    # ── Left arm ───────────────────────────────────────────────────────────
    "lcollar":       "mixamorig:LeftShoulder",  # KOTOR K1: lcollar_g
    "lshldr":        "mixamorig:LeftShoulder",
    "l_shldr":       "mixamorig:LeftShoulder",
    "lshoulder":     "mixamorig:LeftShoulder",
    "lbicep":        "mixamorig:LeftArm",
    "l_bicep":       "mixamorig:LeftArm",
    "luparm":        "mixamorig:LeftArm",
    "l_uparm":       "mixamorig:LeftArm",
    "lforearm":      "mixamorig:LeftForeArm",
    "l_forearm":     "mixamorig:LeftForeArm",
    "lloarm":        "mixamorig:LeftForeArm",
    "l_loarm":       "mixamorig:LeftForeArm",
    "lhand":         "mixamorig:LeftHand",
    "l_hand":        "mixamorig:LeftHand",
    # ── Right arm ──────────────────────────────────────────────────────────
    "rcollar":       "mixamorig:RightShoulder", # KOTOR K1: rcollar_g
    "rshldr":        "mixamorig:RightShoulder",
    "r_shldr":       "mixamorig:RightShoulder",
    "rshoulder":     "mixamorig:RightShoulder",
    "rbicep":        "mixamorig:RightArm",
    "r_bicep":       "mixamorig:RightArm",
    "ruparm":        "mixamorig:RightArm",
    "r_uparm":       "mixamorig:RightArm",
    "rforearm":      "mixamorig:RightForeArm",
    "r_forearm":     "mixamorig:RightForeArm",
    "rloarm":        "mixamorig:RightForeArm",
    "r_loarm":       "mixamorig:RightForeArm",
    "rhand":         "mixamorig:RightHand",
    "r_hand":        "mixamorig:RightHand",
    # ── Left leg ───────────────────────────────────────────────────────────
    "lthigh":        "mixamorig:LeftUpLeg",
    "l_thigh":       "mixamorig:LeftUpLeg",
    "lupleg":        "mixamorig:LeftUpLeg",
    "l_upleg":       "mixamorig:LeftUpLeg",
    "lshin":         "mixamorig:LeftLeg",
    "l_shin":        "mixamorig:LeftLeg",
    "lloleg":        "mixamorig:LeftLeg",
    "l_loleg":       "mixamorig:LeftLeg",
    "lfoot":         "mixamorig:LeftFoot",
    "l_foot":        "mixamorig:LeftFoot",
    "ltoebase":      "mixamorig:LeftToeBase",
    "l_toebase":     "mixamorig:LeftToeBase",
    "ltoe":          "mixamorig:LeftToeBase",
    # ── Right leg ──────────────────────────────────────────────────────────
    "rthigh":        "mixamorig:RightUpLeg",
    "r_thigh":       "mixamorig:RightUpLeg",
    "rupleg":        "mixamorig:RightUpLeg",
    "r_upleg":       "mixamorig:RightUpLeg",
    "rshin":         "mixamorig:RightLeg",
    "r_shin":        "mixamorig:RightLeg",
    "rloleg":        "mixamorig:RightLeg",
    "r_loleg":       "mixamorig:RightLeg",
    "rfoot":         "mixamorig:RightFoot",
    "r_foot":        "mixamorig:RightFoot",
    "rtoebase":      "mixamorig:RightToeBase",
    "r_toebase":     "mixamorig:RightToeBase",
    "rtoe":          "mixamorig:RightToeBase",
    # ── Fingers ────────────────────────────────────────────────────────────
    "lfinger10":     "mixamorig:LeftHandIndex1",
    "lfinger11":     "mixamorig:LeftHandIndex2",
    "lfinger12":     "mixamorig:LeftHandIndex3",
    "lfinger20":     "mixamorig:LeftHandMiddle1",
    "lfinger21":     "mixamorig:LeftHandMiddle2",
    "lfinger22":     "mixamorig:LeftHandMiddle3",
    "lfinger30":     "mixamorig:LeftHandRing1",
    "lfinger31":     "mixamorig:LeftHandRing2",
    "lfinger32":     "mixamorig:LeftHandRing3",
    "rfinger10":     "mixamorig:RightHandIndex1",
    "rfinger11":     "mixamorig:RightHandIndex2",
    "rfinger12":     "mixamorig:RightHandIndex3",
    "rfinger20":     "mixamorig:RightHandMiddle1",
    "rfinger21":     "mixamorig:RightHandMiddle2",
    "rfinger22":     "mixamorig:RightHandMiddle3",
    "rfinger30":     "mixamorig:RightHandRing1",
    "rfinger31":     "mixamorig:RightHandRing2",
    "rfinger32":     "mixamorig:RightHandRing3",
}

# Phyxel FSM clip names (used to warn about non-FSM clips)
PHYXEL_FSM_CLIPS = {
    "idle", "walk", "run", "jump", "fall", "attack", "crouch",
    "start_walking", "strafe_left", "strafe_right",
    "turn_left", "turn_right", "turn_left_sharp", "turn_right_sharp",
    "melee_attack_down", "melee_attack_horizontal",
    "kick", "throw", "talk", "wave", "taunt",
    "pickup", "pickup_object", "put_down", "work_device",
    "open_door_in", "open_door_out",
    "sitting_idle", "stand_to_sit", "sit_to_stand",
    "climbing_down", "climbing_top",
    "jump_down", "jumping_down", "stair_up", "stair_down",
}

# Prefixes that indicate humanoid character models in KOTOR
HUMANOID_PREFIXES = (
    "n_human", "n_twilek", "n_zabrak", "n_rodian",
    "n_trand", "n_ithor", "n_duros",
    "n_comm",   # commoners: n_commm, n_commf
    "n_dark",   # dark jedi: n_darkjedim, n_darkjedif
    "n_jedi",   # jedi npcs
    "s_male", "s_female",   # base animation supermodels
    "p_", "pc",
)


# ===========================================================================
# Tiny binary reader over a bytes object
# ===========================================================================

class Buf:
    """Lightweight cursor-based reader for binary data."""
    __slots__ = ("data", "pos")

    def __init__(self, data: bytes):
        self.data = data
        self.pos  = 0

    def seek(self, p: int):   self.pos = p
    def skip(self, n: int):   self.pos += n
    def tell(self) -> int:    return self.pos

    def u8(self) -> int:
        v = self.data[self.pos]; self.pos += 1; return v

    def u16(self) -> int:
        v, = struct.unpack_from("<H", self.data, self.pos); self.pos += 2; return v

    def u32(self) -> int:
        v, = struct.unpack_from("<I", self.data, self.pos); self.pos += 4; return v

    def f32(self) -> float:
        v, = struct.unpack_from("<f", self.data, self.pos); self.pos += 4; return v

    def str_fixed(self, n: int) -> str:
        """Read up to n bytes, return as string (stops at first null)."""
        raw = self.data[self.pos: self.pos + n]; self.pos += n
        null = raw.find(b"\x00")
        return raw[: null if null >= 0 else n].decode("ascii", errors="replace")

    def str_null(self) -> str:
        """Read a null-terminated string."""
        start = self.pos
        while self.pos < len(self.data) and self.data[self.pos]:
            self.pos += 1
        s = self.data[start: self.pos].decode("ascii", errors="replace")
        self.pos += 1  # consume null
        return s

    def arr_def(self) -> tuple[int, int]:
        """Read a BioWare array definition (offset, count, count2) → (offset, count)."""
        off = self.u32(); c1 = self.u32(); self.u32()  # count2 ignored
        return off, c1


# ===========================================================================
# KEY / BIF archive extraction
# ===========================================================================

def _load_key(key_path: Path) -> tuple[list[str], dict]:
    """
    Parse chitin.key.
    Returns:
        bif_paths  — list of relative BIF file paths (index = BIF file index)
        resources  — dict mapping (resref_lower, restype) → (bif_idx, res_idx)
    """
    data = key_path.read_bytes()
    r = Buf(data)

    # Header
    r.seek(8)  # skip "KEY " + "V1  "
    bif_count     = r.u32()
    key_count     = r.u32()
    bif_table_off = r.u32()
    key_table_off = r.u32()

    # BIF table entries (12 bytes each)
    r.seek(bif_table_off)
    bif_raw = []
    for _ in range(bif_count):
        _file_size  = r.u32()
        fname_off   = r.u32()
        fname_size  = r.u16()
        _drives     = r.u16()
        bif_raw.append((fname_off, fname_size))

    # BIF filenames (stored at fname_off from start of file)
    bif_paths: list[str] = []
    for fname_off, fname_size in bif_raw:
        raw  = data[fname_off: fname_off + fname_size]
        null = raw.find(b"\x00")
        s    = raw[: null if null >= 0 else fname_size].decode("ascii", errors="replace")
        bif_paths.append(s.replace("\\", "/"))

    # Key table entries (22 bytes each: 16 resref + 2 restype + 4 resid)
    resources: dict[tuple[str, int], tuple[int, int]] = {}
    r.seek(key_table_off)
    for _ in range(key_count):
        resref_raw = data[r.pos: r.pos + 16]; r.skip(16)
        null       = resref_raw.find(b"\x00")
        resref     = resref_raw[: null if null >= 0 else 16].decode("ascii", errors="replace").lower().strip()
        restype    = r.u16()
        resid      = r.u32()
        bif_idx    = resid >> 20          # top 12 bits
        res_idx    = resid & 0x000FFFFF  # bottom 20 bits
        resources[(resref, restype)] = (bif_idx, res_idx)

    return bif_paths, resources


def _extract_from_bif(bif_path: Path, res_idx: int) -> bytes:
    """Extract one resource by its index from a BIF file."""
    data = bif_path.read_bytes()
    r    = Buf(data)

    r.seek(8)  # skip "BIFF" + "V1  "
    var_count       = r.u32()
    _fixed_count    = r.u32()
    var_table_off   = r.u32()

    # Variable resource table (16 bytes per entry)
    r.seek(var_table_off)
    for _ in range(var_count):
        entry_resid = r.u32()
        offset      = r.u32()
        filesize    = r.u32()
        _restype    = r.u32()
        if (entry_resid & 0x000FFFFF) == res_idx:
            return data[offset: offset + filesize]

    raise RuntimeError(f"Resource index {res_idx} not found in {bif_path.name}")


def get_model_bytes(game_dir: Path, model_name: str) -> tuple[bytes, bytes]:
    """Return (mdl_bytes, mdx_bytes) for a named model from the game archives."""
    key_path = game_dir / "chitin.key"
    if not key_path.exists():
        raise RuntimeError(f"chitin.key not found in {game_dir}")

    bif_paths, resources = _load_key(key_path)
    name    = model_name.lower()
    mdl_key = (name, RES_MDL)
    mdx_key = (name, RES_MDX)

    if mdl_key not in resources:
        raise RuntimeError(
            f"Model '{model_name}' not found. Use --list to see available models."
        )

    mdl_bif_idx, mdl_res_idx = resources[mdl_key]
    mdl_data = _extract_from_bif(game_dir / bif_paths[mdl_bif_idx], mdl_res_idx)

    mdx_data = b""
    if mdx_key in resources:
        mdx_bif_idx, mdx_res_idx = resources[mdx_key]
        mdx_data = _extract_from_bif(game_dir / bif_paths[mdx_bif_idx], mdx_res_idx)

    return mdl_data, mdx_data


def list_models(game_dir: Path, humanoid_only: bool = True) -> list[str]:
    """Return sorted list of model names from chitin.key."""
    key_path = game_dir / "chitin.key"
    _, resources = _load_key(key_path)
    models = [resref for (resref, restype) in resources if restype == RES_MDL]
    if humanoid_only:
        models = [m for m in models if any(m.startswith(p) for p in HUMANOID_PREFIXES)]
    return sorted(set(models))


# ===========================================================================
# MDL binary parser
# (Logic mirrors seedhartha/kotorblender io_scene_kotor/format/mdl/reader.py)
# ===========================================================================

class _MdlNode:
    __slots__ = ("number", "name", "parent_name")
    def __init__(self, number: int, name: str, parent_name: str | None):
        self.number      = number
        self.name        = name
        self.parent_name = parent_name


class _MdlAnimation:
    def __init__(self, name: str, length: float):
        self.name   = name
        self.length = length
        # bone_name → {"pos": [(t,x,y,z),...], "rot": [(t,x,y,z,w),...]}
        self.keyframes: dict[str, dict] = {}


class MdlParser:
    """Parse a binary KOTOR MDL file to extract skeleton and animations."""

    def __init__(self, mdl_data: bytes):
        self.r               = Buf(mdl_data)
        self.tsl             = False
        self.supermodel_name = ""
        self.names: list[str]                    = []
        self.node_by_number: dict[int, _MdlNode] = {}

    # ── Public ──────────────────────────────────────────────────────────────

    def parse(self) -> tuple[list[_MdlNode], list[_MdlAnimation]]:
        """Returns (nodes, animations)."""
        self._read_file_header()
        self._read_geometry_header()
        self._read_model_header()
        self._read_names()
        self._walk_nodes(self.off_root_node, None)  # builds node_by_number
        anims = self._read_animations()
        return list(self.node_by_number.values()), anims

    # ── Internal seek helper ─────────────────────────────────────────────────

    def _seek(self, off: int):
        """Seek to MDL_OFFSET + off."""
        self.r.seek(MDL_OFFSET + off)

    # ── File / geometry / model headers ─────────────────────────────────────

    def _read_file_header(self):
        self.r.seek(0)
        _sig      = self.r.u32()   # must be 0
        self.r.u32()               # mdl_size
        self.r.u32()               # mdx_size

    def _read_geometry_header(self):
        # starts at byte 12 (MDL_OFFSET)
        fn_ptr1 = self.r.u32()
        if fn_ptr1 in (MODEL_FN_PTR_1_K2_PC, MODEL_FN_PTR_1_K2_XBOX):
            self.tsl = True
        self.r.u32()                         # fn_ptr2
        self.r.str_fixed(32)                 # model_name
        self.off_root_node = self.r.u32()
        self.r.u32()                         # total_num_nodes
        self.r.skip(24)                      # runtime_arr1 + runtime_arr2
        self.r.u32()                         # ref_count
        self.r.u8()                          # model_type
        self.r.skip(3)                       # padding

    def _read_model_header(self):
        self.r.u8()                          # classification
        self.r.u8()                          # subclassification
        self.r.skip(1)                       # unknown
        self.r.u8()                          # affected_by_fog
        self.r.u32()                         # num_child_models
        anim_off, anim_count = self.r.arr_def()
        self.anim_arr_off    = anim_off
        self.anim_count      = anim_count
        self.r.u32()                         # supermodel_ref
        self.r.skip(24)                      # bounding_box (6 floats)
        self.r.f32()                         # radius
        self.r.f32()                         # scale
        self.supermodel_name = self.r.str_fixed(32).strip().lower()
        self.r.u32()                         # off_anim_root
        self.r.skip(4)                       # padding
        self.r.u32()                         # mdx_size
        self.r.u32()                         # mdx_offset
        name_off, name_count = self.r.arr_def()
        self.name_arr_off    = name_off
        self.name_arr_count  = name_count

    def _read_names(self):
        self._seek(self.name_arr_off)
        offsets = [self.r.u32() for _ in range(self.name_arr_count)]
        self.names = []
        for off in offsets:
            self._seek(off)
            self.names.append(self.r.str_null())

    # ── Model node tree (first pass: build number→name map) ──────────────────

    def _walk_nodes(self, offset: int, parent_name: str | None):
        self._seek(offset)
        _type_flags = self.r.u16()
        node_number = self.r.u16()
        name_index  = self.r.u16()
        self.r.skip(2)   # padding
        self.r.u32()     # off_root
        self.r.u32()     # off_parent
        self.r.skip(12)  # position (3 floats)
        self.r.skip(16)  # orientation (4 floats)
        children_off, children_count = self.r.arr_def()
        # skip controller_arr and controller_data_arr (not needed here)

        name = self.names[name_index] if name_index < len(self.names) else f"node_{node_number}"
        node = _MdlNode(node_number, name, parent_name)
        self.node_by_number[node_number] = node

        self._seek(children_off)
        child_offsets = [self.r.u32() for _ in range(children_count)]
        for child_off in child_offsets:
            self._walk_nodes(child_off, name)

    # ── Animations ───────────────────────────────────────────────────────────

    def _read_animations(self) -> list[_MdlAnimation]:
        if self.anim_count == 0:
            return []
        self._seek(self.anim_arr_off)
        anim_offsets = [self.r.u32() for _ in range(self.anim_count)]
        anims = []
        for off in anim_offsets:
            a = self._read_animation(off)
            if a:
                anims.append(a)
        return anims

    def _read_animation(self, offset: int) -> _MdlAnimation | None:
        self._seek(offset)
        self.r.u32()                         # fn_ptr1
        self.r.u32()                         # fn_ptr2
        name          = self.r.str_fixed(32)
        off_root_node = self.r.u32()
        self.r.u32()                         # total_num_nodes
        self.r.skip(24)                      # runtime_arr1, runtime_arr2
        self.r.u32()                         # ref_count
        self.r.u8()                          # model_type
        self.r.skip(3)                       # padding
        length        = self.r.f32()
        self.r.f32()                         # transition
        self.r.str_fixed(32)                 # anim_root
        self.r.skip(12)                      # event_arr
        self.r.skip(4)                       # padding

        if not name:
            return None

        anim = _MdlAnimation(name, length)
        self._walk_anim_nodes(off_root_node, anim)
        return anim

    def _walk_anim_nodes(self, offset: int, anim: _MdlAnimation):
        self._seek(offset)
        _type_flags   = self.r.u16()
        node_number   = self.r.u16()
        name_index    = self.r.u16()
        self.r.skip(2)   # padding
        self.r.u32()     # off_root
        self.r.u32()     # off_parent
        self.r.skip(12)  # position (3 floats) — bind pose, not keyframe
        self.r.skip(16)  # orientation (4 floats) — bind pose, not keyframe
        children_off, children_count = self.r.arr_def()
        ctrl_off,     ctrl_count     = self.r.arr_def()
        ctrl_data_off, _             = self.r.arr_def()

        name = self.names[name_index] if name_index < len(self.names) else f"node_{node_number}"

        if ctrl_count > 0 and node_number in self.node_by_number:
            pos_keys, rot_keys = self._read_controllers(ctrl_off, ctrl_count, ctrl_data_off)
            if pos_keys or rot_keys:
                anim.keyframes[name] = {"pos": pos_keys, "rot": rot_keys}

        self._seek(children_off)
        child_offsets = [self.r.u32() for _ in range(children_count)]
        for child_off in child_offsets:
            self._walk_anim_nodes(child_off, anim)

    def _read_controllers(
        self, ctrl_off: int, ctrl_count: int, data_off: int
    ) -> tuple[list, list]:
        """
        Read position and orientation keyframes from the controller data arrays.
        Returns (pos_keys, rot_keys) where each entry is (t, x, y, z[, w]).
        Mirrors load_controllers() in kotorblender reader.py.
        """
        self._seek(ctrl_off)
        ctrl_keys = []
        for _ in range(ctrl_count):
            ctrl_type      = self.r.u32()
            self.r.skip(2)                # unknown
            num_rows       = self.r.u16()
            timekeys_start = self.r.u16()
            values_start   = self.r.u16()
            num_columns    = self.r.u8()
            self.r.skip(3)                # padding
            ctrl_keys.append((ctrl_type, num_rows, timekeys_start, values_start, num_columns))

        pos_keys: list = []
        rot_keys: list = []

        for ctrl_type, num_rows, tk_start, val_start, num_cols in ctrl_keys:
            if ctrl_type not in (CTRL_BASE_POSITION, CTRL_BASE_ORIENTATION):
                continue

            # Read time values
            self._seek(data_off + 4 * tk_start)
            times = [self.r.f32() for _ in range(num_rows)]

            # Determine value layout
            if ctrl_type == CTRL_BASE_ORIENTATION and num_cols == 2:
                # Compressed quaternion: one uint32 per keyframe
                self._seek(data_off + 4 * val_start)
                for i, t in enumerate(times):
                    x, y, z, w = _decompress_quat(self.r.u32())
                    rot_keys.append((t, x, y, z, w))
            else:
                actual_cols = num_cols & 0xF
                if num_cols & CTRL_FLAG_BEZIER:
                    actual_cols *= 3   # bezier stores 3× tangent data; we only use first set
                self._seek(data_off + 4 * val_start)
                # Read all values as floats in row-major order
                all_vals = [self.r.f32() for _ in range(actual_cols * num_rows)]
                for i, t in enumerate(times):
                    row = all_vals[i * actual_cols: i * actual_cols + actual_cols]
                    if ctrl_type == CTRL_BASE_ORIENTATION:
                        if len(row) >= 4:
                            rot_keys.append((t, row[0], row[1], row[2], row[3]))
                        elif len(row) == 3:
                            x, y, z = row
                            mag2 = x*x + y*y + z*z
                            w = math.sqrt(max(0.0, 1.0 - mag2))
                            rot_keys.append((t, x, y, z, w))
                    elif ctrl_type == CTRL_BASE_POSITION:
                        if len(row) >= 3:
                            pos_keys.append((t, row[0], row[1], row[2]))

        return pos_keys, rot_keys


def _decompress_quat(comp: int) -> tuple[float, float, float, float]:
    """Decompress a packed 32-bit quaternion. From kotorblender reader.py."""
    x = ((comp & 0x7FF) / 1023.0) - 1.0
    y = (((comp >> 11) & 0x7FF) / 1023.0) - 1.0
    z = ((comp >> 22) / 511.0) - 1.0
    mag2 = x*x + y*y + z*z
    w = math.sqrt(max(0.0, 1.0 - mag2))
    return x, y, z, w


# ===========================================================================
# Bone name mapping helpers
# ===========================================================================

def _map_bone(name: str) -> str | None:
    """Map a KOTOR bone name to a Mixamo name. Returns None if unmapped."""
    lower = name.lower().strip()
    if lower in KOTOR_TO_MIXAMO:
        return KOTOR_TO_MIXAMO[lower]
    # Strip _g suffix (KOTOR K1 bone naming convention: pelvis_g, lthigh_g, etc.)
    if lower.endswith("_g"):
        without_g = lower[:-2]
        if without_g in KOTOR_TO_MIXAMO:
            return KOTOR_TO_MIXAMO[without_g]
    # Strip common BioWare prefixes and retry (with and without _g)
    for prefix in ("bone_", "b_", "bip01_", "bip01 ", "mdl_"):
        if lower.startswith(prefix):
            candidate = lower[len(prefix):]
            if candidate in KOTOR_TO_MIXAMO:
                return KOTOR_TO_MIXAMO[candidate]
            if candidate.endswith("_g") and candidate[:-2] in KOTOR_TO_MIXAMO:
                return KOTOR_TO_MIXAMO[candidate[:-2]]
    return None


# ===========================================================================
# Main conversion pipeline
# ===========================================================================

def convert(
    mdl_data: bytes,
    target_af: AnimFile,
    name_map: dict[str, str],
    only_anims: list[str] | None = None,
    scale: float = 1.0,
) -> tuple[list[Clip], dict]:
    """
    Parse MDL and retarget animations onto target_af's Mixamo skeleton.
    Returns (clips, stats).
    """
    parser = MdlParser(mdl_data)
    nodes, raw_anims = parser.parse()

    # Print the node list so user can add missing mappings if needed
    unmapped_bones: set[str] = set()

    stats = {"anims_found": len(raw_anims), "clips_written": 0,
             "channels_mapped": 0, "channels_dropped": 0}

    clips: list[Clip] = []

    for raw in raw_anims:
        # Apply --anims filter
        if only_anims and raw.name.lower() not in {a.lower() for a in only_anims}:
            continue

        clip_name = name_map.get(raw.name, raw.name.lower().replace(" ", "_"))
        clip = Clip(name=clip_name, duration=raw.length)

        for bone_name, kf in raw.keyframes.items():
            mixamo_name = _map_bone(bone_name)
            if mixamo_name is None:
                stats["channels_dropped"] += 1
                unmapped_bones.add(bone_name)
                continue

            bone_id = target_af.bone_map.get(mixamo_name)
            if bone_id is None:
                stats["channels_dropped"] += 1
                unmapped_bones.add(f"{bone_name}->{mixamo_name}(not in target)")
                continue

            stats["channels_mapped"] += 1
            ch = Channel(bone_id=bone_id)
            ch.pos_keys = [
                PosKey(t, x * scale, y * scale, z * scale)
                for (t, x, y, z) in kf.get("pos", [])
            ]
            ch.rot_keys = [
                RotKey(t, x, y, z, w)
                for (t, x, y, z, w) in kf.get("rot", [])
            ]
            if ch.pos_keys or ch.rot_keys:
                clip.channels.append(ch)

        if clip.channels:
            clips.append(clip)
            stats["clips_written"] += 1
        else:
            print(f"  Warning: '{raw.name}' — no channels survived mapping, skipped")

    stats["unmapped_bones"] = unmapped_bones
    return clips, stats


# ===========================================================================
# CLI
# ===========================================================================

def main():
    parser = argparse.ArgumentParser(
        description="Import KOTOR animations into Phyxel's humanoid.anim — no Blender needed",
        epilog=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("game_dir",
        help="Path to the KOTOR game directory (contains chitin.key)")
    parser.add_argument("--model", "-m", default=None,
        help="Model name to extract (e.g. n_humanm01). Required unless --list.")
    parser.add_argument("--list", action="store_true",
        help="List available humanoid character models and exit")
    parser.add_argument("--list-all", action="store_true",
        help="List ALL models (not just humanoid prefixes)")
    parser.add_argument("--list-anims", action="store_true",
        help="List animation names in --model and exit")
    parser.add_argument("--list-bones", action="store_true",
        help="List bone names in --model and show their mapping")
    parser.add_argument("--anims", nargs="*", default=None,
        help="Only import these animation names (e.g. --anims walk run idle)")
    parser.add_argument("--map", nargs="*", default=[], metavar="SRC=DEST",
        help="Rename clips: 'kotor_anim_name=phyxel_clip_name'")
    parser.add_argument("--scale", type=float, default=1.0,
        help="Scale factor applied to position keyframes (default: 1.0)")
    parser.add_argument("--base", default="resources/animated_characters/humanoid.anim",
        help="Base .anim to copy skeleton/model from (default: humanoid.anim)")
    parser.add_argument("--output", "-o", default=None,
        help="Output .anim path (default: <base_dir>/kotor_humanoid.anim)")
    parser.add_argument("--merge", action="store_true",
        help="Merge into base file instead of creating a new file")
    args = parser.parse_args()

    game_dir  = Path(args.game_dir)
    base_path = Path(args.base)

    if not game_dir.exists():
        print(f"Error: game directory not found: {game_dir}"); sys.exit(1)
    if not base_path.exists():
        print(f"Error: base .anim not found: {base_path}"); sys.exit(1)

    # ── --list / --list-all ──────────────────────────────────────────────────
    if args.list or args.list_all:
        models = list_models(game_dir, humanoid_only=not args.list_all)
        print(f"Found {len(models)} models:")
        for m in models:
            print(f"  {m}")
        return

    if not args.model:
        print("Error: specify --model <name>  (or use --list to browse)")
        sys.exit(1)

    # ── Extract MDL bytes ────────────────────────────────────────────────────
    print(f"Extracting '{args.model}' from {game_dir} ...")
    mdl_data, _mdx_data = get_model_bytes(game_dir, args.model)
    print(f"  MDL: {len(mdl_data):,} bytes")

    # ── Parse MDL ───────────────────────────────────────────────────────────
    print("Parsing binary MDL ...")
    mdl_parser = MdlParser(mdl_data)
    nodes, raw_anims = mdl_parser.parse()
    print(f"  {len(nodes)} nodes, {len(raw_anims)} animations")

    # ── Supermodel chain following ────────────────────────────────────────────
    # KOTOR geometry models (n_commm, p_*, etc.) have 0 animations; the
    # actual keyframes live in the supermodel (s_female01, s_male01, etc.).
    _visited = {args.model.lower()}
    while not raw_anims:
        super_name = mdl_parser.supermodel_name
        if not super_name or super_name in ("", "null", "none", "default"):
            break
        if super_name in _visited:
            print(f"  Warning: supermodel loop detected at '{super_name}', stopping")
            break
        print(f"  0 animations — following supermodel '{super_name}' ...")
        try:
            mdl_data, _ = get_model_bytes(game_dir, super_name)
        except RuntimeError as e:
            print(f"  Warning: could not load supermodel: {e}")
            break
        print(f"  Supermodel MDL: {len(mdl_data):,} bytes")
        mdl_parser = MdlParser(mdl_data)
        nodes, raw_anims = mdl_parser.parse()
        print(f"  {len(nodes)} nodes, {len(raw_anims)} animations in '{super_name}'")
        _visited.add(super_name)

    # ── --list-bones ─────────────────────────────────────────────────────────
    if args.list_bones:
        print(f"\nBone/node names in '{args.model}':")
        for node in nodes:
            mapped = _map_bone(node.name)
            tag = f"  ->  {mapped}" if mapped else "  (unmapped)"
            print(f"  {node.name:<35s}{tag}")
        return

    # ── --list-anims ─────────────────────────────────────────────────────────
    if args.list_anims:
        print(f"\nAnimations in '{args.model}':")
        for a in raw_anims:
            bone_count = len(a.keyframes)
            print(f"  {a.name:<30s}  {a.length:.3f}s  ({bone_count} animated bones)")
        return

    # ── Parse --map entries ──────────────────────────────────────────────────
    name_map: dict[str, str] = {}
    for entry in args.map:
        if "=" in entry:
            src, dst = entry.split("=", 1)
            name_map[src.strip()] = dst.strip()

    output_path = (
        Path(args.output) if args.output
        else base_path.parent / "kotor_humanoid.anim"
    )

    # ── Load target skeleton ─────────────────────────────────────────────────
    print(f"\nLoading base skeleton: {base_path}")
    target = parse_anim_file(base_path)
    print(f"  {len(target.bones)} bones, {len(target.clips)} existing clips")

    # ── Convert ──────────────────────────────────────────────────────────────
    print("\nRetargeting animations ...")
    clips, stats = convert(mdl_data, target, name_map, args.anims, args.scale)

    print(f"  Animations found    : {stats['anims_found']}")
    print(f"  Clips written       : {stats['clips_written']}")
    print(f"  Channels mapped     : {stats['channels_mapped']}")
    print(f"  Channels dropped    : {stats['channels_dropped']}")
    if stats["unmapped_bones"]:
        print(f"  Unmapped bones      : {', '.join(sorted(stats['unmapped_bones']))}")
        print(f"  (Add missing entries to KOTOR_TO_MIXAMO in this script if needed)")

    if not clips:
        print("\nNo clips produced. Try --list-anims or --list-bones to debug.")
        sys.exit(1)

    # ── Build output ─────────────────────────────────────────────────────────
    if args.merge:
        out_af = target
        print(f"\nMerging into {output_path} ...")
    else:
        out_af = AnimFile()
        out_af.bones    = target.bones
        out_af.bone_map = target.bone_map
        out_af.boxes    = target.boxes
        out_af.clips    = list(target.clips)
        print(f"\nBuilding output ({len(target.clips)} existing + {len(clips)} new clips) ...")

    for clip in clips:
        out_af.remove_clip(clip.name)
        out_af.clips.append(clip)
        print(f"  + '{clip.name}'  ({clip.duration:.3f}s, {len(clip.channels)} channels)")

    write_anim_file(out_af, output_path)
    print(f"\nWritten -> {output_path}  ({len(out_af.clips)} total clips)")

    # ── FSM hints ────────────────────────────────────────────────────────────
    fsm = [c for c in clips if c.name in PHYXEL_FSM_CLIPS]
    non = [c for c in clips if c.name not in PHYXEL_FSM_CLIPS]
    if fsm:
        print(f"\nFSM-ready clips: {', '.join(c.name for c in fsm)}")
    if non:
        print(f"Non-FSM clips  : {', '.join(c.name for c in non)}")
        print("  Use anim_editor.py or your game JSON to trigger these.")


if __name__ == "__main__":
    main()
