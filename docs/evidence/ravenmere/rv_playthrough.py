"""Ravenmere main-line playthrough over the shipped build's --test API.

Drives the packaged Release exe (no editor): menu -> Reeve (accept) -> Maera -> Bram (recruit)
-> tavern trapdoor -> cellar rats (turn-based, played by the API) -> Hobb (paid) -> east road
-> farm: Dunstan, wolves, tracks, Oswin -> barrow: antechamber fight, grimoire, crypt boss,
relic -> back to town -> Reeve hand-over -> victory.

Every step records what the API said; a step that cannot proceed records WHY and the run
continues where it can. Evidence lands in rv_playthrough_evidence.json + rv_runA.log.
Movement is keypress steering (no teleport on the standalone) - slow but honest.
"""
import math, json, math, subprocess, sys, time, urllib.request
from pathlib import Path

PORT = 8104
BASE = f"http://127.0.0.1:{PORT}"
RELDIR = Path.home() / "Documents/PhyxelProjects/Ravenmere/build/Release"
OUT = Path(__file__).parent
ev = {"runs": []}
T0 = time.time()
cur = None

# Regenerated town (2026-09-09): the cellar hatch is INSIDE the Raven's Rest (the rear toft is fenced and
# the side passages are sealed - G-59). Set from the L4 interior scan; game.json's to_cellar region matches.
HATCH_X, HATCH_Z = -26.5, 9.5

def fps_now():
    try:
        t = api("GET", "/api/debug/engine_timing", t=3)
        return round(float(t.get("fps", 0)), 1)
    except Exception:
        return None

def rec(s, d):
    if isinstance(d, dict): d = dict(d, fps=fps_now())
    cur["steps"].append({"step": s, "t": round(time.time() - T0, 1), "data": d})
    print(f"[{round(time.time()-T0,1):7.1f}] [{s}] {json.dumps(d, default=str)[:300]}", flush=True)

