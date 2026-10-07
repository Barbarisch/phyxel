"""gates.py — the per-import gates of roadmap R2 (docs/CharacterAnimationRoadmap.md, R2 contract):
"voxel aesthetic (sub-voxel detail rule), lint, bone-name independence, oracle on idle/walk,
live spawn screenshot at a stated pose".

Offline here (run by character_add before a rig is written):
  aesthetic  every box is one voxel cube of the import's pitch, and the pitch is finer than a
             subcube (1/3) — detail assets never use full-cube or subcube blocks (CLAUDE.md hard rule)
  lint       tools/anim_pipeline/anim_lint.py on every clip; the import may not add an ERROR its
             source skeleton's clips do not already have (the clips are the source's, re-keyed)

Elsewhere:
  bone-name independence  tests/scene/BodyPlanScopeTest.cpp over rig_scope.json (imports are
                          gameplay rigs through bindings.json; tools/anim_pipeline/rig_scope.py)
  oracle on idle/walk     tests/stress/ImportedRigOracleTest.cpp (C++ MotionOracle), relative to
                          the source skeleton's own clips
  live spawn screenshot   the per-import L4 review (5 / 20 / 40 u)
"""
from __future__ import annotations

import collections
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "anim_pipeline"))
import anim_lint  # noqa: E402

SUBCUBE = 1.0 / 3.0


def aesthetic(anim, pitch: float) -> dict:
    bad = [i for i, b in enumerate(anim.boxes)
           if any(abs(s - pitch) > 1e-5 for s in b.size)]
    ok = pitch < SUBCUBE - 1e-9 and not bad
    return {"ok": ok, "pitch": pitch, "non_voxel_boxes": len(bad),
            "reason": None if ok else (f"pitch {pitch:.4f} is not finer than a subcube" if pitch >= SUBCUBE - 1e-9
                                       else f"{len(bad)} boxes are not {pitch:.4f} cubes")}


def _errors(anim) -> collections.Counter:
    out = collections.Counter()
    for c in anim.clips:
        for sev, msg in anim_lint.lint_clip(anim, c):
            if sev == "ERROR":
                out[(c.name, msg.split(":")[0])] += 1
    return out


def lint_delta(source, imported) -> dict:
    src, imp = _errors(source), _errors(imported)
    new = sorted(f"{clip}: {what}" for (clip, what) in imp if (clip, what) not in src)
    return {"ok": not new, "source_errors": sum(src.values()), "import_errors": sum(imp.values()),
            "new_errors": new[:20]}
