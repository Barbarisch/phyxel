"""Shared helpers for the P1c instrumentation rigs (docs/PerfProgram2026-09.md section 16.4, V3-V5).

route(): start the recorder, start a camera path, WAIT WITHOUT POLLING for its known duration, then read
the recording once. Sending requests during a measured route perturbs it (the main loop drains API
commands every frame), so nothing is sent in between.
analytic_pose(): the reference pose on the constant-speed path (the polyline through the waypoints,
arc-length parametrised; yaw lerped the short way, pitch linear, per segment) -- independent of the
engine's implementation, so V3 compares the engine against geometry, not against itself."""
import json, math, time, urllib.request

B = 'http://127.0.0.1:8090'


def call(m, p, b=None, t=60):
    r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={'Content-Type': 'application/json'})
    return json.load(urllib.request.urlopen(r, timeout=t))


def wait_api():
    for _ in range(120):
        try:
            call('GET', '/api/status', t=3)
            return
        except Exception:
            time.sleep(2)
    raise RuntimeError('engine API not reachable')


def settle(timeout_s=120):
    """Wait until nothing is pending. While the main loop is busy (first streaming load, a build job)
    load_state can time out ("Request timed out waiting for game loop") -- that is 'not settled yet',
    not an error, so keep polling until OUR deadline."""
    t0 = time.time()
    while time.time() - t0 < timeout_s:
        try:
            c = call('GET', '/api/debug/load_state', t=60).get('chunks')
        except Exception:
            c = None
        if c and not (c['generation_pending'] or c['remesh_pending'] or c['remesh_idle_pending']):
            return time.time() - t0
        time.sleep(0.5)
    raise RuntimeError('world did not settle within %d s' % timeout_s)


def dump_recording():
    first = call('GET', '/api/debug/record?from=0&count=2048', t=120)
    rows = list(first['rows'])
    while len(rows) < first['frames']:
        page = call('GET', f'/api/debug/record?from={len(rows)}&count=2048', t=120)
        if not page['rows']:
            break
        rows += page['rows']
    first['rows'] = rows
    return first


def route(waypoints, speed, stream_follow, max_frames=20000, mid_action=None):
    """Runs one recorded route. mid_action=(seconds, fn): fn() is called once at that time -- the ONLY
    request allowed during a route (V5's deliberate hitch). Returns (path_start_response, recording)."""
    r = call('POST', '/api/debug/record', {'start': True, 'max_frames': max_frames})
    assert r['success'], r
    start = call('POST', '/api/camera/path', {'waypoints': waypoints, 'speed_u_per_s': speed,
                                              'loop': False, 'stream_follow': stream_follow})
    assert start['success'], start
    t0 = time.time()
    duration = start['duration_s']
    if mid_action:
        at, fn = mid_action
        time.sleep(max(0.0, at - (time.time() - t0)))
        fn()
    time.sleep(max(0.0, duration + 1.0 - (time.time() - t0)))
    status = call('GET', '/api/camera/path')
    call('POST', '/api/debug/record', {'stop': True})
    rec = dump_recording()
    rec['path_status_after'] = status
    return start, rec


def _lerp_angle(a, b, t):
    d = b - a
    while d > 180: d -= 360
    while d < -180: d += 360
    return a + d * t


def analytic_pose(waypoints, progress):
    pts = [(w['x'], w['y'], w['z']) for w in waypoints]
    seg = [math.dist(pts[i], pts[i + 1]) for i in range(len(pts) - 1)]
    s = max(0.0, min(1.0, progress)) * sum(seg)
    for i, L in enumerate(seg):
        if s <= L or i == len(seg) - 1:
            t = 0.0 if L <= 0 else min(1.0, s / L)
            p = tuple(pts[i][k] + (pts[i + 1][k] - pts[i][k]) * t for k in range(3))
            yaw = _lerp_angle(waypoints[i]['yaw'], waypoints[i + 1]['yaw'], t)
            pitch = waypoints[i]['pitch'] + (waypoints[i + 1]['pitch'] - waypoints[i]['pitch']) * t
            return p, yaw, pitch
        s -= L


def angle_diff(a, b):
    d = (a - b) % 360.0
    return min(d, 360.0 - d)
