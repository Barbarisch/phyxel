"""spec_complete.py — give a creature-forge species spec the parts its stat block needs.

Roadmap R2 decision 9 (docs/CharacterAnimationRoadmap.md): the binder uses the forge species
skeleton, so the skeleton must carry what the monsters it serves do. 0 of the 32 shipped specs have
an articulated jaw, yet Bite is the commonest attack (147 monsters).

Needs come from the R1 coverage report keys (`attack:bite`, `attack:tail`, `move:fly`, ...).
  attack:bite / attack:beak -> Jaw + JawTip joints off the head chain's root, a `jaw` chain + volume, and,
                      when the spec has an `attack` clip, a jaw-open track peaking at its hit frame
  attack:tail      -> a 3-joint `tail` chain off the body chain's root (skipped if any chain
                      already contains "tail")
  move:fly, attack:tentacle, attack:sting -> NOT auto-added yet (wings/limb sets need design per
                      species); reported by unsupported_needs() so the import says so

complete() is idempotent and deterministic: a spec that already has the part is returned unchanged.
"""
from __future__ import annotations

import copy

AUTO = {"attack:bite": "jaw", "attack:beak": "jaw", "attack:tail": "tail"}   # a beak is a jaw that opens
UNSUPPORTED = {"move:fly": "wings", "attack:tentacle": "tentacles", "attack:sting": "stinger"}


def _head_chain(spec: dict):
    for name, joints in spec.get("chains", {}).items():
        if "head" in name.lower() and joints:
            return name, joints
    return None, None


def _body_chain(spec: dict):
    chains = spec.get("chains", {})
    for pref in ("body", "spine", "torso"):
        for name, joints in chains.items():
            if pref in name.lower() and joints:
                return name, joints
    return None, None


def has_part(spec: dict, part: str) -> bool:
    chains = {k.lower() for k in spec.get("chains", {})}
    joints = {k.lower() for k in spec.get("joints", {})}
    if part == "jaw":
        return "jaw" in chains or any("jaw" in j for j in joints)
    if part == "tail":
        return any("tail" in c for c in chains)
    if part == "wings":
        return any("wing" in c for c in chains)
    return False


def unsupported_needs(spec: dict, needs) -> list:
    """Parts the needs call for that the spec still lacks after completion: the ones with no part
    type yet (wings, tentacles, stinger) AND auto parts that could not be added (a jaw needs a
    head chain; giant_spider has none — measured 2026-10-02, it used to be skipped silently)."""
    missing = {UNSUPPORTED[n] for n in needs if n in UNSUPPORTED and not has_part(spec, UNSUPPORTED[n])}
    missing |= {AUTO[n] for n in needs if n in AUTO and not has_part(spec, AUTO[n])}
    return sorted(missing)


def _add_jaw(spec: dict) -> bool:
    hname, hjoints = _head_chain(spec)
    if not hname:
        return False
    root = hjoints[0]
    head_volume = next((v for v in spec.get("volumes", []) if v.get("chain") == hname), None)
    spec["joints"]["Jaw"] = {"from": root, "up": -0.05, "fwd": 0.04}
    spec["joints"]["JawTip"] = {"from": "Jaw", "up": -0.01, "fwd": 0.16}
    spec["chains"]["jaw"] = ["Jaw", "JawTip"]
    spec.setdefault("attach", {})["jaw"] = root
    vol = {"chain": "jaw", "material": head_volume["material"] if head_volume else next(iter(spec["palette"])),
           "frame": "up", "sides": 10,
           "profile": [[0.0, 0.07, 0.035], [0.6, 0.06, 0.03], [1.0, 0.035, 0.02]],
           "caps": ["none", "dome"]}
    spec.setdefault("volumes", []).append(vol)
    atk = spec.get("animations", {}).get("attack")
    if atk is not None:
        hit = float(atk.get("hit_fraction", 0.45))
        atk.setdefault("tracks", {})["Jaw"] = {"rx": [[0, 0], [max(hit - 0.2, 0.05), 26], [hit, 30],
                                                      [min(hit + 0.15, 0.95), 6], [1, 0]]}
    return True


def _add_tail(spec: dict) -> bool:
    bname, bjoints = _body_chain(spec)
    if not bname:
        return False
    root = bjoints[0]
    spec["joints"]["TailRoot"] = {"from": root, "up": 0.0, "fwd": -0.08}
    spec["joints"]["Tail1"] = {"from": "TailRoot", "up": -0.03, "fwd": -0.2}
    spec["joints"]["TailTip"] = {"from": "Tail1", "up": -0.08, "fwd": -0.22}
    spec["chains"]["tail"] = ["TailRoot", "Tail1", "TailTip"]
    spec.setdefault("attach", {})["tail"] = root
    body_volume = next((v for v in spec.get("volumes", []) if v.get("chain") == bname), None)
    spec.setdefault("volumes", []).append({
        "chain": "tail", "material": body_volume["material"] if body_volume else next(iter(spec["palette"])),
        "frame": "up", "sides": 10, "profile": [[0.0, 0.06, 0.06], [1.0, 0.02, 0.02]], "caps": ["none", "dome"]})
    return True