def api(m, p, b=None, t=15):
    d = json.dumps(b).encode() if b is not None else None
    r = urllib.request.Request(BASE + p, data=d, method=m, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(r, timeout=t) as x:
        return json.loads(x.read().decode())

def safe(m, p, b=None):
    try: return api(m, p, b)
    except Exception as e: return {"error": repr(e)}

def combat(action, body=None): return safe("POST", f"/api/rpg/combat/{action}", body or {})
def screen(): return safe("GET", "/api/screen/state")
def state(): return safe("GET", "/api/state")

def entities():
    return {e.get("id"): e for e in state().get("entities", [])}

def player_pos():
    e = entities().get("player")
    if not e: return None
    p = e["position"]; return (p["x"], p["z"])

def hp(eid):
    r = safe("POST", "/api/rpg/entity_health", {"id": eid})
    return r.get("health", r.get("hp", r))

def key(k, hold):
    keys = k if isinstance(k, list) else [k]
    r = safe("POST", "/api/input/inject", {"keys": keys, "hold": hold})
    if not r.get("injected"): rec("inject_unresolved", {"key": k, "resp": r})
    time.sleep(hold + 0.25)

def fired(trig_id):
    for t in safe("GET", "/api/triggers").get("triggers", []):
        if t.get("id") == trig_id: return t.get("fired")
    return None

def launch(tag, wipe_saves=True):
    global cur
    cur = {"tag": tag, "steps": []}
    ev["runs"].append(cur)
    if wipe_saves:
        # a fresh run must not inherit a PlayerProfile row; world DBs are the authored bake
        for f in (RELDIR / "worlds").glob("*.db*"):
            if f.name.startswith(("cellar", "farm", "barrow")):
                for _ in range(10):
                    try: f.unlink(); break
                    except PermissionError: time.sleep(1)
    fh = open(OUT / f"rv_{tag}.log", "w", encoding="utf-8", errors="replace")
    p = subprocess.Popen([str(RELDIR / "Ravenmere.exe"), "--test", str(PORT)],
                         cwd=str(RELDIR), stdout=fh, stderr=subprocess.STDOUT)
    dl = time.time() + 120
    while time.time() < dl:
        try: api("GET", "/api/state", t=3); break
        except Exception: time.sleep(1)
    return p, fh

# ── steering ──────────────────────────────────────────────────────────────────────────
def calibrate(dirs):
    # Net-ZERO calibration: tap W then S, D then A, so the player ends where it started.
    # The old W-then-D pair drifted the player ~0.75 m west per call, and precise_to
    # calibrated on every call: in run 34 that walked the player from the storeroom's
    # door line onto the cellar hatch cell 2.5 m away (the trigger fired while the
    # harness believed the player was at x=-24.5).
    for k, back in (("W", "S"), ("D", "A")):
        p0 = player_pos(); key(k, 0.35); p1 = player_pos(); key(back, 0.35)
        if p0 is None or p1 is None: continue
        dx, dz = p1[0] - p0[0], p1[1] - p0[1]
        n = math.hypot(dx, dz)
        if n > 0.05: dirs[k] = (dx / n, dz / n)
    dirs["S"] = (-dirs["W"][0], -dirs["W"][1]); dirs["A"] = (-dirs["D"][0], -dirs["D"][1])
    # chords: the 4 keys are camera-relative diagonals in world XZ, so W+D / W+A / S+D / S+A give
    # the world axes — without them a north-bound walk zig-zags +-0.5 m and misses a doorway
    for a, b in (("W", "D"), ("W", "A"), ("S", "D"), ("S", "A")):
        vx, vz = dirs[a][0] + dirs[b][0], dirs[a][1] + dirs[b][1]
        n = math.hypot(vx, vz) or 1e-9
        dirs[a + b] = (vx / n, vz / n)
    for k in list(dirs.keys()):
        if len(k) == 2 and not isinstance(k, str): pass

def steer_direct(dirs, tx, tz, tol=1.3, max_iter=90, stop=None):
    last = None; stalled = 0
    for i in range(max_iter):
        unpause()
        if stop and stop(): rec("steer_stopped", {"iter": i, "pos": player_pos()}); return "stopped"
        p = player_pos()
        if p is None: time.sleep(1.0); continue
        dx, dz = tx - p[0], tz - p[1]; dist = math.hypot(dx, dz)
        if dist < tol: rec("steer_arrived", {"iter": i, "pos": [round(p[0], 1), round(p[1], 1)]}); return "arrived"
        if last is not None and dist >= last - 0.1:
            stalled += 1
            if stalled >= 3: calibrate(dirs); stalled = 0
        else: stalled = 0
        last = dist
        best = max(dirs, key=lambda k2: (dirs[k2][0] * dx + dirs[k2][1] * dz) / dist)
        key(list(best) if len(best) == 2 else best, min(0.6, max(0.15, dist * 0.09)))
    rec("steer_stuck", {"target": [tx, tz], "pos": player_pos()})
    defect("route_walkable", pos=player_pos(), route_label=f"steer -> ({tx}, {tz})", evidence="steer_stuck: direct steering made no progress")
    return "stuck"

cur_scene = {"id": None}   # updated by steer_to at the start of every walk

DEFECTS = Path(r"C:/Users/bpete/Documents/PhyxelProjects/Ravenmere/playtest/defects.jsonl")

def defect(invariant, pos=None, route_label=None, evidence=None, source="probe"):
    """Layer C (WalkabilityGateAndPlaytestLoop increment 6): one defect record per stall,
    appended to <project>/playtest/defects.jsonl - the fix loop's input."""
    try:
        DEFECTS.parent.mkdir(parents=True, exist_ok=True)
        rec_ = {"when": time.strftime("%Y-%m-%dT%H:%M:%S"), "source": source, "world_seed": 7,
                "scene": screen().get("scene_id"), "pos": pos, "structure_id": None, "lot": None,
                "route_label": route_label, "invariant": invariant, "evidence": evidence}
        with open(DEFECTS, "a", encoding="utf-8") as f: f.write(json.dumps(rec_, default=str) + "\n")
    except Exception as e:
        rec("defect_write_failed", {"error": repr(e)})

def framed_shot(tag, cam, target):
    """Detach the camera, look from `cam` at `target`, screenshot, re-attach (inc7 visual L4)."""
    dx, dy, dz = target[0]-cam[0], target[1]-cam[1], target[2]-cam[2]
    yaw = math.degrees(math.atan2(dz, dx)); pitch = math.degrees(math.atan2(dy, math.hypot(dx, dz)))
    safe("POST", "/api/rpg/set_camera", {"detach": True, "x": cam[0], "y": cam[1], "z": cam[2], "yaw": yaw, "pitch": pitch})
    time.sleep(1.2)
    rec(tag, dict(safe("GET", "/api/screenshot"), cam=cam, target=target, yaw=round(yaw, 1), pitch=round(pitch, 1)))
    safe("POST", "/api/rpg/set_camera", {"detach": False}); time.sleep(0.5)

def world_health(tag):
    """Layer B: the shipped game's own load-time self-check (WorldHealth::check on scene ready)."""
    r = safe("POST", "/api/rpg/world_health", {})
    for a in r.get("anchors", []):
        if not a.get("reachable"):
            defect("anchor_reachable_from_spawn", pos=a.get("position"), route_label=f"spawn -> {a.get('kind')} {a.get('id')}", evidence="world_health", source="selfcheck")
    if r.get("terrain_under_spawn") is False:
        defect("terrain_under_spawn", pos=r.get("spawn"), evidence="world_health: no terrain within 8 cubes below the spawn", source="selfcheck")
    rec("world_health", {"tag": tag, "ok": r.get("ok"), "reachable": r.get("reachable"), "total": r.get("total"),
                         "terrain": r.get("terrain_under_spawn"), "error": r.get("error"),
                         "unreachable": [a.get("id") for a in r.get("anchors", []) if not a.get("reachable")][:12]})
    return r

def scene_changed(s0):
    """True once the active scene differs from `s0` (a floor trigger fired mid-walk - run 33
    walked onto the cellar hatch on the way to Bram and kept steering in the cellar, G-80)."""
    sid = screen().get("scene_id")
    return sid is not None and s0 is not None and sid != s0

def steer_to(dirs, tx, tz, tol=1.3, max_iter=90, stop=None, replans=3):
    """Walk like a player would: ask the shipped build's nav grid for a path from the player's cell
    to the target and steer waypoint by waypoint (straight-line steering walked into the hall the
    moment the town had walls - run 14). Falls back to direct steering when no path is found."""
    p = player_pos()
    if p is None: return steer_direct(dirs, tx, tz, tol, max_iter, stop)
    # The grid excludes cells next to anything > 2 blocks tall (trees, walls), so a start or
    # goal beside a trunk/wall has no links: try the exact cells first, then a small ring of
    # nearby cells for both ends (a player steps into the open before setting off).
    ring = [(0, 0), (1, 0), (-1, 0), (0, 1), (0, -1), (2, 0), (-2, 0), (0, 2), (0, -2), (2, 2), (-2, -2), (2, -2), (-2, 2), (3, 0), (-3, 0), (0, 3), (0, -3)]
    sx, sz = int(math.floor(p[0])), int(math.floor(p[1])); gx, gz = int(math.floor(tx)), int(math.floor(tz))
    wps = []; tried = 0
    for (dsx, dsz) in ring[:9]:
        for (dgx, dgz) in ring:
            r = safe("POST", "/api/rpg/navgraph_path", {"x1": sx + dsx + 0.5, "z1": sz + dsz + 0.5, "y1": 18.0, "x2": gx + dgx + 0.5, "z2": gz + dgz + 0.5, "y2": 18.0}); tried += 1   # the 3D NavGraph (what NPCs walk), not the legacy 2.5D grid
            if r.get("found") and r.get("waypoints"):
                wps = r["waypoints"]; break
        if wps: break
    if not wps:
        rec("no_path", {"from": [round(p[0], 1), round(p[1], 1)], "to": [tx, tz], "tried": tried})
        defect("route_pathable", pos=[round(p[0], 1), round(p[1], 1)], route_label=f"navgraph -> ({tx}, {tz})", evidence=f"no_path after {tried} start/goal ring tries")
        return steer_direct(dirs, tx, tz, tol, max_iter, stop)
    rec("path", {"to": [tx, tz], "waypoints": len(wps), "tried": tried})
    s0 = screen().get("scene_id"); cur_scene["id"] = s0
    # Follow the smoothed waypoints PRECISELY: the NavGraph's spans are exact for a 0.25 m
    # half-width agent and run through 1 m gaps between buildings (run 26: loose steering
    # drifted 2 m off a 17 m span and pinned the player on the tavern's fence line).
    # Tight crossings keep their cell-centre waypoints and the sweep proved them within
    # +-1 micro of the centre (the shrine gap, run 33): hit intermediate waypoints to 0.2 m.
    for w in wps[:-1]:
        if stop and stop(): return "stopped"
        if scene_changed(s0): rec("scene_changed_midwalk", {"to": [tx, tz]}); return "scene_changed"
        if not precise_to(dirs, w["x"], w["z"], tol=0.2, max_iter=60):
            # A missed intermediate waypoint must NOT be followed by a beeline to the next
            # one: run 43 lost (-17.5,2.5) by 0.4 m, then aimed straight at (-20.5,11.5)
            # through a house wall and stood against it for 5 minutes while the graph's
            # own route went west first. Re-plan from where we are.
            if replans > 0:
                rec("replan", {"missed": [w["x"], w["z"]], "pos": player_pos(), "left": replans - 1})
                return steer_to(dirs, tx, tz, tol=tol, max_iter=max_iter, stop=stop, replans=replans - 1)
    # The last span too: loose steering from the final waypoint slipped inside the tavern's
    # fence line and wedged the player between fence and wall (run 27).
    if stop and stop(): return "stopped"
    if scene_changed(s0): rec("scene_changed_midwalk", {"to": [tx, tz]}); return "scene_changed"
    ok = precise_to(dirs, tx, tz, tol=max(0.45, min(tol, 0.9)), max_iter=60)
    if stop and stop(): return "stopped"
    return "arrived" if ok else steer_direct(dirs, tx, tz, tol, max_iter=20, stop=stop)

def precise_to(dirs, tx, tz, tol=0.35, max_iter=60):
    """Short taps + frequent re-calibration: sub-half-metre arrival for door lines."""
    last = None; stalled = 0
    trace = []   # (iter, x, z, key) - dumped when the walk does not end at its target
    # No blanket calibration per call (the camera yaw is fixed, dirs are stable); only a
    # stall re-calibrates, and that is net-zero now.
    for i in range(max_iter):
        unpause()
        p = player_pos()
        if p is None: time.sleep(0.5); continue
        dx, dz = tx - p[0], tz - p[1]; dist = math.hypot(dx, dz)
        if dist < tol: return True
        if screen().get("scene_id") != cur_scene.get("id"):
            rec("precise_scene_changed", {"to": [tx, tz], "trace": trace[-12:]}); return False
        if last is not None and dist >= last - 0.05:
            stalled += 1
            if stalled >= 3: calibrate(dirs); stalled = 0
        else: stalled = 0
        last = dist
        # Near the target move MANHATTAN-style - the larger error axis first, then the other.
        # A diagonal push into a doorway from 1 m off-axis pressed the player into the jamb
        # and the collision slide never resolved it (run 30); a wall wants to be approached
        # square-on and a door along its axis.
        if dist < 1.8 and abs(abs(dx) - abs(dz)) > 0.08:
            ax, az = (dx, 0.0) if abs(dx) > abs(dz) else (0.0, dz)
            best = max(dirs, key=lambda k2: (dirs[k2][0] * ax + dirs[k2][1] * az))
        else:
            best = max(dirs, key=lambda k2: (dirs[k2][0] * dx + dirs[k2][1] * dz) / dist)
        trace.append((i, round(p[0], 2), round(p[1], 2), best))
        key(list(best) if len(best) == 2 else best, min(0.22, max(0.05, dist * 0.05)))
    rec("precise_unreached", {"to": [tx, tz], "trace": trace[-12:]})
    return False

def thread_door(dirs, door_x, z_outside, z_inside, tries=4):
    """Line up on the door column outside, then walk straight through; on a miss, nudge sideways.
    Returns True once the player is past z_inside (either direction)."""
    going_in = z_inside > z_outside
    for attempt in range(tries):
        offset = [0.0, 0.5, -0.5, 1.0][attempt]
        precise_to(dirs, door_x + offset, z_outside, tol=0.35)
        p = player_pos()
        if p is None: continue
        for _ in range(16):
            p0 = player_pos()
            precise_to(dirs, door_x + offset, z_inside, tol=0.6, max_iter=6)
            p1 = player_pos()
            if p1 and abs(p1[0] - (door_x + offset)) < 1.3 and ((going_in and p1[1] >= z_inside - 0.6) or (not going_in and p1[1] <= z_inside + 0.6)):
                rec("door_threaded", {"door_x": door_x + offset, "attempt": attempt, "pos": [round(p1[0], 1), round(p1[1], 1)]})
                return True
            if p0 and p1 and abs(p1[1] - p0[1]) < 0.15: break   # blocked: try another offset
    rec("door_failed", {"door_x": door_x, "pos": player_pos()})
    return False

def unpause():
    """Injected Escape does NOT close the shipped pause menu (finding); click Resume (540,300 200x48)."""
    if screen().get("screen") == "paused":
        safe("POST", "/api/ui/click", {"x": 640, "y": 324}); time.sleep(0.6)

def talk(choice_keys, settle=0.7, closing_enters=1, linear_hops=0):
    """Drive one conversation the way the DialogueSystem state machine wants it:
    E starts it (node text TYPES out); Enter skips the typewriter (harmless in ChoiceSelection);
    a digit picks a choice; the final node needs Enter (skip) + Enter (end). Linear nodes
    (nextNodeId) need one extra Enter pair each. Never Escape (pause menu)."""
    unpause()
    key("E", 0.1); time.sleep(settle)
    for c in choice_keys:
        key("Enter", 0.1); time.sleep(0.4)      # skip typewriter -> ChoiceSelection
        key(c, 0.1); time.sleep(settle)         # pick
    for _ in range(linear_hops):
        key("Enter", 0.1); time.sleep(0.4); key("Enter", 0.1); time.sleep(settle)
    for _ in range(max(1, closing_enters)):
        key("Enter", 0.1); time.sleep(0.4)      # skip typewriter
        key("Enter", 0.1); time.sleep(0.5)      # end (no-op if already ended)


def dlg(): return safe("POST", "/api/rpg/dialogue_state", {})

def talk_by_text(*wanted, settle=0.6, max_steps=12):
    """Drive a conversation by CHOICE TEXT (dialogue_state endpoint, G-47) instead of blind
    indices: E starts it; at each node skip the typewriter (Enter), then pick the first visible
    choice whose text contains the next wanted fragment, or advance/end when there is none.
    Records what it actually saw so a wrong branch is evidence, not a mystery."""
    unpause()
    key("E", 0.1); time.sleep(settle)
    seen = []; queue = list(wanted)
    for _ in range(max_steps):
        d = dlg()
        if not d.get("active"): break
        st = d.get("state")
        if st == "typing":
            key("Enter", 0.1); time.sleep(0.35); continue
        if st == "choice_selection":
            choices = d.get("choices", [])
            seen.append({"node": d.get("node"), "choices": [c["text"][:40] for c in choices]})
            pick = None
            if queue:
                for c in choices:
                    if queue[0].lower() in c["text"].lower(): pick = c["index"]; break
                if pick is None: seen.append({"MISSING": queue[0]}); pick = len(choices) - 1  # last = usually "leave"
                else: queue.pop(0)
            else:
                pick = len(choices) - 1
            key(str(pick + 1), 0.1); time.sleep(settle); continue
        if st == "waiting_for_input":
            key("Enter", 0.1); time.sleep(0.5); continue
        break
    return seen

# ── combat (played through the same API a human's clicks drive) ───────────────────────
def fight(tag, enemy_ids, max_rounds=40):
    log = []
    t_end = time.time() + 600
    rounds_seen = 0
    while time.time() < t_end:
        st = combat("state")
        if not st.get("in_combat"):
            break
        rounds_seen = max(rounds_seen, st.get("round", 0))
        if rounds_seen > max_rounds: log.append("max_rounds"); break
        if st.get("current_entity") != "player":
            time.sleep(0.5); continue
        # our turn: nearest living enemy
        ents = entities(); pp = ents.get("player", {}).get("position", {})
        alive = []
        for eid in enemy_ids:
            e = ents.get(eid)
            if not e: continue
            h = hp(eid)
            hv = h.get("health", h) if isinstance(h, dict) else h
            if isinstance(hv, (int, float)) and hv <= 0: continue
            q = e["position"]
            alive.append((math.hypot(q["x"] - pp.get("x", 0), q["z"] - pp.get("z", 0)), eid, q))
        if not alive:
            log.append("no_alive_enemies_seen"); combat("end_turn"); time.sleep(0.5); continue
        alive.sort(); dist, tid, q = alive[0]
        # a cleric heals when hurt: cure_wounds on self below 45% (the spellbar path a human would click)
        ph = hp("player"); phv = ph.get("health") if isinstance(ph, dict) else ph
        # Run 38: the player died to the wolves (game_over screen, health 0) and the
        # harness kept walking a corpse for 400 s. A dead player ends the fight AND the run.
        if (isinstance(ph, dict) and ph.get("alive") is False) or screen().get("screen") == "game_over":
            rec("game_over", {"tag": tag, "hp": phv, "screen": screen().get("screen")})
            raise RuntimeError("player died in " + tag)
        if isinstance(phv, (int, float)) and phv < 45:
            h = combat("player_cast", {"spell_id": "cure_wounds", "target_id": "player"})
            log.append({"round": st.get("round"), "how": "cure_wounds_self", "ok": h.get("ok"), "blocked": h.get("blocked"), "hp": phv})
            if h.get("ok"):
                time.sleep(1.8)
                before = combat("state").get("current_entity"); key("Space", 0.1); time.sleep(0.6)
                if combat("state").get("in_combat") and combat("state").get("current_entity") == before == "player":
                    combat("end_turn"); time.sleep(0.8)
                continue
        a = combat("player_attack", {"target_id": tid})
        how = "melee"
        if not a.get("ok"):
            # out of reach: a cleric has a ranged cantrip (60 ft) — cast it like the spellbar would
            a = combat("player_cast", {"spell_id": "sacred_flame", "target_id": tid}); how = "sacred_flame"
            if not a.get("ok"):
                combat("player_move", {"x": q["x"], "y": q["y"], "z": q["z"]}); time.sleep(2.5)
                a = combat("player_attack", {"target_id": tid}); how = "move+melee"
        log.append({"round": st.get("round"), "target": tid, "dist": round(dist, 1), "how": how, "ok": a.get("ok"), "blocked": a.get("blocked")})
        time.sleep(1.8)
        # End Turn through the KEYBOARD binding (Space, G-02) - fall back to the API if the
        # turn did not advance (records whether the binding works).
        before = combat("state").get("current_entity")
        key("Space", 0.1); time.sleep(0.6)
        after = combat("state").get("current_entity")
        if combat("state").get("in_combat") and after == before == "player":
            log.append("space_end_turn_failed"); combat("end_turn"); time.sleep(0.8)
    fin = combat("state")
    # The loop leaves on "combat over" - which is also what the player's death produces
    # (run 39: 16 rounds, health 0, game_over, and the in-loop check never ran).
    ph_end = hp("player")
    if (isinstance(ph_end, dict) and ph_end.get("alive") is False) or screen().get("screen") == "game_over":
        rec("game_over", {"tag": tag, "rounds": rounds_seen, "hp": ph_end.get("health") if isinstance(ph_end, dict) else ph_end})
        raise RuntimeError("player died in " + tag)
    rec(tag, {"resolved": not fin.get("in_combat"), "rounds": rounds_seen, "turns": log[-12:],
              "enemy_hp": {e: hp(e) for e in enemy_ids}, "player_hp": hp("player")})
    return not fin.get("in_combat")

# ═══ RUN A ═══════════════════════════════════════════════════════════════════════════
proc, fh = launch("runA")
try:
    rec("boot", screen())
    api("POST", "/api/ui/click", {"x": 640, "y": 374}); time.sleep(4)   # New Game (button 540,350 200x48)
    for _ in range(30):
        if screen().get("screen") == "playing": break
        time.sleep(1)
    time.sleep(5)  # first-load settle (interact dead-zone after a scene's first load)
    rec("in_town", {"screen": screen(), "pos": player_pos(), "npcs": sorted(k for k in entities() if k.startswith("npc_"))})
    world_health("in_town")
    dirs = {"W": (0.0, -1.0), "D": (1.0, 0.0), "S": (0.0, 1.0), "A": (-1.0, 0.0)}
    calibrate(dirs); rec("dirs", {k: [round(v, 2) for v in d] for k, d in dirs.items()})

    # 1. Reeve Aldric on the street in front of the hall (-12, 13): "Tell me about the children" (1) -> "I'll find them" (1)
    r = steer_to(dirs, -12, 14.5, tol=1.6); seen = talk_by_text("Tell me about the children", "I'll find them")
    rec("reeve_accept", {"steer": r, "seen": seen})
    # [Persuasion DC 12] release Wren — visible now as choice 1 (children hidden after accept); a real d20
    seen = talk_by_text("Release the poacher")
    rec("reeve_persuade_wren", {"seen": seen, "persuaded": fired("wren_persuaded"), "refused": fired("wren_refused")})

    # 2. Maera at the shrine south of the hall (-14, -11): choice 1 (barrow)
    r = steer_to(dirs, -14, -9.5, tol=1.6); seen = talk_by_text("What do you know of the barrow")
    rec("maera", {"steer": r, "seen": seen})

    # 3. Bram at the stair rail (-26,-3) inside the Raven's Rest. Regenerated town (2026-09-09):
    #    the street door is on the NORTH wall at (-25,10); interiors are pathable now (NavGraph micro mode).
    # Approach from the STREET (z=13.5), not the fence line: the cleared approach band is only the two
    # cells outside the door, and loose steering at z=12 slid the player west along the strip between
    # fence and wall (runs 27/28). The reveal is centred on x=-24.5 (jambs at -25.0 and -24.1).
    # The NavGraph route goes THROUGH the door (run 29: the threading check said no, the next
    # steer_to walked in and arrived at the stair rail) - no door threading needed any more.
    r1 = steer_to(dirs, -24.5, 13.5, tol=0.6)
    r2 = steer_to(dirs, -24.5, 8.5, tol=0.6)           # into the storeroom through the north door
    r3 = steer_to(dirs, -25.5, -2.5, tol=0.8)          # down the taproom to the stair rail
    seen = talk_by_text("children are missing")
    rec("bram_recruit", {"steer": [r1, r2, r3], "party": safe("GET", "/api/rpg/party"), "pos": player_pos()})

    # 4. Cellar hatch: inside the tavern (HATCH_X, HATCH_Z) - see the constants at the top
    r = steer_to(dirs, HATCH_X, HATCH_Z, tol=0.5, stop=lambda: screen().get("scene_id") == "cellar")
    # inc7: the hatch is a visible trapdoor and interact-gated - stand on it and press E.
    framed_shot("hatch_screenshot", (-24.5, 19.5, 7.5), (-26.5, 17.6, 9.5))
    if screen().get("scene_id") != "cellar":
        key("E", 0.1); time.sleep(1.5)
        rec("hatch_interact", {"scene": screen().get("scene_id"), "pos": player_pos()})
    for _ in range(8):
        if screen().get("scene_id") == "cellar": r = "stopped"; break
        time.sleep(0.5)
    time.sleep(4); rec("cellar", {"steer": r, "scene": screen(), "pos": player_pos()})
    # rats: region z 16..20 from spawn (10,26)
    r = steer_to(dirs, 12, 18.5, tol=1.0, stop=lambda: combat("state").get("in_combat"))
    time.sleep(1.5); st = combat("state"); rec("rats_encounter", {"steer": r, "in_combat": st.get("in_combat"), "order": st.get("turn_order")})
    fight("rats_fight", ["npc_Rat", "npc_Rat2", "npc_Rat3"])
    rec("rats_cleared_var", {"rats_dead_trigger": fired("rats_dead"), "sheet": safe("GET", "/api/rpg/sheet")})
    # back up through the exit region (8..12, 26..27)
    r = steer_to(dirs, 10, 26.6, tol=0.5, stop=lambda: screen().get("scene_id") == "town")
    time.sleep(4); rec("back_in_town", {"steer": r, "scene": screen(), "pos": player_pos(), "player_state_pos": safe("POST", "/api/rpg/get_player_state", {}).get("position")})
    world_health("back_in_town")
    if screen().get("scene_id") == "cellar":   # fell back through the trapdoor? climb out again
        steer_to(dirs, 10, 26.6, tol=0.5, stop=lambda: screen().get("scene_id") == "town"); time.sleep(4)
        rec("back_in_town_retry", {"scene": screen(), "pos": player_pos()})

    # 5. Hobb at the bar's customer end (-24,-2): choice 1 "Your cellar is clear"
    if screen().get("scene_id") == "town" and (player_pos() or [0, 0])[1] > 9:   # re-entered outside? walk back in
        steer_to(dirs, -24.5, 13.5, tol=0.6); steer_to(dirs, -24.5, 8.5, tol=0.6)
    r = steer_to(dirs, -24.5, -0.5, tol=0.6); seen = talk_by_text("Your cellar is clear")
    if any("MISSING" in x for x in seen):   # the nearest NPC was Bram (he follows us) - step closer to Hobb and retry
        precise_to(dirs, -24.5, -1.2, tol=0.3); time.sleep(1.5); seen = seen + talk_by_text("Your cellar is clear")
    rec("hobb_paid", {"steer": r, "seen": seen, "inventory": safe("GET", "/api/rpg/inventory")})

    # 6. East road to Hollin Farm: out the tavern's street door, then along the street to x=60
    steer_to(dirs, -24.5, 8.5, tol=0.6)
    steer_to(dirs, -24.5, 13.5, tol=0.6)   # back out through the north door to the street
    steer_to(dirs, -18, 15, tol=2.0); steer_to(dirs, 20, 15, tol=2.0)
    # inc7: the road exit has a waystone at its verge - stand 5 m short of it and take a picture.
    steer_to(dirs, 55, 15, tol=1.0); framed_shot("waystone_screenshot", (58.0, 20.0, 11.0), (63.5, 17.5, 15.5))
    r = steer_to(dirs, 60, 15, tol=1.5, max_iter=120, stop=lambda: screen().get("scene_id") == "farm")
    time.sleep(4); rec("farm", {"steer": r, "scene": screen(), "pos": player_pos()})

    # 7. Dunstan (16,16): choice 1 (wolves). Wolves west copse region (22..30, 4..14)
    r = steer_to(dirs, 14.5, 16, tol=1.6); seen = talk_by_text("deal with the wolves"); rec("dunstan", {"steer": r, "seen": seen})
    r = steer_to(dirs, 26, 9, tol=1.0, stop=lambda: combat("state").get("in_combat"))
    time.sleep(1.5); rec("wolves_encounter", {"steer": r, "state": combat("state")})
    fight("wolves_fight", ["npc_Wolf", "npc_Wolf2", "npc_Wolf3", "npc_AlphaWolf"])
    r = steer_to(dirs, 16, 17.5, tol=1.6); seen = talk_by_text("wolves are dead"); rec("dunstan_paid", {"steer": r, "seen": seen, "inv": safe("GET", "/api/rpg/inventory")})
    # tracks at the north fence (12..20, 26..29)
    r = steer_to(dirs, 16, 27.5, tol=0.6, stop=lambda: fired("tracks"))
    if not fired("tracks"): r = steer_to(dirs, 15, 28.2, tol=0.5, stop=lambda: fired("tracks"))
    rec("tracks", {"steer": r, "fired": fired("tracks")})
    # Oswin at the mill (8, 26): after tracks, his join choice needs barrow_open (Reeve) - so only 'explain' now
    r = steer_to(dirs, 9.5, 26, tol=1.6); seen = talk_by_text("What grimoire"); rec("oswin_first", {"steer": r, "seen": seen})
    # back to town to report tracks: west exit (1..3, 12..20)
    r = steer_to(dirs, 2, 16, tol=1.0, stop=lambda: screen().get("scene_id") == "town")
    time.sleep(4); rec("town_again", {"steer": r, "scene": screen(), "pos": player_pos()})

    # 8. Reeve: "I found tracks" (visible: [tracks, bye] -> 1) ; Wren freed -> recruit (visible: [offer?, freed, bye])
    r = steer_to(dirs, -12, 14.5, tol=1.6, max_iter=140); seen = talk_by_text("found tracks"); rec("reeve_tracks", {"steer": r, "seen": seen, "wren_unlock": fired("wren_unlock")})
    r = steer_to(dirs, -5, 13, tol=1.6); seen = talk_by_text("You're free"); rec("wren_recruit", {"steer": r, "seen": seen, "party": safe("GET", "/api/rpg/party")})

    # 9. Farm again -> Oswin joins -> barrow
    r = steer_to(dirs, 60, 15, tol=1.5, max_iter=140, stop=lambda: screen().get("scene_id") == "farm")
    time.sleep(4); rec("farm2", {"steer": r, "scene": screen()})
    r = steer_to(dirs, 9.5, 26, tol=1.6); seen = talk_by_text("Come and fetch it yourself"); rec("oswin_join", {"steer": r, "seen": seen, "party": safe("GET", "/api/rpg/party")})
    r = steer_to(dirs, 16, 30.5, tol=1.0, stop=lambda: screen().get("scene_id") == "barrow")
    time.sleep(4); rec("barrow", {"steer": r, "scene": screen(), "pos": player_pos(), "enter": fired("enter_barrow")})

    # 10. antechamber fight (z 34..38), grimoire (22..27, 39..43), crypt fight (z 18..24), boss, relic
    r = steer_to(dirs, 16, 36, tol=1.0, stop=lambda: combat("state").get("in_combat"))
    time.sleep(1.5); rec("antechamber", {"steer": r, "state": combat("state")})
    fight("antechamber_fight", ["npc_Skeleton", "npc_Skeleton2", "npc_Cultist"])
    r = steer_to(dirs, 24, 41, tol=1.2); rec("grimoire", {"steer": r, "fired": fired("grimoire")})
    r = steer_to(dirs, 16, 21, tol=1.0, stop=lambda: combat("state").get("in_combat"))
    time.sleep(1.5); rec("crypt", {"steer": r, "state": combat("state")})
    fight("crypt_fight", ["npc_HollowPriest", "npc_Cultist2", "npc_Skeleton3"])
    rec("priest", {"priest_slain": fired("priest_slain"), "sheet": safe("GET", "/api/rpg/sheet")})
    r = steer_to(dirs, 16, 12.0, tol=0.8, stop=lambda: fired("take_relic")); rec("relic", {"steer": r, "fired": fired("take_relic"), "inv": safe("GET", "/api/rpg/inventory")})

    # 11. Home: barrow exit (14..18, 46..47) -> farm -> west exit -> town -> Reeve (relic, choice 1 -> "Take it" 1)
    r = steer_to(dirs, 16, 46.5, tol=1.0, max_iter=140, stop=lambda: screen().get("scene_id") == "farm")
    time.sleep(4); rec("farm3", {"steer": r, "scene": screen()})
    r = steer_to(dirs, 2, 16, tol=1.0, max_iter=140, stop=lambda: screen().get("scene_id") == "town")
    time.sleep(4); rec("town_final", {"steer": r, "scene": screen()})
    r = steer_to(dirs, -12, 14.5, tol=1.6, max_iter=160); seen = talk_by_text("I have his relic", "Take it")
    time.sleep(2)
    rec("finale", {"steer": r, "seen": seen, "screen": screen(), "win": fired("win"), "sheet": safe("GET", "/api/rpg/sheet")})
except Exception as e:
    rec("RUN_A_ERROR", {"error": repr(e)})
finally:
    try: rec("final_screenshot", safe("GET", "/api/screenshot"))
    except Exception: pass
    proc.kill(); fh.close(); time.sleep(1)

json.dump(ev, open(OUT / "rv_playthrough_evidence.json", "w"), indent=1, default=str)
print("evidence written")
