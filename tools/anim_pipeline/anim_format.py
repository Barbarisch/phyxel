"""Parse / write / splice the Phyxel text .anim format.

Format (mirrors engine/src/graphics/AnimationSystem.cpp loadFromFile):

    # archetype: humanoid_normal          <- header comments, top level only
    # clip_meta: <clip> key=value ...     <- parsed by AnimatedVoxelCharacter
    SKELETON
    BoneCount N
    Bone <id> <name> <parentId> px py pz qx qy qz qw sx sy sz
    MODEL
    BoxCount N
    Box <boneId> sx sy sz cx cy cz
    ANIMATION <name>
    Duration <sec>
    Speed <units/sec>                     <- optional
    RootMotion <x> <y> <z>                <- optional (0/1 flags)
    BoneChannelCount <n>
    Channel <boneId> <nPos> <nRot> <nScale>
    PosKey t x y z
    RotKey t x y z w
    ScaleKey t x y z

IMPORTANT: the C++ loader skips unknown/comment lines ONLY at top level.
Inside SKELETON/MODEL/ANIMATION sections it consumes raw lines by count,
so never emit blank lines or comments inside a section body.
"""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional


# ---------------------------------------------------------------------------
# Data model
# ---------------------------------------------------------------------------

@dataclass
class Bone:
    id: int
    name: str
    parent_id: int
    pos: tuple  # (x, y, z)
    rot: tuple  # (x, y, z, w)
    scale: tuple  # (x, y, z)


@dataclass
class BoxShape:
    bone_id: int
    size: tuple   # (x, y, z)
    center: tuple  # (x, y, z)


@dataclass
class Channel:
    bone_id: int
    pos_keys: list = field(default_factory=list)    # [(t, (x, y, z)), ...]
    rot_keys: list = field(default_factory=list)    # [(t, (x, y, z, w)), ...]
    scale_keys: list = field(default_factory=list)  # [(t, (x, y, z)), ...]


@dataclass
class Clip:
    name: str
    duration: float
    speed: Optional[float] = None
    root_motion: Optional[tuple] = None  # (x, y, z) 0/1 flags or None
    channels: list = field(default_factory=list)


@dataclass
class AnimFile:
    header_comments: list = field(default_factory=list)  # raw '# ...' lines, order preserved
    bones: list = field(default_factory=list)
    boxes: list = field(default_factory=list)
    clips: list = field(default_factory=list)

    # -- lookups ----------------------------------------------------------
    def bone_map(self) -> dict:
        return {b.name: b.id for b in self.bones}

    def bone_by_id(self, bone_id: int) -> Bone:
        return self.bones[bone_id]

    def clip(self, name: str) -> Optional[Clip]:
        for c in self.clips:
            if c.name == name:
                return c
        return None

    # -- clip_meta --------------------------------------------------------
    def clip_meta(self, clip_name: str) -> Optional[dict]:
        """Return the clip_meta key=value dict for a clip, or None."""
        for line in self.header_comments:
            parsed = _parse_clip_meta_line(line)
            if parsed and parsed[0] == clip_name:
                return parsed[1]
        return None

    def set_clip_meta(self, clip_name: str, meta: dict) -> None:
        """Replace or append the '# clip_meta:' header line for a clip."""
        new_line = "# clip_meta: " + clip_name + " " + " ".join(
            f"{k}={_fmt_meta_value(v)}" for k, v in meta.items())
        for i, line in enumerate(self.header_comments):
            parsed = _parse_clip_meta_line(line)
            if parsed and parsed[0] == clip_name:
                self.header_comments[i] = new_line
                return
        self.header_comments.append(new_line)

    # -- clip splicing ----------------------------------------------------
    def set_clip(self, clip: Clip) -> bool:
        """Replace a clip with the same name, or append. Returns True if replaced."""
        for i, c in enumerate(self.clips):
            if c.name == clip.name:
                self.clips[i] = clip
                return True
        self.clips.append(clip)
        return False

    def remove_clip(self, name: str) -> bool:
        before = len(self.clips)
        self.clips = [c for c in self.clips if c.name != name]
        return len(self.clips) != before


