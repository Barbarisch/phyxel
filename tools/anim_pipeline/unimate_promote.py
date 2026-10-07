#!/usr/bin/env python3
"""Apply review-ledger verdicts to a Phyxel .anim rig (docs/UniMateIntegrationPlan.md M1b).

Data-driven successor of the per-pack ``promote_*.py`` scripts. Reads
``unimate_review.json`` (tools/anim_pipeline/unimate_ledger.py) and, for every entry whose
``rig`` matches the target file:

  reject   -> remove the clip and its ``# clip_meta:`` + ``# unimate_prompt:`` header lines
  accept   -> keep as a variant, or rename to ``promote_to`` (the FSM name); the provenance
              lines follow the new name and gain ``promoted_from=<old>``
  pending  -> REFUSE the whole run and list them: nothing is written until every generated
              clip has a verdict

Further refusals (nothing written, exit 2):
  * a ``promote_to`` that would replace a clip NOT in the ``unimate_`` namespace unless the
    entry says ``replace_shipped: true`` — the way a draft could silently overwrite mocap
  * a ``unimate_*`` clip in the rig that has NO ledger entry — an unreviewed generated clip

Usage:
    python tools/anim_pipeline/unimate_promote.py [--target humanoid.anim] [--ledger ...] [--dry-run]
Exit 0 = applied (or nothing to do), 2 = refused.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from anim_format import AnimFile, parse, write  # noqa: E402
import unimate_ledger  # noqa: E402
from unimate_import import DEFAULT_TARGET, REQUIRED_PREFIX, PROMPT_HEADER  # noqa: E402


def _prompt_line(af: AnimFile, clip: str) -> str | None:
    prefix = f"{PROMPT_HEADER} {clip} "
    return next((h for h in af.header_comments if h.startswith(prefix)), None)


def _remove_prompt_line(af: AnimFile, clip: str) -> None:
    prefix = f"{PROMPT_HEADER} {clip} "
    af.header_comments = [h for h in af.header_comments if not h.startswith(prefix)]


def plan(af: AnimFile, ledger: dict, rig: str) -> dict:
    """Decide what would happen. Returns {'blocked': [...], 'actions': [...]} — actions are
    (kind, clip, target) with kind in kept|promoted|removed."""
    blocked, actions = [], []
    ledgered = {n for n, e in ledger["clips"].items() if e.get("rig") == rig}
    for c in af.clips:
        if c.name.startswith(REQUIRED_PREFIX) and c.name not in ledgered:
            blocked.append(f"{c.name}: generated clip with no ledger entry (import it through "
                           f"unimate_import.py or add a verdict)")
    for name, e in sorted(ledger["clips"].items()):
        if e.get("rig") != rig or e.get("applied"):
            continue
        if af.clip(name) is None:
            continue  # ledger knows a clip the rig no longer has — nothing to apply
        v = e.get("verdict", "pending")
        if v == "pending":
            blocked.append(f"{name}: verdict pending")
        elif v == "reject":
            actions.append(("removed", name, None))
        elif v == "accept":
            to = e.get("promote_to")
            if not to or to == name:
                actions.append(("kept", name, None))
                continue
            existing = af.clip(to)
            if existing is not None and not to.startswith(REQUIRED_PREFIX) \
                    and not e.get("replace_shipped"):
                blocked.append(f"{name}: promote_to {to!r} would replace a shipped clip; set "
                               f"replace_shipped: true in the ledger to allow it")
                continue
            actions.append(("promoted", name, to))
        else:
            blocked.append(f"{name}: unknown verdict {v!r}")
    return {"blocked": blocked, "actions": actions}


def apply(af: AnimFile, ledger: dict, actions: list) -> None:
    stamp = unimate_ledger.now_iso()
    for kind, name, to in actions:
        e = ledger["clips"][name]
        if kind == "removed":
            af.remove_clip(name)
            af.remove_clip_meta(name)
            _remove_prompt_line(af, name)
            e["applied"] = "removed"
        elif kind == "kept":
            e["applied"] = "kept"
        elif kind == "promoted":
            clip = af.clip(name)
            meta = af.clip_meta(name) or {}
            prompt = _prompt_line(af, name)
            af.remove_clip(name)
            af.remove_clip_meta(name)
            _remove_prompt_line(af, name)
            if af.clip(to) is not None:          # replace_shipped == True (checked in plan)
                af.remove_clip(to)
                af.remove_clip_meta(to)
            clip.name = to
            af.set_clip(clip)
            meta["promoted_from"] = name
            meta["review"] = "approved"      # roadmap R1 decision 5: promotion IS the owner's approval
            af.set_clip_meta(to, meta)
            if prompt:
                af.header_comments.append(prompt.replace(f"{PROMPT_HEADER} {name} ",
                                                         f"{PROMPT_HEADER} {to} ", 1))
            e["applied"] = f"promoted:{to}"
        e["applied_at"] = stamp


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--target", type=Path, default=DEFAULT_TARGET)
    ap.add_argument("--ledger", type=Path, default=unimate_ledger.DEFAULT_LEDGER)
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    af = parse(a.target)
    ledger = unimate_ledger.load(a.ledger)
    p = plan(af, ledger, a.target.name)
    summary = {"target": str(a.target), "blocked": p["blocked"],
               "promoted": [(n, t) for k, n, t in p["actions"] if k == "promoted"],
               "removed": [n for k, n, _ in p["actions"] if k == "removed"],
               "kept": [n for k, n, _ in p["actions"] if k == "kept"],
               "written": False}
    if p["blocked"]:
        print("REFUSED — nothing written:")
        for b in p["blocked"]:
            print("  -", b)
        print(json.dumps(summary, indent=2))
        return 2
    if not p["actions"]:
        print("nothing to apply")
        print(json.dumps(summary, indent=2))
        return 0
    if a.dry_run:
        print("dry run —", json.dumps(summary, indent=2))
        return 0
    apply(af, ledger, p["actions"])
    write(af, a.target)
    unimate_ledger.save(ledger, a.ledger)
    summary["written"] = True
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
