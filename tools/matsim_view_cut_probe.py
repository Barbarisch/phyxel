"""6i view check: the lab view's faces drawn / cut 0.5 s after the dam break (hand-placed MatSimLab rig, port 8120).
    python tools/matsim_view_cut_probe.py 9    # cells_per_m 3 or 9; prints view_faces and view_faces_cut
"""
import sys
sys.path.insert(0, r"G:\Github\phyxel-matsimb-lab\tools")
import matsim_lab as L
from water_bench import Api, world_job
cpm = int(sys.argv[1]) if len(sys.argv) > 1 else 9
api = Api("http://127.0.0.1:8120"); api.wait_status(60)
L.fill(api, L.DAM); L.wait_occupancy()
L.must(L.ms(api, "create", {"min": L.BOX_MIN, "size": L.BOX_SIZE, "cells_per_m": cpm, "particles": "hybrid"}), "create")
L.must(L.ms(api, "fill_box", {"material": "Water", "min": {"x": 8, "y": 16, "z": 8}, "size": {"x": 3, "y": 3, "z": 8}, "fraction": 1.0}), "fill")
L.must(L.ms(api, "fill_box", {"material": "Water", "min": {"x": 8, "y": 16, "z": 17}, "size": {"x": 8, "y": 5, "z": 7}, "fraction": 1.0}), "fill")
L.must(L.ms(api, "view", {"on": True}), "view")
world_job(api, "/api/world/clear", L.DAM); L.wait_occupancy()
L.must(L.ms(api, "resolidify"), "resolidify")
for k in range(5):
    L.must(L.ms(api, "step", {"n": 6}), "step")
import time; time.sleep(0.3)
st = L.ms(api, "stats")
print({"cpm": cpm, "tick": st["tick"], "particles": st.get("particles"), "view_faces": st["view_faces"], "view_faces_cut": st["view_faces_cut"], "ledger_exact": st["ledger"]["exact"]})
L.ms(api, "view", {"on": False}); L.fill(api, L.DAM)
