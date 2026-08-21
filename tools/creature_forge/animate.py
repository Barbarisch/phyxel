"""Animation compile: ACS degree tracks -> .anim Pos/Rot channels.

Port of anyCreature engine/core/anim.js:
* tracks: {joint: {rx/ry/rz: degrees, tx/ty/tz: metres OFFSET from bind}},
  keys are [[fraction-of-duration, value], ...]
* auto-mirror to R chains BEFORE sampling: axis flips {ry, rz, tx} -> -1,
  key times (t + mirror_phase) % 1, re-sorted, loop re-closed
* uniform resample (default 24 samples/cycle, inclusive of both ends) with
  linear key interpolation; XYZ-order Euler -> quaternion (XYZW)
* emitted key times are ABSOLUTE SECONDS (the .anim contract)

Clip naming: spec names pass through, with the engine-facing alias
{'move': 'walk'} (overridable via spec['clip_aliases']) so the character
FSM finds its locomotion clip.
"""
from __future__ import annotations

import math

from anim_format import Channel, Clip

from .joints import mirror_name
from .spec import SpecError

_FLIP_AXES = {"ry", "rz", "tx"}
_DEFAULT_ALIASES = {"move": "walk"}


def _sample_keys(keys, t):
    if not keys:
        return 0.0
    if t <= keys[0][0]:
        return keys[0][1]
    if t >= keys[-1][0]:
        return keys[-1][1]
    for i in range(len(keys) - 1):
        if keys[i][0] <= t <= keys[i + 1][0]:
            t0, v0 = keys[i]
            t1, v1 = keys[i + 1]
            f = (t - t0) / ((t1 - t0) or 1.0)
            return v0 + (v1 - v0) * f
    return keys[-1][1]


def _mirror_axis_keys(keys, phase, flip):
    shifted = sorted([((t + phase) % 1.0, v * flip) for t, v in keys],
                     key=lambda kv: kv[0])
    if shifted and shifted[0][0] > 1e-9:
        shifted.insert(0, (0.0, _sample_keys(shifted, 0.0)))
    if shifted and shifted[-1][0] < 1.0 - 1e-9:
        shifted.append((1.0, shifted[0][1]))  # re-close the loop
    return shifted


def euler_xyz_to_quat(rx_deg, ry_deg, rz_deg):
    """XYZ-order Euler degrees -> (x, y, z, w), exactly as anim.js."""
    x = rx_deg * math.pi / 360.0
    y = ry_deg * math.pi / 360.0
    z = rz_deg * math.pi / 360.0
    sx, cx = math.sin(x), math.cos(x)
    sy, cy = math.sin(y), math.cos(y)
    sz, cz = math.sin(z), math.cos(z)
    q = (sx * cy * cz + cx * sy * sz,
         cx * sy * cz - sx * cy * sz,
         cx * cy * sz + sx * sy * cz,
         cx * cy * cz - sx * sy * sz)
    n = math.sqrt(sum(c * c for c in q)) or 1.0
    return (q[0] / n, q[1] / n, q[2] / n, q[3] / n)


def compile_clips(spec, sk, bind_local, samples=24):
    """bind_local: bone name -> bind local translation (for t-track offsets).
    Returns a list of anim_format.Clip."""
    aliases = dict(_DEFAULT_ALIASES)
    aliases.update(spec.get("clip_aliases", {}))
    clips = []
    for spec_name, a in spec.get("animations", {}).items():
        duration = float(a["duration"])
        tracks = {jn: {ax: [list(k) for k in keys] for ax, keys in tr.items()}
                  for jn, tr in a.get("tracks", {}).items()}

        # auto-mirror onto R chains (author-written R tracks win)
        phase = float(a.get("mirror_phase", 0.0))
        for lchain in spec.get("mirror", []):
            for jn in spec["chains"][lchain]:
                if jn not in tracks:
                    continue
                rj = mirror_name(jn)
                if rj in tracks:
                    continue
                tracks[rj] = {
                    ax: _mirror_axis_keys(keys, phase,
                                          -1.0 if ax in _FLIP_AXES else 1.0)
                    for ax, keys in tracks[jn].items()}

        for jn in tracks:
            if jn not in sk.index:
                raise SpecError(
                    f"animation '{spec_name}' targets unknown joint '{jn}'")

        channels = []
        for jn in sorted(tracks, key=lambda n: sk.index[n]):
            tr = tracks[jn]
            ch = Channel(bone_id=sk.index[jn])
            has_rot = any(ax in tr for ax in ("rx", "ry", "rz"))
            has_pos = any(ax in tr for ax in ("tx", "ty", "tz"))
            base = bind_local[jn]
            for i in range(samples + 1):
                frac = i / samples
                t = frac * duration
                if has_rot:
                    ch.rot_keys.append((t, euler_xyz_to_quat(
                        _sample_keys(tr.get("rx", []), frac),
                        _sample_keys(tr.get("ry", []), frac),
                        _sample_keys(tr.get("rz", []), frac))))
                if has_pos:
                    ch.pos_keys.append((t, (
                        base[0] + _sample_keys(tr.get("tx", []), frac),
                        base[1] + _sample_keys(tr.get("ty", []), frac),
                        base[2] + _sample_keys(tr.get("tz", []), frac))))
            channels.append(ch)

        clips.append(Clip(name=aliases.get(spec_name, spec_name),
                          duration=duration, channels=channels))
    return clips
