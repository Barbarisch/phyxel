#!/usr/bin/env python3
"""character_add.py — one manifest -> one playable voxel character (roadmap R2 decision 5).

    python tools/character_add.py resources/characters/<id>.json [--dry-run]
    python tools/character_add.py --check            # every manifest agrees with bindings.json

Manifest (resources/characters/<id>.json):
    id        rig stem; the rig is written to resources/animated_characters/<id>.anim
    serves    monster / race ids this character backs (their bindings point at the rig)
    size      D&D size: Tiny Small Medium Large Huge Gargantuan
    height_u  optional; clamped into the size band
    body      "humanoid" or a creature-forge species id (tools/creature_forge/specs/<body>.json)
    source    {"file": "<model.glb>"}  or  {"meshy": {"prompt": "...", "task_ids": [...]}}
    palette   max palette colours (<= 24)
    faction   optional binding faction (default "beasts" for creatures, "monsters" for humanoids)

Steps: source model -> species spec completed from the served stat blocks (decision 9) ->
voxelize (decisions 1, 2, 8) -> palette (10) -> bind onto our skeleton (3) -> write the rig ->
bindings for `serves`. It echoes one JSON object: rig, pitch, parts, budget, palette, binder,
steps, credits, species_completed, unsupported_parts, warnings.

Real runs write bindings through tools/creature_forge/bindings_map.json and regenerate
resources/monsters/visuals/bindings.json with gen_bindings.py, so its "every stat block bound
exactly once" validation still applies. Tests pass bindings_path to write a bindings file directly.

Meshy: a manifest with a meshy source reuses resources/characters/source/<id>.glb when it exists
(decision 11: committed sources), so re-runs spend nothing. Otherwise it runs text-to-3D
(preview + refine, GLB only, target_polycount 30000), refusing when credits are below 10 % of the
allotment (owner rule). Key: env MESHY_API_KEY, else the git-ignored tools/meshy.local.json.
Licence: the owner confirmed 2026-10-01 that Meshy output may ship in games (decision 7).
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "anim_pipeline"))

import anim_format as af  # noqa: E402
from tools.characters import binder, gates, palette as pal_mod, spec_complete, voxelize  # noqa: E402

MANIFEST_DIR = ROOT / "resources" / "characters"
SOURCE_DIR = MANIFEST_DIR / "source"
RIG_DIR = ROOT / "resources" / "animated_characters"
SPEC_DIR = ROOT / "tools" / "creature_forge" / "specs"
BESTIARY = ROOT / "tools" / "creature_forge" / "bestiary.json"
BINDINGS = ROOT / "resources" / "monsters" / "visuals" / "bindings.json"
BINDINGS_MAP = ROOT / "tools" / "creature_forge" / "bindings_map.json"
HUMANOID = "resources/animated_characters/humanoid.anim"
MESHY_T23D = "https://api.meshy.ai/openapi/v2/text-to-3d"
REQUIRED = ("id", "serves", "size", "body", "source")


def _rel(p: Path) -> str:
    p = Path(p).resolve()
    try:
        return p.relative_to(ROOT).as_posix()
    except ValueError:
        return p.as_posix()


def load_manifest(path) -> dict:
    m = json.loads(Path(path).read_text(encoding="utf-8"))
    missing = [k for k in REQUIRED if k not in m]
    if missing:
        raise ValueError(f"{path}: manifest missing {missing}")
    if m["size"] not in voxelize.SIZE_TABLE:
        raise ValueError(f"{path}: size {m['size']!r} not in {list(voxelize.SIZE_TABLE)}")
    if not isinstance(m["serves"], list) or not m["serves"]:
        raise ValueError(f"{path}: serves must be a non-empty list")
    n = int(m.get("palette", 24))
    if not 2 <= n <= 24:
        raise ValueError(f"{path}: palette {n} outside 2..24")
    src = m["source"]
    if not (isinstance(src, dict) and (("file" in src) ^ ("meshy" in src))):
        raise ValueError(f"{path}: source must be {{'file': ...}} or {{'meshy': {{...}}}}")
    return m


# ------------------------------------------------------------------------------------------
# needs from the served stat blocks (the R1 coverage keys)
# ------------------------------------------------------------------------------------------
def needs_for(serves) -> set:
    from coverage_report import AttackClassifier, attack_requirements
    clf = AttackClassifier()
    want = set(serves)
    needs = set()
    for p in sorted((ROOT / "resources" / "monsters").glob("*.json")):
        data = json.loads(p.read_text(encoding="utf-8"))
        for mon in data if isinstance(data, list) else []:
            if not isinstance(mon, dict) or mon.get("id") not in want:
                continue
            kinds, _ = attack_requirements(mon, clf)
            needs.update(f"attack:{k}" for k in kinds)
            for mode, on in (mon.get("movementModes") or {}).items():
                if on and mode != "walk":
                    needs.add(f"move:{mode}")
    return needs


# ------------------------------------------------------------------------------------------
# body -> target rig
# ------------------------------------------------------------------------------------------
def _bestiary_entry(body: str) -> dict | None:
    for e in json.loads(BESTIARY.read_text(encoding="utf-8")):
        if isinstance(e, dict) and e.get("spec") == f"specs/{body}.json":
            return e
    return None


def target_rig(m: dict, species_dir: Path, needs: set, write: bool = True):
    """(rig AnimFile, body_kind, species report). write=False (dry run) never touches the spec."""
    if m["body"] == "humanoid":
        return af.parse(ROOT / HUMANOID), "biped", {"rig": HUMANOID, "completed": [], "unsupported": []}
    entry = _bestiary_entry(m["body"])
    spec_path = SPEC_DIR / f"{m['body']}.json"
    if entry is None or not spec_path.exists():
        raise ValueError(f"body {m['body']!r}: no creature-forge spec + bestiary entry")
    spec_text = spec_path.read_text(encoding="utf-8")
    spec = json.loads(spec_text)
    completed, added = spec_complete.complete(spec, needs)
    unsupported = spec_complete.unsupported_needs(completed, needs)
    rig_rel = entry["out"]
    if added and write:
        # decision 9: the completed spec is written back inserting ONLY the new entries, so the
        # diff is reviewable (a re-dump turned one jaw into a 1,125-line bear.json diff)
        species_dir = Path(species_dir)
        species_dir.mkdir(parents=True, exist_ok=True)
        new_text = spec_complete.write_completed(spec_path, spec_text, completed)
        (species_dir / f"{m['body']}.json").write_text(new_text, encoding="utf-8")
    # always bind onto the skeleton compiled from the CURRENT (completed) spec: the species rig
    # file may predate an earlier import's completion (giant_lizard, 2026-10-02)
    from creature_forge.emit import Options, compile_spec
    compiled = compile_spec(completed, Options(voxel_size=entry.get("voxel_size", 0.05),
                                               target_height=entry.get("target_height"),
                                               samples=entry.get("samples", 24)))
    rig = compiled.af
    return rig, "creature", {"rig": rig_rel, "completed": added, "unsupported": unsupported}


# ------------------------------------------------------------------------------------------
# Meshy (live only; never in tests)
# ------------------------------------------------------------------------------------------
def _meshy(method: str, url: str, body: dict | None = None) -> dict:
    import meshy_credits as mc
    key = mc.api_key()
    data = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(url, data=data, method=method,
                                 headers={"Authorization": f"Bearer {key}", "Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.load(r)


def _meshy_wait(task_id: str, timeout_s: int = 1800) -> dict:
    t0 = time.time()
    while time.time() - t0 < timeout_s:
        t = _meshy("GET", f"{MESHY_T23D}/{task_id}")
        if t.get("status") == "SUCCEEDED":
            return t
        if t.get("status") in ("FAILED", "CANCELED", "EXPIRED"):
            raise RuntimeError(f"Meshy task {task_id}: {t.get('status')} {t.get('task_error')}")
        time.sleep(10)
    raise TimeoutError(f"Meshy task {task_id} still running after {timeout_s}s")


def prompt_for(m: dict, needs: set) -> str:
    """The text sent to Meshy: the manifest prompt plus a pose suffix chosen by body and needs.
    Measured 2026-10-01: one fixed suffix ("..., wings spread flat") on every creature turned the
    owlbear prompt into an upright winged owl (30 credits, binder refused at 0.970). Wings are
    asked for only when a served monster flies; a quadruped body is asked to stand on four legs."""
    prompt = m["source"]["meshy"]["prompt"]
    if m["body"] == "humanoid":
        return prompt                               # the pose comes from pose_mode a-pose
    entry = _bestiary_entry(m["body"]) or {}
    parts = []
    if entry.get("morphology") == "quadruped":
        parts.append("standing on all four legs like a bear or horse, body horizontal, side profile")
    else:
        parts.append("standing")
    parts.append("legs straight, mouth closed")
    parts.append("wings spread flat" if "move:fly" in needs else "no wings")
    return prompt + ", " + ", ".join(parts)


def fetch_meshy(m: dict, dest: Path, needs: set) -> dict:
    """Generate the model; returns {'task_ids': [...], 'consumed': N, 'balance_before', 'balance_after'}."""
    import meshy_credits as mc
    state = mc.load_state()
    before = mc.fetch_balance()
    frac, low = mc.evaluate(before, state.get("allotment"))
    if low:
        raise RuntimeError(f"Meshy credits below 10 % ({before}); owner rule: ask before spending")
    meshy = m["source"]["meshy"]
    req = {"mode": "preview", "prompt": prompt_for(m, needs),
           "target_polycount": int(meshy.get("target_polycount", 30000)), "should_remesh": True}
    if m["body"] == "humanoid":
        req["pose_mode"] = "a-pose"
    pre = _meshy("POST", MESHY_T23D, req)["result"]
    _meshy_wait(pre)
    ref = _meshy("POST", MESHY_T23D, {"mode": "refine", "preview_task_id": pre})["result"]
    done = _meshy_wait(ref)
    dest.parent.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen(done["model_urls"]["glb"], timeout=300) as r:
        dest.write_bytes(r.read())
    after = mc.fetch_balance()
    state["log"].append({"time": time.strftime("%Y-%m-%dT%H:%M:%S"), "balance": after, "label": f"character_add {m['id']}"})
    mc.save_state(state)
    _, low_after = mc.evaluate(after, state.get("allotment"))
    return {"task_ids": [pre, ref], "prompt": req["prompt"], "consumed": before - after, "balance_before": before,
            "balance_after": after, "below_10pct": low_after}


def resolve_source(m: dict, allow_spend: bool, needs: set):
    src = m["source"]
    if "file" in src:
        return ROOT / src["file"], None
    dest = SOURCE_DIR / f"{m['id']}.glb"
    if dest.exists():
        return dest, {"consumed": 0, "note": "committed source reused"}
    if not allow_spend:
        raise RuntimeError(f"{_rel(dest)} missing and spending is not allowed (--spend)")
    return dest, fetch_meshy(m, dest, needs)


# ------------------------------------------------------------------------------------------
# bindings
# ------------------------------------------------------------------------------------------
def _faction(m: dict) -> str:
    return m.get("faction", "monsters" if m["body"] == "humanoid" else "beasts")


def write_bindings_direct(m: dict, rig_rel: str, bindings_path: Path) -> None:
    j = json.loads(Path(bindings_path).read_text(encoding="utf-8")) if Path(bindings_path).exists() else {}
    for sid in m["serves"]:
        entry = dict(j.get(sid, {}))
        entry["animFile"] = rig_rel
        entry.setdefault("faction", _faction(m))
        j[sid] = entry
    Path(bindings_path).write_text(json.dumps(j, indent=2) + "\n", encoding="utf-8")


# Overrides that tuned a STAND-IN rig's look or clips. A manifest rig carries its own colours and
# size, so they are dropped on the move (measured 2026-10-01: the owlbear inherited forge_bear's
# tint [0.85, 0.75, 0.55] and scale 1.1).
STAND_IN_OVERRIDES = ("tint", "alpha", "scale", "appearance", "animationMapping", "approx")


def write_bindings_via_map(m: dict, rig_rel: str, map_path: Path = None, regenerate: bool = True) -> dict:
    """Move every served id into an archetype for this rig, dropping stand-in overrides, then
    regenerate bindings.json (gen_bindings validates coverage + playability). Returns
    {served id: {dropped override: old value}}."""
    map_path = Path(map_path) if map_path is not None else BINDINGS_MAP
    mp = json.loads(map_path.read_text(encoding="utf-8"))
    arch = mp["archetypes"]
    moved = {}
    for name, a in arch.items():
        for sid in m["serves"]:
            if sid in a.get("members", {}):
                moved[sid] = a["members"].pop(sid)
    dropped = {}
    for sid, mem in moved.items():
        if isinstance(mem, dict):
            gone = {k: mem.pop(k) for k in STAND_IN_OVERRIDES if k in mem}
            if gone:
                dropped[sid] = gone
    key = m["id"]
    tgt = arch.setdefault(key, {"animFile": rig_rel, "faction": _faction(m), "members": {}})
    tgt["animFile"] = rig_rel
    for sid in m["serves"]:
        tgt["members"][sid] = moved.get(sid, {})
    # indent=2 with ASCII escapes reproduces the checked-in map byte-for-byte (measured)
    map_path.write_text(json.dumps(mp, indent=2) + "\n", encoding="utf-8")
    if regenerate:
        import subprocess
        r = subprocess.run([sys.executable, str(ROOT / "tools/creature_forge/gen_bindings.py")],
                           capture_output=True, text=True, cwd=ROOT)
        if r.returncode != 0:
            raise RuntimeError(f"gen_bindings failed:\n{r.stdout}\n{r.stderr}")
    return dropped


def check_bindings(manifests, bindings_path=BINDINGS, rig_dir=RIG_DIR) -> list:
    """Every id a manifest serves must be bound to that manifest's rig."""
    j = json.loads(Path(bindings_path).read_text(encoding="utf-8"))
    problems = []
    for mp in manifests:
        m = load_manifest(mp)
        if m.get("status") == "refused":
            continue
        want = _rel(Path(rig_dir) / f"{m['id']}.anim")
        for sid in m["serves"]:
            got = j.get(sid, {}).get("animFile")
            if got != want:
                problems.append(f"{sid}: bound to {got!r}, manifest {_rel(mp)} says {want!r}")
    return problems


