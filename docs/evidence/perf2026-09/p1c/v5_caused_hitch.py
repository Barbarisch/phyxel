"""V5 -- a hitch we cause is seen, with its cause, on R-P1 (docs/PerfProgram2026-09.md section 16.4). L4.

Rig R-P1: M4TavernBench (Flat fixed range). Path: inside chunk (0,0,0) at y=21, 2.6 u/s, ~20 s,
stream_follow off. At t = 8 s the harness sends ONE request: fill chunk (0,0,0) three layers deep with
Stone, x 0..31, y 17..19, z 0..31 (3,072 cubes, all below the camera), forcing a full re-mesh.
Prediction (written before running): the fill run has >= 1 HITCH (hitch_analysis.py: a spike > 2x and
>= 4 ms above the same-parity local median, +-0.5 s) within 2.5 s after the fill, whose top phase is the
fill's command (Frame/API Drain) or the re-mesh (drawFrame/Dirty Chunk Flush). Control: the same route
without the fill has NO hitch in that window. (First attempt, 2026-09-25: a GLOBAL-median rule flagged
210 frames of a sustained GPU-bound view change in the control -- the rule, not the tool, was wrong.)
Raw recordings are saved (v5_raw_<arm>.json) so re-analysis never needs a re-run.
Usage: v5_caused_hitch.py out.json"""
import json, sys

from hitch_analysis import hitches
from rig_common import call, route, settle, wait_api

WAYPOINTS = [{'x': 2, 'y': 21, 'z': 4, 'yaw': 0, 'pitch': -30},
             {'x': 30, 'y': 21, 'z': 4, 'yaw': 0, 'pitch': -30},
             {'x': 30, 'y': 21, 'z': 28, 'yaw': 90, 'pitch': -30}]
SPEED = 2.6
FILL_AT = 8.0
WINDOW = (FILL_AT - 0.2, FILL_AT + 2.5)
CAUSES = ('Frame/API Drain', 'drawFrame/Dirty Chunk Flush')


def do_fill():
    call('POST', '/api/world/fill', {'x1': 0, 'y1': 17, 'z1': 0, 'x2': 31, 'y2': 19, 'z2': 31,
                                     'material': 'Stone', 'replace': True})


wait_api()
settle()
out = {'rig': 'R-P1 (M4TavernBench, Flat fixed range)', 'fill_at_s': FILL_AT, 'window_s': WINDOW}
_, ctrl = route(WAYPOINTS, SPEED, stream_follow=False)
json.dump(ctrl, open('v5_raw_control.json', 'w'))
settle()
_, test = route(WAYPOINTS, SPEED, stream_follow=False, mid_action=(FILL_AT, do_fill))
json.dump(test, open('v5_raw_fill.json', 'w'))
out['control_hitches'] = hitches(ctrl, WINDOW)
out['fill_hitches'] = hitches(test, WINDOW)
out['fill_hitches_whole_route'] = len(hitches(test))
out['control_hitches_whole_route'] = len(hitches(ctrl))
caused = [h for h in out['fill_hitches'] if h['top_phases'] and any(h['top_phases'][0][0].endswith(c.split('/')[-1]) for c in CAUSES)]
out['pass'] = bool(caused) and not out['control_hitches']
print(json.dumps({k: v for k, v in out.items() if k != 'rig'}, indent=1)[:2500])
json.dump(out, open(sys.argv[1], 'w'), indent=1)
print('V5', 'PASS' if out['pass'] else 'FAIL')
sys.exit(0 if out['pass'] else 1)