def complete(spec: dict, needs) -> tuple[dict, list]:
    """Return (completed spec, parts added). Never mutates the input."""
    out = copy.deepcopy(spec)
    added = []
    for need in sorted(set(needs)):
        part = AUTO.get(need)
        if not part or has_part(out, part):
            continue
        if part == "jaw" and _add_jaw(out):
            added.append("jaw")
        elif part == "tail" and _add_tail(out):
            added.append("tail")
    return out, added


# ------------------------------------------------------------------------------------------
# format-preserving write-back: the specs are hand-aligned JSON, and decision 9 says the
# completion is "reviewed in the diff" -- re-dumping a spec turned a jaw into a 1,125-line diff
# (bear.json, 2026-10-01). Only the added entries are inserted into the original text.
# ------------------------------------------------------------------------------------------
import json as _json


def _skip_ws(t, i):
    while i < len(t) and t[i] in " \t\r\n":
        i += 1
    return i


def _value_end(t, i):
    """Index just past the JSON value starting at t[i]."""
    c = t[i]
    if c == '"':
        i += 1
        while t[i] != '"':
            i += 2 if t[i] == "\\" else 1
        return i + 1
    if c in "{[":
        depth, i = 0, i
        while True:
            ch = t[i]
            if ch == '"':
                i = _value_end(t, i)
                continue
            if ch in "{[":
                depth += 1
            elif ch in "}]":
                depth -= 1
                if depth == 0:
                    return i + 1
            i += 1
    while i < len(t) and t[i] not in ",}]\r\n":
        i += 1
    return i


def _member_span(t, obj_start, key):
    """(value start, value end) of key inside the object whose '{' is at obj_start."""
    i = _skip_ws(t, obj_start + 1)
    while t[i] != "}":
        kend = _value_end(t, i)
        k = _json.loads(t[i:kend])
        i = _skip_ws(t, kend)
        assert t[i] == ":"
        vs = _skip_ws(t, i + 1)
        ve = _value_end(t, vs)
        if k == key:
            return vs, ve
        i = _skip_ws(t, ve)
        if t[i] == ",":
            i = _skip_ws(t, i + 1)
    return None


def _container(t, path):
    start = _skip_ws(t, 0)
    for key in path:
        sp = _member_span(t, start, key)
        if sp is None:
            return None
        start = sp[0]
    return start, _value_end(t, start)


def _insert(t, path, entry_text):
    """Insert `entry_text` (a member 'k: v' or a list item) as the container's last entry,
    copying the indentation of the container's existing last line."""
    s, e = _container(t, path)
    close = e - 1
    body = t[s + 1:close]
    j = close - 1
    while j > s and t[j] in " \t\r\n":
        j -= 1
    line_start = t.rfind("\n", 0, j) + 1
    indent = ""
    k = line_start
    while t[k] in " \t":
        indent += t[k]
        k += 1
    if body.strip() == "":
        closing_indent = t[t.rfind("\n", 0, close) + 1:close] if "\n" in body else ""
        return t[:s + 1] + "\n" + closing_indent + "  " + entry_text + "\n" + closing_indent + t[close:]
    return t[:j + 1] + ",\n" + indent + entry_text + t[j + 1:]


def write_completed(path, original_text: str, completed: dict) -> str:
    """Return the original spec text with ONLY the completion's additions inserted. The result
    must parse back equal to `completed` (asserted)."""
    orig = _json.loads(original_text)
    t = original_text
    for sect in ("joints", "chains", "attach"):
        for k, v in completed.get(sect, {}).items():
            if k not in orig.get(sect, {}):
                t = _insert(t, [sect], f"{_json.dumps(k)}: {_json.dumps(v)}")
    for v in completed.get("volumes", [])[len(orig.get("volumes", [])):]:
        t = _insert(t, ["volumes"], _json.dumps(v))
    for clip, c in completed.get("animations", {}).items():
        o_tracks = orig.get("animations", {}).get(clip, {}).get("tracks", {})
        for joint, tr in c.get("tracks", {}).items():
            if joint not in o_tracks:
                t = _insert(t, ["animations", clip, "tracks"], f"{_json.dumps(joint)}: {_json.dumps(tr)}")
    if _json.loads(t) != completed:
        raise AssertionError(f"{path}: format-preserving write-back does not reproduce the completed spec")
    return t