# ------------------------------------------------------------------------------------------
def run(manifest_path, out_dir=RIG_DIR, bindings_path=None, species_dir=SPEC_DIR,
        allow_spend=False, dry_run=False) -> dict:
    m = load_manifest(manifest_path)
    needs = needs_for(m["serves"])
    src, credits = resolve_source(m, allow_spend, needs)
    rig, body_kind, species = target_rig(m, Path(species_dir), needs, write=not dry_run)

    vox = voxelize.voxelize(src, m["size"], body_kind, m.get("height_u"))
    budget = voxelize.SIZE_TABLE[m["size"]]["budget"]
    pal, idx = pal_mod.quantize(vox.colors, max_colors=int(m.get("palette", 24)))
    anim, metrics = binder.bind(vox, rig, body_kind, palette_rgb=pal, palette_index=idx)

    warnings = []
    bind_ok = metrics["within_frac"] >= 0.98 and metrics["side_violations"] == 0 \
        and metrics["feet_ground_err_u"] <= 0.05 * vox.extent_u
    gate = {"aesthetic": gates.aesthetic(anim, vox.pitch), "lint": gates.lint_delta(rig, anim)}
    passed = bind_ok and all(g["ok"] for g in gate.values())
    if not bind_ok:
        why = []
        if metrics["within_frac"] < 0.98:
            why.append(f"within {metrics['within_frac']} < 0.98")
        if metrics["side_violations"]:
            why.append(f"{metrics['side_violations']} side violations")
        if metrics["feet_ground_err_u"] > 0.05 * vox.extent_u:
            why.append(f"feet {metrics['feet_ground_err_u']} u above ground > {0.05 * vox.extent_u:.3f}")
        warnings.append("binder below the pass bar (" + ", ".join(why) + "): rig NOT written")
    for name, g in gate.items():
        if not g["ok"]:
            warnings.append(f"{name} gate failed: rig NOT written ({g.get('reason') or g.get('new_errors')})")
    if species["completed"]:
        warnings.append(f"species spec {m['body']} completed with {species['completed']}: regenerate "
                        f"{species['rig']} (gen_creature.py --only), the clip fixture "
                        f"(PHYXEL_WRITE_CLIP_FIXTURE=1) and the checklist")
    if species["unsupported"]:
        warnings.append(f"needs not auto-added (no part type yet): {species['unsupported']}")

    bindings_dropped = {}
    out_dir = Path(out_dir)
    rig_path = out_dir / f"{m['id']}.anim"
    anim.header_comments = [
        f"# GENERATED by tools/character_add.py from {_rel(manifest_path)} - do not hand-edit",
        f"# source {m['source'].get('file') or _rel(src)}; body {m['body']} ({species['rig']}); "
        f"size {m['size']}; pitch 1/{round(1 / vox.pitch)}; {len(vox.centers)} parts; palette {len(pal)}",
    ] + [c for c in anim.header_comments if "GENERATED" not in c]   # keep archetype + clip_meta
    if not dry_run:
        # the manifest records the verdict: refused imports serve nothing (check_bindings skips them)
        mj = json.loads(Path(manifest_path).read_text(encoding="utf-8"))
        if credits and credits.get("task_ids") and "meshy" in mj.get("source", {}):
            mj["source"]["meshy"]["task_ids"] = credits["task_ids"]   # decision 5: traceable source
        if passed:
            mj.pop("status", None); mj.pop("refused_reason", None)
        else:
            mj["status"] = "refused"
            mj["refused_reason"] = "; ".join(w for w in warnings if "NOT written" in w)
        Path(manifest_path).write_text(json.dumps(mj, indent=2) + "\n", encoding="utf-8")
    if not passed and not dry_run and Path(out_dir).resolve() == RIG_DIR.resolve():
        # a refusal must not leave an EARLIER pass live: giant_lizard passed the first batch run,
        # was refused on the re-run, and kept its rig + binding (2026-10-02)
        bound = json.loads(BINDINGS.read_text(encoding="utf-8")) if BINDINGS.exists() else {}
        if rig_path.exists() or any(bound.get(sid, {}).get("animFile") == _rel(rig_path) for sid in m["serves"]):
            refuse(manifest_path, "; ".join(w for w in warnings if "NOT written" in w))
    if passed and not dry_run:
        out_dir.mkdir(parents=True, exist_ok=True)
        af.write(anim, rig_path)
        rig_rel = _rel(rig_path)
        if bindings_path is not None:
            write_bindings_direct(m, rig_rel, Path(bindings_path))
        else:
            bindings_dropped = write_bindings_via_map(m, rig_rel)

    return {"rig": _rel(rig_path), "written": bool(passed and not dry_run), "pitch": vox.pitch,
            "parts": len(vox.centers), "budget": budget, "palette": len(pal), "binder": metrics,
            "steps": vox.steps, "credits": credits, "needs": sorted(needs),
            "species_completed": species["completed"], "unsupported_parts": species["unsupported"],
            "bindings_dropped": bindings_dropped,
            "gates": gate, "warnings": warnings}