def _parse_clip_meta_line(line: str):
    prefix = "# clip_meta:"
    if not line.startswith(prefix):
        return None
    rest = line[len(prefix):].strip().split()
    if not rest:
        return None
    name = rest[0]
    meta = {}
    for token in rest[1:]:
        if "=" in token:
            k, v = token.split("=", 1)
            meta[k] = v
    return name, meta


def _fmt_meta_value(v) -> str:
    if isinstance(v, bool):
        return "1" if v else "0"
    if isinstance(v, float):
        return f"{v:.6f}"
    return str(v)


# ---------------------------------------------------------------------------
# Parsing
# ---------------------------------------------------------------------------

def parse(path) -> AnimFile:
    lines = Path(path).read_text(encoding="utf-8").splitlines()
    af = AnimFile()
    i = 0
    n = len(lines)

    def next_line() -> str:
        nonlocal i
        line = lines[i]
        i += 1
        return line

    while i < n:
        line = next_line()
        stripped = line.strip()
        if not stripped:
            continue
        token = stripped.split()[0]

        if token == "#" or stripped.startswith("#"):
            af.header_comments.append(stripped)
        elif token == "SKELETON":
            parts = next_line().split()
            assert parts[0] == "BoneCount", f"expected BoneCount, got {parts[0]}"
            for _ in range(int(parts[1])):
                p = next_line().split()
                # Bone id name parent px py pz qx qy qz qw sx sy sz
                af.bones.append(Bone(
                    id=int(p[1]), name=p[2], parent_id=int(p[3]),
                    pos=tuple(float(x) for x in p[4:7]),
                    rot=tuple(float(x) for x in p[7:11]),
                    scale=tuple(float(x) for x in p[11:14])))
        elif token == "MODEL":
            parts = next_line().split()
            assert parts[0] == "BoxCount", f"expected BoxCount, got {parts[0]}"
            for _ in range(int(parts[1])):
                p = next_line().split()
                af.boxes.append(BoxShape(
                    bone_id=int(p[1]),
                    size=tuple(float(x) for x in p[2:5]),
                    center=tuple(float(x) for x in p[5:8])))
        elif token == "ANIMATION":
            clip = Clip(name=stripped.split(None, 1)[1], duration=0.0)
            parts = next_line().split()
            assert parts[0] == "Duration", f"expected Duration, got {parts[0]}"
            clip.duration = float(parts[1])

            parts = next_line().split()
            if parts[0] == "Speed":
                clip.speed = float(parts[1])
                parts = next_line().split()
            if parts[0] == "RootMotion":
                clip.root_motion = (int(parts[1]), int(parts[2]), int(parts[3]))
                parts = next_line().split()
            assert parts[0] == "BoneChannelCount", \
                f"clip '{clip.name}': expected BoneChannelCount, got {parts[0]}"

            for _ in range(int(parts[1])):
                p = next_line().split()
                assert p[0] == "Channel", f"clip '{clip.name}': expected Channel, got {p[0]}"
                ch = Channel(bone_id=int(p[1]))
                n_pos, n_rot, n_scale = int(p[2]), int(p[3]), int(p[4])
                for _ in range(n_pos):
                    k = next_line().split()
                    ch.pos_keys.append((float(k[1]), tuple(float(x) for x in k[2:5])))
                for _ in range(n_rot):
                    k = next_line().split()
                    ch.rot_keys.append((float(k[1]), tuple(float(x) for x in k[2:6])))
                for _ in range(n_scale):
                    k = next_line().split()
                    ch.scale_keys.append((float(k[1]), tuple(float(x) for x in k[2:5])))
                clip.channels.append(ch)
            af.clips.append(clip)
        # unknown top-level tokens are skipped, same as the C++ loader

    return af


# ---------------------------------------------------------------------------
# Writing
# ---------------------------------------------------------------------------

def _f(v: float) -> str:
    """Shortest round-trip float formatting."""
    return repr(float(v))


