#!/usr/bin/env python3
"""perf_harness.py - the one measurement harness for the perf program (docs/PerfProgram2026-09.md, I10).

Replaces the one-off rigs (perf_shader_bisect.py, perf_light_marches.py, the M4 ad-hoc sampling).
Every number it writes is taken under the same rules:

  * Release only. /api/status reports build_config; Debug is refused unless --allow-debug.
  * SETTLED only. /api/debug/load_state must report generation_pending == remesh_pending ==
    remesh_idle_pending == 0 AND visibleInstances must hold still for 3 s. Until then it refuses to
    sample and logs how long it waited.
  * POSE-VERIFIED. The camera is set in free mode and read back; a sample is refused if the
    read-back position is off by more than 0.05 u or the angles by more than 0.1 degrees.
  * HISTORY, not single frames. Timing comes from GET /api/debug/gpu_timing over >= --frames GPU
    frames that the engine itself accepted (serial-deduplicated), never from one gpu_scopes read.
  * INTERLEAVED A/B. Configs are visited A B A B ..., never A A A B B B, so drift hits both equally.
  * Every row carries git hash + dirty flag, build config, GPU, present mode, pose, and PROVENANCE
    ("engine-generated: <route>" or "hand-placed rig: <id>"), per the CLAUDE.md provenance rule.

Usage:
  python tools/perf_harness.py sample --poses poses.json --provenance "engine-generated: /api/settlement/build" --out run.jsonl
  python tools/perf_harness.py sample --poses poses.json --ab ab.json --repeats 4 --provenance ... --out run.jsonl
  python tools/perf_harness.py aa --poses poses.json --repeats 6 --provenance ... --out aa.jsonl
  python tools/perf_harness.py compare run.jsonl [--scope "Scene Pass/Static Geometry"]
  python tools/perf_harness.py check settle|gpu_frame|release

poses.json: {"street": [x, y, z, yaw, pitch], ...}
ab.json:    {"A": [[METHOD, PATH, BODY-or-null], ...], "B": [...]}  (requests that put the engine in each config)
"""
import argparse
import json
import random
import statistics
import subprocess
import sys
import time
import urllib.request

BASE = 'http://127.0.0.1:8090'
SETTLE_KEYS = ('generation_pending', 'remesh_pending', 'remesh_idle_pending')


class HarnessError(RuntimeError):
    pass


def call(method, path, body=None, timeout=60):
    req = urllib.request.Request(BASE + path, method=method,
                                 data=json.dumps(body).encode() if body is not None else None,
                                 headers={'Content-Type': 'application/json'})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.load(r)


# ---------------------------------------------------------------------------------------------
# Preconditions
# ---------------------------------------------------------------------------------------------

def require_release(allow_debug):
    cfg = call('GET', '/api/status').get('build_config')
    if cfg is None:
        raise HarnessError('/api/status has no build_config: this engine predates the field; rebuild it')
    if cfg != 'Release' and not allow_debug:
        raise HarnessError(f'engine is a {cfg} build; perf numbers must come from Release (--allow-debug overrides)')
    return cfg


def load_state():
    ls = call('GET', '/api/debug/load_state')
    chunks = ls.get('chunks', {})
    return {k: chunks.get(k) for k in SETTLE_KEYS}


def pending(state):
    return any((v or 0) != 0 for v in state.values())


def wait_settled(timeout_s=600.0, stable_s=3.0, poll_s=0.25, log=print):
    """Blocks until the world is settled. Returns the seconds waited. Raises on timeout."""
    t0 = time.time()
    stable_since = None
    last_vis = None
    refused = 0
    while True:
        if time.time() - t0 > timeout_s:
            raise HarnessError(f'world did not settle within {timeout_s:.0f}s (last: {load_state()})')
        st = load_state()
        vis = call('GET', '/api/debug/engine_timing').get('visibleInstances')
        if pending(st):
            if refused == 0:
                log(f'  refusing to sample: {st}')
            refused += 1
            stable_since, last_vis = None, None
        elif vis != last_vis:
            stable_since, last_vis = time.time(), vis
        elif time.time() - stable_since >= stable_s:
            return round(time.time() - t0, 2)
        time.sleep(poll_s)


def set_pose(pose, tol_pos=0.05, tol_ang=0.1):
    x, y, z, yaw, pitch = pose
    call('POST', '/api/camera', {'mode': 'free', 'position': {'x': x, 'y': y, 'z': z}, 'yaw': yaw, 'pitch': pitch})
    time.sleep(0.5)
    cam = call('GET', '/api/camera')
    p = cam.get('position', {})
    err = max(abs(p.get('x', 1e9) - x), abs(p.get('y', 1e9) - y), abs(p.get('z', 1e9) - z))
    aerr = max(abs(cam.get('yaw', 1e9) - yaw), abs(cam.get('pitch', 1e9) - pitch))
    ok = err <= tol_pos and aerr <= tol_ang
    return ok, {'position': p, 'yaw': cam.get('yaw'), 'pitch': cam.get('pitch'), 'pos_err': err, 'ang_err': aerr}