ORACLE_VERDICTS = ROOT / "build" / "coverage" / "import_oracle.json"


def _pre_import_member(sid: str):
    """(archetype key, archetype minus members, member) for `sid` from the NEWEST committed
    bindings_map in which it is NOT bound to a manifest-backed rig. HEAD alone is wrong once an
    import has been committed: it points at the rig being refused (orc, 2026-10-02)."""
    import subprocess
    manifest_rigs = {p.stem for p in MANIFEST_DIR.glob("*.json")}
    log = subprocess.run(["git", "log", "--format=%H", "--", "tools/creature_forge/bindings_map.json"],
                         capture_output=True, text=True, cwd=ROOT).stdout.split()
    for rev in log:
        r = subprocess.run(["git", "show", f"{rev}:tools/creature_forge/bindings_map.json"],
                           capture_output=True, text=True, encoding="utf-8", cwd=ROOT)
        if r.returncode != 0:
            continue
        for key, a in json.loads(r.stdout).get("archetypes", {}).items():
            if sid in a.get("members", {}):
                if Path(a.get("animFile", "")).stem in manifest_rigs:
                    break           # still on an import in this revision: look further back
                return key, {k: v for k, v in a.items() if k != "members"}, a["members"][sid]
    return None


def _head_map() -> dict:
    """Test hook: a fixed map instead of git history."""
    return None


