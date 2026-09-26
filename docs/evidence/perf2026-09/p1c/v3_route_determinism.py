"""V3 -- route determinism on R-P1 (docs/PerfProgram2026-09.md section 16.4). L4.

Rig R-P1: M4TavernBench fresh process (Flat, fixed from/to range -1..1, no streaming), nothing built;
the path stays inside chunk (0,0,0). stream_follow is OFF (a static world: there is nothing to stream).
Prediction (written before running): in BOTH runs, every recorded frame with 0 <= progress < 1 has its
camera within 0.05 u and 0.1 deg of the analytic pose at that frame's progress. A camera overwritten by
InputManager, drifting, or a recorder attaching the wrong row, fails.
Control: the analytic pose (geometry of the waypoints, rig_common.analytic_pose).
Usage: v3_route_determinism.py out.json"""
import json, math, sys

from rig_common import analytic_pose, angle_diff, call, route, settle, wait_api

WAYPOINTS = [{'x': 4, 'y': 20, 'z': 4, 'yaw': 0, 'pitch': -10},
             {'x': 28, 'y': 20, 'z': 4, 'yaw': 90, 'pitch': -10},
             {'x': 28, 'y': 21, 'z': 28, 'yaw': 180, 'pitch': -20},
             {'x': 4, 'y': 22, 'z': 28, 'yaw': 270, 'pitch': -5}]
SPEED = 4.0          # ~walking speed; 73 u -> ~18 s
POS_TOL, ANG_TOL = 0.05, 0.1

wait_api()
settle()
out = {'rig': 'R-P1 (M4TavernBench, Flat fixed range)', 'waypoints': WAYPOINTS, 'speed': SPEED,
       'pos_tol_u': POS_TOL, 'ang_tol_deg': ANG_TOL, 'runs': []}
ok = True
for run in range(2):
    start, rec = route(WAYPOINTS, SPEED, stream_follow=False)
    worst_p, worst_a, n, bad = 0.0, 0.0, 0, 0
    for row in rec['rows']:
        prog = row['path_progress']
        if prog < 0 or prog >= 1:
            continue
        (ax, ay, az), ayaw, apitch = analytic_pose(WAYPOINTS, prog)
        cx, cy, cz, cyaw, cpitch = row['camera']
        dp = math.dist((cx, cy, cz), (ax, ay, az))
        da = max(angle_diff(cyaw, ayaw), abs(cpitch - apitch))
        worst_p, worst_a = max(worst_p, dp), max(worst_a, da)
        n += 1
        if dp > POS_TOL or da > ANG_TOL:
            bad += 1
    r = {'run': run, 'frames_recorded': rec['frames'], 'frames_on_path': n, 'frames_out_of_tol': bad,
         'worst_pos_err_u': worst_p, 'worst_ang_err_deg': worst_a, 'truncated': rec['truncated'],
         'arc_length_u': start['arc_length_u'], 'duration_s': start['duration_s'],
         'path_finished': rec['path_status_after']['finished']}
    good = n > 100 and bad == 0 and not rec['truncated'] and r['path_finished']
    r['pass'] = good
    ok &= good
    out['runs'].append(r)
    print(json.dumps(r))
out['pass'] = ok
json.dump(out, open(sys.argv[1], 'w'), indent=1)
print('V3', 'PASS' if ok else 'FAIL')
sys.exit(0 if ok else 1)