def meta(provenance):
    def sh(cmd):
        try:
            return subprocess.check_output(cmd, stderr=subprocess.DEVNULL, text=True).strip()
        except Exception:
            return None
    gpu = sh(['nvidia-smi', '--query-gpu=name,driver_version', '--format=csv,noheader'])
    et = call('GET', '/api/debug/engine_timing')
    return {'git': sh(['git', 'rev-parse', '--short', 'HEAD']),
            'dirty': bool(sh(['git', 'status', '--porcelain', '--untracked-files=no'])),
            'build_config': call('GET', '/api/status').get('build_config'),
            'gpu': gpu, 'present_mode': et.get('present_mode'),
            'provenance': provenance, 'started': time.strftime('%Y-%m-%d %H:%M:%S')}


# ---------------------------------------------------------------------------------------------
# Sampling
# ---------------------------------------------------------------------------------------------

def wait_frames(frames, timeout_s=120.0):
    """Waits until the engine has ACCEPTED `frames` new GPU frames into its timing history."""
    start = call('GET', '/api/debug/gpu_timing?frames=1').get('frames_accepted', 0)
    t0 = time.time()
    while True:
        now = call('GET', '/api/debug/gpu_timing?frames=1').get('frames_accepted', 0)
        if now - start >= frames:
            return now - start
        if time.time() - t0 > timeout_s:
            raise HarnessError(f'only {now - start} GPU frames accepted in {timeout_s:.0f}s')
        time.sleep(0.1)


def take_window(frames):
    wait_frames(frames)
    g = call('GET', f'/api/debug/gpu_timing?frames={frames}')
    et = call('GET', '/api/debug/engine_timing')
    try:
        ls = call('GET', '/api/debug/light_stats')
    except Exception:
        ls = None
    return {'gpu_timing': g, 'present_mode': et.get('present_mode'),
            'cpu_frame_ms': et.get('cpuFrameTime'), 'visible_instances': et.get('visibleInstances'),
            'light_stats': ls}


def apply_config(requests):
    for method, path, body in requests:
        call(method, path, body)
    time.sleep(0.5)


def run(args, configs):
    require_release(args.allow_debug)
    poses = json.load(open(args.poses))
    m = meta(args.provenance)
    order = list(configs.keys())
    with open(args.out, 'a') as out:
        out.write(json.dumps({'meta': m, 'mode': args.cmd, 'frames': args.frames, 'repeats': args.repeats,
                              'configs': configs}) + '\n')
        if m['present_mode'] and m['present_mode'].startswith('FIFO'):
            print(f"WARNING: present mode {m['present_mode']} caps the frame rate; FPS is meaningless "
                  f"(GPU scope times remain valid)")
        for pose_name, pose in poses.items():
            for rep in range(args.repeats):
                # COUNTERBALANCED (ABBA): even reps run A,B and odd reps B,A. Plain A,B,A,B put A
                # first in every pair, so any drift within the run (the first A/A run, 2026-09-24,
                # climbed 27.8 -> 33.8 ms with nothing changed) always penalised B, and the A/A
                # "found" a difference that did not exist.
                rep_order = order if rep % 2 == 0 else list(reversed(order))
                for cfg in rep_order:
                    apply_config(configs[cfg])
                    waited = wait_settled(log=lambda s: print(s, flush=True))
                    ok, cam = set_pose(pose)
                    if not ok:
                        out.write(json.dumps({'pose': pose_name, 'config': cfg, 'rep': rep,
                                              'refused': 'pose', 'camera': cam}) + '\n')
                        print(f'  {pose_name} {cfg} rep{rep}: POSE REFUSED {cam}', flush=True)
                        continue
                    time.sleep(args.warmup_s)
                    w = take_window(args.frames)
                    row = {'pose': pose_name, 'config': cfg, 'rep': rep, 'settle_wait_s': waited,
                           'camera': cam, **w, 'provenance': args.provenance}
                    out.write(json.dumps(row) + '\n')
                    out.flush()
                    fr = (w['gpu_timing'].get('gpu_frame_ms') or {}).get('median_ms')
                    print(f'  {pose_name} {cfg} rep{rep}: gpu_frame median {fr} ms '
                          f'({w["gpu_timing"].get("frames_used")} frames)', flush=True)


# ---------------------------------------------------------------------------------------------
# Comparison
# ---------------------------------------------------------------------------------------------

def scope_median(row, scope):
    g = row.get('gpu_timing') or {}
    if scope == 'GPU Frame':
        return (g.get('gpu_frame_ms') or {}).get('median_ms')
    for s in g.get('scopes', []):
        if s.get('key') == scope:
            return s.get('median_ms')
    return None


MIN_PAIRS_FOR_VERDICT = 8


def bootstrap_median_ci(diffs, n=5000, seed=1):
    rng = random.Random(seed)
    meds = sorted(statistics.median([rng.choice(diffs) for _ in diffs]) for _ in range(n))
    return meds[int(0.025 * n)], meds[int(0.975 * n)]