def refuse(manifest_path, reason: str, map_path: Path = None, regenerate: bool = True,
           head_map: dict = None, rig_dir: Path = RIG_DIR) -> dict:
    """A written import that fails a later gate (the C++ oracle) is refused WITH ITS REASON: the
    manifest records it, the rig is removed (rebuildable from the committed source model), and
    every id it served goes back to the archetype it had in git HEAD (with its old overrides)."""
    mp = Path(manifest_path)
    m = json.loads(mp.read_text(encoding="utf-8"))
    m["status"] = "refused"
    m["refused_reason"] = reason
    mp.write_text(json.dumps(m, indent=2) + "\n", encoding="utf-8")
    rig = Path(rig_dir) / f"{m['id']}.anim"
    if rig.exists():
        rig.unlink()
    map_path = Path(map_path) if map_path is not None else BINDINGS_MAP
    cur = json.loads(map_path.read_text(encoding="utf-8"))
    restored = {}
    for sid in m["serves"]:
        for a in cur["archetypes"].values():
            a.get("members", {}).pop(sid, None)
        if head_map is not None:
            src = next(((k, {kk: vv for kk, vv in a.items() if kk != "members"}, a["members"][sid])
                        for k, a in head_map.get("archetypes", {}).items() if sid in a.get("members", {})), None)
        else:
            src = _pre_import_member(sid)
        if src is None:
            continue
        key, arch_meta, member = src
        tgt = cur["archetypes"].setdefault(key, dict(arch_meta) | {"members": {}})
        tgt.setdefault("members", {})[sid] = member
        restored[sid] = key
    # drop archetypes this manifest created that are now empty
    if m["id"] in cur["archetypes"] and not cur["archetypes"][m["id"]].get("members"):
        del cur["archetypes"][m["id"]]
    map_path.write_text(json.dumps(cur, indent=2) + "\n", encoding="utf-8")
    if regenerate:
        import subprocess
        r = subprocess.run([sys.executable, str(ROOT / "tools/creature_forge/gen_bindings.py")],
                           capture_output=True, text=True, cwd=ROOT)
        if r.returncode != 0:
            raise RuntimeError(f"gen_bindings failed:\n{r.stdout}\n{r.stderr}")
    return {"id": m["id"], "reason": reason, "restored": restored}


