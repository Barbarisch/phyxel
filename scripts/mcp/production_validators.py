"""Static (L1/L2) milestone validators for `production(op='validate')`.

Pure JSON/file checks on the project's `game.json` (+ `GAMEPLAN.md`) — NO engine. These catch the
silent-drop ship bugs (no win trigger, empty design, missing world/player) cheaply. L3/L4 runtime
validators (a trigger actually fires, a menu renders, a playtest reaches victory) are later phases
that drive the engine. Each validator declares the max static depth it can reach and returns a
Verdict: {reached: "L0".."L2", ok, evidence, issues}.

Grounded in the real schemas: triggers are `{"when":{...}, "then":[{"type": ...}]}` (TriggerSystem),
terminal actions are show_victory / show_credits / transition_scene / quit_game (the game shell's
action executor). Multi-scene defs nest a `definition` per scene.
"""
from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path

TERMINAL_ACTIONS = {"show_victory", "show_credits", "quit_game", "transition_scene"}


def _game(project_dir):
    p = Path(project_dir) / "game.json"
    if not p.is_file():
        return None
    try:
        return json.loads(p.read_text(encoding="utf-8"))
    except Exception:
        return None


def _all_defs(g):
    """The top-level def + each scene's nested definition (multi-scene)."""
    if not g:
        return []
    defs = [g]
    for s in g.get("scenes", []) or []:
        if isinstance(s.get("definition"), dict):
            defs.append(s["definition"])
    return defs


def _all_triggers(g):
    out = []
    for d in _all_defs(g):
        out += d.get("triggers", []) or []
    return out


def _V(reached, ok, evidence, issues=None):
    return {"reached": reached, "ok": ok, "evidence": evidence, "issues": issues or []}


def v_design_brief(pd, g):
    p = Path(pd) / "GAMEPLAN.md"
    if not p.is_file():
        return _V("L0", False, "no GAMEPLAN.md", ["scaffold it via `phyxel new/link`"])
    text = p.read_text(encoding="utf-8", errors="replace")
    missing = []
    for sec in ("Genre", "Core Loop", "Win / Lose"):
        m = re.search(r"^#+\s*" + re.escape(sec), text, re.M)
        if not m:
            missing.append(sec)
            continue
        body = text[m.end():]
        nxt = re.search(r"^#+\s", body, re.M)
        body = body[:nxt.start()] if nxt else body
        # Drop template placeholder lines (italic _..._) and blanks; require real prose.
        real = "\n".join(l for l in body.splitlines()
                         if l.strip() and not (l.strip().startswith("_") and l.strip().endswith("_")))
        if len(real.strip()) < 20:
            missing.append(sec)
    if missing:
        return _V("L0", False, f"GAMEPLAN sections thin/unfilled: {', '.join(missing)}",
                  [f"fill the '{s}' section of GAMEPLAN.md" for s in missing])
    return _V("L1", True, "GAMEPLAN.md core sections filled (Genre, Core Loop, Win/Lose)")


def v_world(pd, g):
    for d in _all_defs(g):
        w = d.get("world")
        if isinstance(w, dict) and w.get("type"):
            return _V("L1", True, f"world type={w['type']}")
    if g and g.get("scenes"):
        return _V("L1", True, "multi-scene world definitions present")
    return _V("L0", False, "no world block in game.json", ["add a world block (type + range)"])


def v_player(pd, g):
    for d in _all_defs(g):
        if isinstance(d.get("player"), dict):
            return _V("L1", True, f"player type={d['player'].get('type', '?')}")
    if g and isinstance(g.get("playerDefaults"), dict):
        return _V("L1", True, "playerDefaults present (multi-scene)")
    return _V("L0", False, "no player block", ["add a player block (type: animated + position)"])


def v_win_condition(pd, g):
    for t in _all_triggers(g):
        for a in (t.get("then") or []):
            if isinstance(a, dict) and a.get("type") in TERMINAL_ACTIONS:
                return _V("L2", True, f"terminal trigger action '{a['type']}' wired")
    return _V("L0", False,
              "no trigger with a terminal 'then' action (show_victory/transition_scene/quit_game)",
              ["wire a win trigger, e.g. triggers[].then = [{\"type\":\"show_victory\"}]"])


def v_main_menu(pd, g):
    for s in (g or {}).get("scenes", []) or []:
        if s.get("sceneType") == "menu":
            return _V("L2", True, f"menu scene '{s.get('id', '?')}' present")
    return _V("L1", True, "using the engine's default shell main-menu screen (no custom menu scene)",
              ["add a custom menu scene (sceneType:menu) for a game-specific main menu"])


def v_hud(pd, g):
    for d in _all_defs(g):
        if d.get("hud"):
            return _V("L1", True, "custom hud block present")
    return _V("L1", True, "using the default HUD (ships out of the box)",
              ["add a custom hud block for game-specific panels"])


def v_credits(pd, g):
    for t in _all_triggers(g):
        for a in (t.get("then") or []):
            if isinstance(a, dict) and a.get("type") == "show_credits":
                return _V("L2", True, "show_credits trigger wired")
    return _V("L1", True, "using the default shell credits screen (no game-specific credits wired)",
              ["wire show_credits (or a credits scene) for real credits"])


# milestone -> (validator fn, max static depth). Absent milestones have no static validator.
REGISTRY = {
    "design_brief": (v_design_brief, "L1"),
    "world": (v_world, "L1"),
    "player": (v_player, "L1"),
    "win_condition": (v_win_condition, "L2"),
    "main_menu": (v_main_menu, "L2"),
    "hud": (v_hud, "L1"),
    "credits": (v_credits, "L1"),
}


def validate(project_dir, milestone: str) -> dict:
    """Run the static validator for `milestone`. Returns a verdict; static=False if none exists."""
    entry = REGISTRY.get(milestone)
    if entry is None:
        return {"milestone": milestone, "static": False, "ok": None,
                "note": "no static validator — needs a runtime (L3/L4) validator (later phase) "
                        "or manual verification"}
    fn, static_max = entry
    v = fn(project_dir, _game(project_dir))
    v.update({"milestone": milestone, "static": True, "static_max": static_max})
    return v


def has_validator(milestone: str) -> bool:
    return milestone in REGISTRY


def input_digest(project_dir, milestone: str) -> str:
    """A content hash over the milestone's validation inputs (durability §8): GAMEPLAN.md for
    design_brief, else game.json. Coarse-but-honest — any game.json edit changes the hash, and the
    sweep self-heals still-passing static milestones while flagging the rest stale."""
    pd = Path(project_dir)
    f = (pd / "GAMEPLAN.md") if milestone == "design_brief" else (pd / "game.json")
    data = f.read_bytes() if f.is_file() else b""
    return "sha256:" + hashlib.sha256(data).hexdigest()[:16]