def compare(path, scope):
    """Paired comparison: within each (pose, rep) the two configs ran back to back in counterbalanced
    order, so their difference cancels slow drift. The verdict is a bootstrap CI on the median of
    those paired differences, and it is withheld below MIN_PAIRS_FOR_VERDICT pairs: at n=4 an A/A run
    produced a CI that excluded zero."""
    rows = [json.loads(l) for l in open(path)]
    body = [r for r in rows if 'pose' in r and 'refused' not in r]
    configs = []
    for r in body:
        if r['config'] not in configs:
            configs.append(r['config'])
    print(f'scope: {scope}')
    for pose in dict.fromkeys(r['pose'] for r in body):
        vals = {c: [v for v in (scope_median(r, scope) for r in body if r['pose'] == pose and r['config'] == c)
                    if v is not None] for c in configs}
        line = '  '.join(f'{c}: median {statistics.median(v):.3f} ms (n={len(v)})' for c, v in vals.items() if v)
        print(f'  {pose}: {line}')
        if len(configs) < 2:
            continue
        a_cfg, b_cfg = configs[0], configs[1]
        by_rep = {}
        for r in body:
            if r['pose'] == pose and r['config'] in (a_cfg, b_cfg):
                v = scope_median(r, scope)
                if v is not None:
                    by_rep.setdefault(r['rep'], {})[r['config']] = v
        diffs = [d[b_cfg] - d[a_cfg] for d in by_rep.values() if a_cfg in d and b_cfg in d]
        if not diffs:
            continue
        med = statistics.median(diffs)
        if len(diffs) < MIN_PAIRS_FOR_VERDICT:
            print(f'    {b_cfg} - {a_cfg}: paired median {med:+.3f} ms over {len(diffs)} pairs '
                  f'- NO VERDICT (need >= {MIN_PAIRS_FOR_VERDICT} pairs)')
            continue
        lo, hi = bootstrap_median_ci(diffs)
        print(f'    {b_cfg} - {a_cfg}: paired median {med:+.3f} ms over {len(diffs)} pairs, '
              f'95% CI [{lo:+.3f}, {hi:+.3f}]{"  (excludes 0)" if lo > 0 or hi < 0 else "  (includes 0)"}')


# ---------------------------------------------------------------------------------------------
# Self-checks (the §3.1 reds)
# ---------------------------------------------------------------------------------------------

def check(which):
    if which == 'release':
        cfg = call('GET', '/api/status').get('build_config')
        print(f'build_config = {cfg}')
        return 0 if cfg == 'Release' else 1
    if which == 'settle':
        # Must be run right after a build request, while re-meshing is still pending: the harness
        # has to REFUSE first, then accept once settled.
        st = load_state()
        if not pending(st):
            print(f'NOT A VALID CHECK: nothing pending right now ({st}); trigger a build first')
            return 2
        refused = []
        waited = wait_settled(log=lambda s: refused.append(s))
        print(f'refused while pending ({refused[0] if refused else "-"}), then settled after {waited}s')
        return 0 if refused else 1
    if which == 'gpu_frame':
        # The real GPU frame time must NOT equal the CPU frame time (the old gpuFrameTime did).
        et = call('GET', '/api/debug/engine_timing')
        g, c, fake = et.get('gpu_frame_ms'), et.get('cpuFrameTime'), et.get('gpuFrameTime')
        print(f'gpu_frame_ms={g}  cpuFrameTime={c}  (legacy gpuFrameTime={fake})')
        return 0 if g is not None and abs(g - c) > 1e-6 else 1
    raise HarnessError(f'unknown check {which}')


def main():
    global BASE
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('cmd', choices=['sample', 'aa', 'compare', 'check'])
    ap.add_argument('target', nargs='?', help='compare: jsonl path; check: settle|gpu_frame|release')
    ap.add_argument('--base', default=BASE)
    ap.add_argument('--poses')
    ap.add_argument('--ab', help='json: {"A": [[method, path, body], ...], "B": [...]}')
    ap.add_argument('--frames', type=int, default=240)
    ap.add_argument('--repeats', type=int, default=4)
    ap.add_argument('--warmup-s', dest='warmup_s', type=float, default=1.0)
    ap.add_argument('--provenance')
    ap.add_argument('--out')
    ap.add_argument('--scope', default='GPU Frame')
    ap.add_argument('--allow-debug', action='store_true')
    args = ap.parse_args()
    BASE = args.base.rstrip('/')

    try:
        if args.cmd == 'compare':
            compare(args.target, args.scope)
            return 0
        if args.cmd == 'check':
            return check(args.target)
        if not (args.poses and args.out and args.provenance):
            raise HarnessError('sample/aa need --poses, --out and --provenance')
        if args.cmd == 'aa':
            # A/A noise floor: two identical configs, interleaved. The CI of their difference is the
            # smallest effect this setup can resolve.
            configs = {'A1': [], 'A2': []}
        else:
            configs = json.load(open(args.ab)) if args.ab else {'base': []}
        run(args, configs)
        return 0
    except HarnessError as e:
        print(f'REFUSED: {e}', file=sys.stderr)
        return 3


if __name__ == '__main__':
    sys.exit(main())