def refuse_oracle_failures(verdicts_path: Path = ORACLE_VERDICTS) -> list:
    v = json.loads(Path(verdicts_path).read_text(encoding="utf-8"))
    out = []
    for id_, r in sorted(v.items()):
        if not r.get("pass", True):
            out.append(refuse(MANIFEST_DIR / f"{id_}.json", "oracle gate: " + "; ".join(r.get("reasons", []))))
    return out


def run_batch(manifests, allow_spend=False, credit_check=None) -> list:
    """R2 stress batch: every manifest is imported or refused WITH A REASON; one failure never
    stops the batch. Credits are checked BETWEEN items (owner rule): a reading below 10 % of the
    allotment stops the batch before the next Meshy job and is reported as the reason."""
    import traceback
    rows = []
    for i, mp in enumerate(manifests):
        if allow_spend:
            low = credit_check() if credit_check else _credits_low()
            if low:
                rows.extend({"manifest": _rel(Path(x)), "status": "not run", "reason": low} for x in manifests[i:])
                break
        try:
            r = run(mp, allow_spend=allow_spend)
            reasons = [w for w in r["warnings"] if "NOT written" in w]
            rows.append({"manifest": _rel(Path(mp)), "status": "imported" if r["written"] else "refused",
                         "reason": "; ".join(reasons) or None, "report": r})
        except Exception as e:  # noqa: BLE001 - a refusal with its reason, never a silent stop
            rows.append({"manifest": _rel(Path(mp)), "status": "refused",
                         "reason": f"{type(e).__name__}: {e}", "trace": traceback.format_exc()[-800:]})
    return rows


