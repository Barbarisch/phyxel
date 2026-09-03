"""Hearthvale playthrough part 1: menu scene -> Begin -> town, quest accept via dialogue."""
import json, subprocess, time, urllib.request
from pathlib import Path

PORT = 8101
BASE = f"http://127.0.0.1:{PORT}"
RELDIR = Path.home() / "Documents/PhyxelProjects/Hearthvale/build/Release"
SCRATCH = Path(__file__).parent
ev = {"steps": []}
T0 = time.time()

def rec(s, d):
    ev["steps"].append({"step": s, "t": round(time.time()-T0,1), "data": d})
    print(f"[{s}] {json.dumps(d, default=str)[:260]}")

def api(m, p, b=None, t=10):
    d = json.dumps(b).encode() if b is not None else None
    r = urllib.request.Request(BASE+p, data=d, method=m, headers={"Content-Type":"application/json"})
    with urllib.request.urlopen(r, timeout=t) as x: return json.loads(x.read().decode())

log = open(SCRATCH/"hv_game.log", "w", encoding="utf-8", errors="replace")
proc = subprocess.Popen([str(RELDIR/"Hearthvale.exe"), "--test", str(PORT)],
                        cwd=str(RELDIR), stdout=log, stderr=subprocess.STDOUT)
try:
    dl = time.time()+90
    while time.time() < dl:
        try: api("GET","/api/state",t=3); break
        except Exception: time.sleep(1)
    rec("screen_initial", api("GET","/api/screen/state"))
    # Menu scene: click Begin (button pos [540,360] size [200,48] -> center 640,384)
    rec("click_begin", api("POST","/api/ui/click",{"x":640,"y":384}))
    time.sleep(3)
    scr = api("GET","/api/screen/state"); rec("after_begin", scr)
    st = api("GET","/api/state"); rec("town_state", {"entities":[e.get("id") for e in st.get("entities",[])],"camera":st.get("camera")})
    rec("triggers", {"ids":[t0.get("id") for t0 in api("GET","/api/triggers").get("triggers",[])]})
finally:
    proc.kill(); log.close(); time.sleep(0.5)
    txt = (SCRATCH/"hv_game.log").read_text(encoding="utf-8", errors="replace")
    ev["log"] = [l for l in txt.splitlines() if any(k in l for k in
        ("objective","Objective","Loading screen","menu","Menu","scene","Scene","ERROR","WARN","Unhandled"))][-60:]
(SCRATCH/"hv_menu_evidence.json").write_text(json.dumps(ev, indent=2))
print("done")
