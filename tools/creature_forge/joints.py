"""Joint resolution: relational spec joints -> absolute world positions.

Faithful port of anyCreature engine/core/relative.js: three legal joint forms
(absolute [x,y,z]; {from, side/up/fwd, ground}; {from, dir, len}), resolved by
iterating to a fixed point so declaration order never matters. `ground` is an
ABSOLUTE world Y that overrides the offset result. Axis convention matches
Phyxel: side->X (+X = left), up->Y, fwd->Z (+Z = model forward).
"""
from __future__ import annotations

import math

from .spec import SpecError


def _norm(v):
    l = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])
    if l < 1e-12:
        return (0.0, 0.0, 0.0)
    return (v[0] / l, v[1] / l, v[2] / l)


def _resolve_map(defs: dict, lookup_extra: dict | None = None) -> dict:
    resolved = {}
    pending = []
    for name, d in defs.items():
        if name.startswith("_"):
            continue          # annotation key, same convention as every other section
        if isinstance(d, (list, tuple)):
            resolved[name] = (float(d[0]), float(d[1]), float(d[2]))
        else:
            pending.append(name)

    def look(name):
        if name in resolved:
            return resolved[name]
        if lookup_extra and name in lookup_extra:
            return lookup_extra[name]
        return None

    for _ in range(len(pending) + 1):
        if not pending:
            break
        progress = False
        for name in list(pending):
            d = defs[name]
            src = d.get("from")
            if src is None or (src not in defs and
                               not (lookup_extra and src in lookup_extra)):
                raise SpecError(f"joint '{name}' references unknown joint '{src}'")
            base = look(src)
            if base is None:
                continue  # defer to a later round
            if "dir" in d and "len" in d:
                nd = _norm(d["dir"])
                p = [base[0] + nd[0] * d["len"],
                     base[1] + nd[1] * d["len"],
                     base[2] + nd[2] * d["len"]]
            else:
                p = [base[0] + float(d.get("side", 0.0)),
                     base[1] + float(d.get("up", 0.0)),
                     base[2] + float(d.get("fwd", 0.0))]
            if "ground" in d:
                p[1] = float(d["ground"])
            resolved[name] = tuple(p)
            pending.remove(name)
            progress = True
        if pending and not progress:
            raise SpecError(f"joint dependency cycle involving: {sorted(pending)}")
    return resolved


def resolve(spec: dict):
    """Return (joints, joints_R): absolute positions for spec['joints'] and
    the optional right-side bind overrides in spec['joints_R'] (which may
    resolve `from` against either map, preferring the R side)."""
    joints = _resolve_map(spec["joints"])
    joints_r = {}
    if spec.get("joints_R"):
        joints_r = _resolve_map(spec["joints_R"], lookup_extra=joints)
    return joints, joints_r


def mirror_name(name: str) -> str:
    """Side-name mirroring: L-prefix (anyCreature convention, LFrontPaw ->
    RFrontPaw) or _L suffix (engine arachnid convention, leg1_coxa_L ->
    leg1_coxa_R)."""
    if name.endswith("_L"):
        return name[:-2] + "_R"
    if name.startswith("L"):
        return "R" + name[1:]
    return name