def _credits_low():
    import meshy_credits as mc
    state = mc.load_state()
    bal = mc.fetch_balance()
    _, low = mc.evaluate(bal, state.get("allotment"))
    return f"Meshy credits below 10 % ({bal}): stopped before spending more" if low else None


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("manifest", nargs="?")
    ap.add_argument("--check", action="store_true", help="verify every manifest's bindings")
    ap.add_argument("--dry-run", action="store_true", help="measure, write nothing")
    ap.add_argument("--spend", action="store_true", help="allow Meshy credit spend for a missing source")
    ap.add_argument("--batch", nargs="+", help="import several manifests; each passes or is refused with a reason")
    ap.add_argument("--out", type=Path, help="write the batch report JSON here")
    ap.add_argument("--refuse-oracle-failures", action="store_true",
                    help="refuse every written import that build/coverage/import_oracle.json fails")
    a = ap.parse_args(argv)
    if a.check:
        mans = sorted(p for p in MANIFEST_DIR.glob("*.json")) if MANIFEST_DIR.is_dir() else []
        probs = check_bindings(mans) if mans else []
        for p in probs:
            print("MISMATCH", p)
        print(f"{len(mans)} manifests, {len(probs)} mismatches")
        return 1 if probs else 0
    if a.refuse_oracle_failures:
        for r in refuse_oracle_failures():
            print(f"refused {r['id']}: {r['reason']}  (restored {r['restored']})")
        return 0
    if a.batch:
        rows = run_batch(a.batch, allow_spend=a.spend)
        if a.out:
            a.out.write_text(json.dumps(rows, indent=1, default=str), encoding="utf-8")
        for r in rows:
            print(f"{r['status']:9s} {r['manifest']}  {r.get('reason') or ''}")
        return 0
    if not a.manifest:
        ap.error("manifest path required")
    rep = run(a.manifest, allow_spend=a.spend, dry_run=a.dry_run)
    print(json.dumps(rep, indent=2))
    return 0 if rep["written"] or a.dry_run else 1


if __name__ == "__main__":
    sys.exit(main())
