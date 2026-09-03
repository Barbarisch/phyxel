"""Run The Redoubt on the UNMODIFIED BattleSim.exe and prove it actually fought.

The claim under test is not "a battle happened" — it is "a battle with a
DIFFERENT design, driven by JSON-authored behaviour trees, ran on a binary
that was never rebuilt". So this records:

  * the exe's SHA256 before and after (the no-recompile claim)
  * per-faction alive/HP over time (the asymmetric siege actually resolving)
  * screenshots (the standing rule: if it wasn't seen, it didn't happen)
"""
import hashlib, json, subprocess, sys, time, urllib.request
from pathlib import Path

PORT = 8102
BASE = f"http://127.0.0.1:{PORT}"
RELDIR = Path.home() / "Documents/PhyxelProjects/BattleSim/build/Release"
SCRATCH = Path(__file__).parent
EXE = RELDIR / "BattleSim.exe"

def sha(p):
    h = hashlib.sha256()
    h.update(Path(p).read_bytes())
    return h.hexdigest().upper()

def api(m, p, b=None, t=25):
    d = json.dumps(b).encode() if b is not None else None
    r = urllib.request.Request(BASE + p, data=d, method=m,
                               headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(r, timeout=t) as x:
        return json.loads(x.read().decode())

def shot(tag):
    r = api("POST", "/api/rpg/capture_screenshot", {})
    if not r.get("success"):
        return None
    dst = SCRATCH / f"redoubt_{tag}.png"
    (RELDIR / r["path"]).replace(dst)
    return dst

def set_cam(x, y, z, yaw, pitch):
    return api("POST", "/api/rpg/set_camera",
               {"x": x, "y": y, "z": z, "yaw": yaw, "pitch": pitch, "detach": True})

hash_before = sha(EXE)
print(f"exe SHA256 BEFORE : {hash_before}")

for f in (RELDIR / "worlds").glob("*.db*"):
    for _ in range(10):
        try:
            f.unlink(); break
        except PermissionError:
            time.sleep(1)

logpath = SCRATCH / "redoubt.log"
log = open(logpath, "w", encoding="utf-8", errors="replace")
proc = subprocess.Popen([str(EXE), "--test", str(PORT)], cwd=str(RELDIR),
                        stdout=log, stderr=subprocess.STDOUT)
samples = []
try:
    dl = time.time() + 240
    while time.time() < dl:
        try:
            api("GET", "/api/state", t=3); break
        except Exception:
            time.sleep(1.5)
    time.sleep(6)

    # Look down on the fort from the south-east so the gate (south face) and
    # the wall line are both visible.
    set_cam(48.0, 46.0, 86.0, -90, -38)
    time.sleep(1.5)

    t0 = time.time()
    for tag, when in [("t00", 0), ("t01", 12), ("t02", 25), ("t03", 40),
                      ("t04", 58), ("t05", 78)]:
        while time.time() - t0 < when:
            time.sleep(0.5)
        bs = api("POST", "/api/rpg/battle_stats", {})
        p = shot(tag)
        row = {"tag": tag, "t": round(time.time() - t0, 1),
               "alive": bs.get("alive"), "dead": bs.get("dead"),
               "fps": round(bs.get("fps", 0), 1),
               "factions": {f["faction"]: {"alive": f["alive"],
                                           "hp": round(f["hp"], 0)}
                            for f in bs.get("factions", [])},
               "shot": p.name if p else None}
        samples.append(row)
        print(json.dumps(row))
finally:
    proc.kill(); log.close()

hash_after = sha(EXE)
print(f"exe SHA256 AFTER  : {hash_after}")

txt = logpath.read_text(encoding="utf-8", errors="replace")
bt_loaded = txt.count("loaded BT from")
bt_failed = txt.count("Failed to load BT") + txt.count("behaviorTree")
result = {
    "exe_sha256_before": hash_before,
    "exe_sha256_after": hash_after,
    "exe_unchanged": hash_before == hash_after,
    "samples": samples,
}
(SCRATCH / "redoubt_result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")

print("\n=== THE REDOUBT ===")
print("exe unchanged      :", hash_before == hash_after)
first, last = (samples[0], samples[-1]) if samples else ({}, {})
print("first sample       :", json.dumps(first.get("factions")))
print("last  sample       :", json.dumps(last.get("factions")))