def write(af: AnimFile, path) -> None:
    out = []
    for comment in af.header_comments:
        out.append(comment)

    out.append("SKELETON")
    out.append(f"BoneCount {len(af.bones)}")
    for b in af.bones:
        out.append("Bone {} {} {} {} {} {} {} {} {} {} {} {} {}".format(
            b.id, b.name, b.parent_id,
            _f(b.pos[0]), _f(b.pos[1]), _f(b.pos[2]),
            _f(b.rot[0]), _f(b.rot[1]), _f(b.rot[2]), _f(b.rot[3]),
            _f(b.scale[0]), _f(b.scale[1]), _f(b.scale[2])))

    out.append("MODEL")
    out.append(f"BoxCount {len(af.boxes)}")
    for box in af.boxes:
        out.append("Box {} {:.6f} {:.6f} {:.6f} {:.6f} {:.6f} {:.6f}".format(
            box.bone_id,
            box.size[0], box.size[1], box.size[2],
            box.center[0], box.center[1], box.center[2]))

    for clip in af.clips:
        out.append(f"ANIMATION {clip.name}")
        out.append(f"Duration {_f(clip.duration)}")
        if clip.speed is not None and clip.speed > 0.0:
            out.append(f"Speed {_f(clip.speed)}")
        if clip.root_motion is not None and any(clip.root_motion):
            rm = clip.root_motion
            out.append(f"RootMotion {rm[0]} {rm[1]} {rm[2]}")
        out.append(f"BoneChannelCount {len(clip.channels)}")
        for ch in clip.channels:
            out.append(f"Channel {ch.bone_id} {len(ch.pos_keys)} {len(ch.rot_keys)} {len(ch.scale_keys)}")
            for t, v in ch.pos_keys:
                out.append(f"PosKey {_f(t)} {_f(v[0])} {_f(v[1])} {_f(v[2])}")
            for t, v in ch.rot_keys:
                out.append(f"RotKey {_f(t)} {_f(v[0])} {_f(v[1])} {_f(v[2])} {_f(v[3])}")
            for t, v in ch.scale_keys:
                out.append(f"ScaleKey {_f(t)} {_f(v[0])} {_f(v[1])} {_f(v[2])}")

    Path(path).write_text("\n".join(out) + "\n", encoding="utf-8")


# ---------------------------------------------------------------------------
# Semantic comparison (round-trip verification)
# ---------------------------------------------------------------------------

def _close(a, b, tol=1e-9):
    return abs(a - b) <= tol * max(1.0, abs(a), abs(b))


def _keys_equal(ka, kb) -> bool:
    if len(ka) != len(kb):
        return False
    for (ta, va), (tb, vb) in zip(ka, kb):
        if not _close(ta, tb) or len(va) != len(vb):
            return False
        if not all(_close(x, y) for x, y in zip(va, vb)):
            return False
    return True


def semantically_equal(a: AnimFile, b: AnimFile) -> list:
    """Return a list of human-readable differences (empty = equal)."""
    diffs = []
    if len(a.bones) != len(b.bones):
        diffs.append(f"bone count {len(a.bones)} != {len(b.bones)}")
    else:
        for ba, bb in zip(a.bones, b.bones):
            if (ba.id, ba.name, ba.parent_id) != (bb.id, bb.name, bb.parent_id):
                diffs.append(f"bone {ba.id} identity mismatch")
            elif not all(_close(x, y) for x, y in
                         zip(ba.pos + ba.rot + ba.scale, bb.pos + bb.rot + bb.scale)):
                diffs.append(f"bone {ba.name} transform mismatch")
    if len(a.boxes) != len(b.boxes):
        diffs.append(f"box count {len(a.boxes)} != {len(b.boxes)}")
    if len(a.clips) != len(b.clips):
        diffs.append(f"clip count {len(a.clips)} != {len(b.clips)}")
    else:
        for ca, cb in zip(a.clips, b.clips):
            if ca.name != cb.name:
                diffs.append(f"clip order/name mismatch: {ca.name} != {cb.name}")
                continue
            if not _close(ca.duration, cb.duration):
                diffs.append(f"clip {ca.name}: duration mismatch")
            if (ca.speed is None) != (cb.speed is None) or \
               (ca.speed is not None and not _close(ca.speed, cb.speed)):
                diffs.append(f"clip {ca.name}: speed mismatch")
            if ca.root_motion != cb.root_motion:
                diffs.append(f"clip {ca.name}: root motion mismatch")
            if len(ca.channels) != len(cb.channels):
                diffs.append(f"clip {ca.name}: channel count mismatch")
                continue
            for cha, chb in zip(ca.channels, cb.channels):
                if cha.bone_id != chb.bone_id:
                    diffs.append(f"clip {ca.name}: channel bone mismatch")
                elif not (_keys_equal(cha.pos_keys, chb.pos_keys) and
                          _keys_equal(cha.rot_keys, chb.rot_keys) and
                          _keys_equal(cha.scale_keys, chb.scale_keys)):
                    diffs.append(f"clip {ca.name} bone {cha.bone_id}: key mismatch")
    return diffs


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def _cmd_info(args):
    af = parse(args.file)
    print(f"{args.file}")
    print(f"  bones: {len(af.bones)}  boxes: {len(af.boxes)}  clips: {len(af.clips)}")
    print(f"  header comments: {len(af.header_comments)}")
    for clip in af.clips:
        keys = sum(len(c.pos_keys) + len(c.rot_keys) + len(c.scale_keys) for c in clip.channels)
        extras = []
        if clip.speed:
            extras.append(f"speed={clip.speed:.2f}")
        if clip.root_motion:
            extras.append(f"rootmotion={clip.root_motion}")
        meta = af.clip_meta(clip.name)
        if meta is not None:
            extras.append("meta")
        extra_str = (" [" + ", ".join(extras) + "]") if extras else ""
        print(f"  {clip.name}: {clip.duration:.2f}s, {len(clip.channels)} channels, {keys} keys{extra_str}")
    return 0


