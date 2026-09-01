"""ACS spec loading and resolver-level validation.

A spec is plain JSON (see specs/wolf.json). Keys starting with '_' are
documentation for the authoring workflow and are ignored here.
"""
from __future__ import annotations

import json
from pathlib import Path


class SpecError(Exception):
    """A spec problem the author must fix (unknown joint, cycle, missing
    material, ...). Always carries the offending name in the message."""


def load_spec(path) -> dict:
    with open(path, "r", encoding="utf-8") as f:
        spec = json.load(f)
    if not isinstance(spec, dict):
        raise SpecError(f"{path}: spec root must be a JSON object")
    return spec


def validate_basic(spec: dict) -> None:
    """Structural validation that needs no geometry: required keys, palette
    references, chain/joint cross-references."""
    for key in ("palette", "joints", "chains"):
        if key not in spec or not spec[key]:
            raise SpecError(f"spec is missing required key '{key}'")

    joints = spec["joints"]
    chains = spec["chains"]
    attach = spec.get("attach", {})
    palette = spec["palette"]

    for cn, names in chains.items():
        if not names:
            raise SpecError(f"chain '{cn}' is empty")
        for jn in names:
            if jn not in joints:
                raise SpecError(f"chain '{cn}' references unknown joint '{jn}'")

    for cn, host in attach.items():
        # attach entries may target chains OR loose joints
        if cn not in chains and cn not in joints:
            raise SpecError(f"attach references unknown chain/joint '{cn}'")
        if host not in joints:
            raise SpecError(f"attach host '{host}' is not a known joint")

    for cn in spec.get("mirror", []):
        if cn not in chains:
            raise SpecError(f"mirror references unknown chain '{cn}'")

    root_chains = [cn for cn in chains if cn not in attach]
    if not root_chains:
        raise SpecError("no root chain: every chain is attached, one must be free")

    for vol in spec.get("volumes", []):
        cn = vol.get("chain")
        if cn not in chains:
            raise SpecError(f"volume references unknown chain '{cn}'")
        mat = vol.get("material")
        if mat not in palette:
            raise SpecError(f"volume material '{mat}' not in palette")
        prof = vol.get("profile")
        if not prof or len(prof) < 2:
            raise SpecError(f"volume on chain '{cn}' needs a profile with >= 2 rows")
        sec = vol.get("section")
        if sec and sec not in spec.get("sections", {}):
            raise SpecError(f"volume names unknown section '{sec}'")

    for part in spec.get("parts", []):
        ptype = part.get("type")
        if ptype not in ("curve", "membrane", "fin", "eye", "paw", "spike"):
            raise SpecError(f"unknown part type '{ptype}'")
        mat = part.get("material")
        if mat not in palette:
            raise SpecError(f"part material '{mat}' not in palette")
        if ptype == "membrane":
            for rib in part.get("ribs", []):
                if rib.get("chain") not in chains:
                    raise SpecError(
                        f"membrane rib references unknown chain '{rib.get('chain')}'")
        elif "host" in part and part["host"] not in joints:
            raise SpecError(f"part host '{part['host']}' is not a known joint")
        anchor = part.get("anchor")
        if anchor and anchor.get("chain") not in chains:
            raise SpecError(
                f"part anchor references unknown chain '{anchor.get('chain')}'")


def hex_to_rgb(hex_color: str):
    h = hex_color.lstrip("#")
    return (int(h[0:2], 16) / 255.0, int(h[2:4], 16) / 255.0, int(h[4:6], 16) / 255.0)