def _cmd_roundtrip(args):
    af = parse(args.file)
    tmp = Path(args.file).with_suffix(".roundtrip.anim")
    write(af, tmp)
    af2 = parse(tmp)
    diffs = semantically_equal(af, af2)
    if args.keep:
        print(f"wrote {tmp}")
    else:
        tmp.unlink()
    if diffs:
        print(f"ROUND-TRIP FAILED ({len(diffs)} differences):")
        for d in diffs[:20]:
            print(f"  {d}")
        return 1
    print("round-trip OK (semantically identical)")
    return 0


def _cmd_extract(args):
    af = parse(args.file)
    clip = af.clip(args.clip)
    if clip is None:
        print(f"clip '{args.clip}' not found; available: {[c.name for c in af.clips]}")
        return 1
    out = AnimFile(bones=af.bones, boxes=af.boxes, clips=[clip])
    meta = af.clip_meta(args.clip)
    if meta is not None:
        out.set_clip_meta(args.clip, meta)
    write(out, args.out)
    print(f"wrote {args.out} (clip '{args.clip}' + skeleton + model)")
    return 0


def _cmd_splice(args):
    target = parse(args.target)
    source = parse(args.source)
    names = args.clips.split(",") if args.clips else [c.name for c in source.clips]
    for name in names:
        clip = source.clip(name)
        if clip is None:
            print(f"clip '{name}' not in {args.source}")
            return 1
        replaced = target.set_clip(clip)
        meta = source.clip_meta(name)
        if meta is not None:
            target.set_clip_meta(name, meta)
        print(f"{'replaced' if replaced else 'added'} clip '{name}'")
    out = args.out or args.target
    write(target, out)
    print(f"wrote {out}")
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description="Phyxel .anim toolkit")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("info", help="summarize an .anim file")
    p.add_argument("file")
    p.set_defaults(fn=_cmd_info)

    p = sub.add_parser("roundtrip", help="verify parse->write->parse is lossless")
    p.add_argument("file")
    p.add_argument("--keep", action="store_true", help="keep the .roundtrip.anim output")
    p.set_defaults(fn=_cmd_roundtrip)

    p = sub.add_parser("extract", help="extract one clip (+skeleton/model) to a new file")
    p.add_argument("file")
    p.add_argument("clip")
    p.add_argument("out")
    p.set_defaults(fn=_cmd_extract)

    p = sub.add_parser("splice", help="copy clips from source into target .anim")
    p.add_argument("target")
    p.add_argument("source")
    p.add_argument("--clips", help="comma-separated clip names (default: all in source)")
    p.add_argument("--out", help="output path (default: overwrite target)")
    p.set_defaults(fn=_cmd_splice)

    args = ap.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
