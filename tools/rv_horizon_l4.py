"""rv_horizon_l4.py - G-166 / streamed-surround L4 on the SHIPPED build: the world continues past the wall.

User: "There is an obvious gap between the fall off of generated terrain and the horizon" and
"some more high res terrain needs to be generated around the current terrain even if the player
cant reach it ... that should also be the norm".

Pose: the player walked to 2 u inside the farm's south edge (z = -32), the camera parked 4 u
behind/above the player looking SOUTH across the edge, so the edge and whatever lies beyond it
are IN FRAME. Captures a tonemapped screenshot + render stats (visible chunks, far tiles) +
play_bounds + a median FPS, and writes rv_horizon_l4_<label>.{png,json}.
  before: 9 resident chunks, 0 far tiles, sky below the horizon past the edge
  after : streamed chunks past the wall, far tiles > 0, bounds still the authored region
Usage: python tools/rv_horizon_l4.py before|after
"""
import json
import statistics
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

REL = Path(r"C:/Users/bpete/Documents/PhyxelProjects/Ravenmere/build_probe/Release")
OUT = Path(r"C:/Users/bpete/Documents/GitHub/phyxel/docs/evidence/ravenmere")
PORT = 8128
B = 'http://127.0.0.1:%d' % PORT
LABEL = sys.argv[1] if len(sys.argv) > 1 else 'run'
CAM_YAW = float(sys.argv[2]) if len(sys.argv) > 2 else -90.0   # free-cam convention: yaw 0 = +X, so -90 looks along -Z


def safe(m, p, b=None, t=60):
    try:
        r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None,
                                   method=m, headers={'Content-Type': 'application/json'})
        return json.load(urllib.request.urlopen(r, timeout=t))
    except Exception as e:
        return {'err': str(e)[:200]}


def scene():
    return safe('GET', '/api/screen/state').get('scene_id')


def player_pos():
    st = safe('GET', '/api/state')
    for e in st.get('entities', []):
        if e.get('id') == 'player':
            p = e.get('position') or {}
            return (float(p.get('x', 0)), float(p.get('y', 0)), float(p.get('z', 0)))
    return None


def go(trigger_id, want_scene):
    safe('POST', '/api/rpg/fire_trigger', {'id': trigger_id})
    for _ in range(60):
        if scene() == want_scene:
            time.sleep(4)
            return True
        time.sleep(1)
    return False


def _uniform(png_bytes):
    """True when the PNG is (nearly) one flat colour - a blank capture, not evidence."""
    try:
        import struct, zlib
        # decode the IHDR + IDAT of an 8-bit RGB(A) PNG well enough to sample pixels
        assert png_bytes[:8] == bytes([137, 80, 78, 71, 13, 10, 26, 10])   # PNG signature
        pos, w, h, ctype, idat = 8, 0, 0, 0, b''
        while pos < len(png_bytes):
            ln, = struct.unpack('>I', png_bytes[pos:pos + 4]); tag = png_bytes[pos + 4:pos + 8]
            data = png_bytes[pos + 8:pos + 8 + ln]; pos += 12 + ln
            if tag == b'IHDR': w, h, _bd, ctype = struct.unpack('>IIBB', data[:10])
            elif tag == b'IDAT': idat += data
            elif tag == b'IEND': break
        raw = zlib.decompress(idat); bpp = 4 if ctype == 6 else 3; stride = w * bpp + 1
        vals = set()
        for y in range(0, h, max(1, h // 24)):
            row = raw[y * stride + 1:(y + 1) * stride]
            for x in range(0, w * bpp, max(bpp, (w * bpp) // 24)):
                vals.add(tuple(v // 8 for v in row[x:x + 3]))
        return len(vals) <= 3
    except Exception:
        return False


def shot(name, tries=4):
    """Capture; a flat (blank) frame is retried - the first pumped 'after' came back pure white."""
    dst = OUT / ('rv_horizon_l4_%s.png' % name)
    for i in range(tries):
        r = safe('GET', '/api/screenshot')
        if r.get('path'):
            b = (REL / r['path']).read_bytes()
            if not _uniform(b):
                dst.write_bytes(b)
                return str(dst)
            print('   capture %d was a flat frame; retrying' % (i + 1), flush=True)
        time.sleep(2.0)
    return None


def fps_median(n=10):
    xs = []
    for _ in range(n):
        r = safe('GET', '/api/rpg/get_gpu_scopes')
        if isinstance(r.get('fps'), (int, float)):
            xs.append(r['fps'])
        time.sleep(0.25)
    return round(statistics.median(xs), 1) if xs else None


log = open(OUT / ('rv_horizon_l4_%s.log' % LABEL), 'w', encoding='utf-8', errors='replace')
proc = subprocess.Popen([str(REL / 'Ravenmere.exe'), '--test', str(PORT)], cwd=str(REL),
                        stdout=log, stderr=subprocess.STDOUT)
res = {'label': LABEL, 'cam_yaw': CAM_YAW}
try:
    for _ in range(180):
        if 'err' not in safe('GET', '/api/state', t=3):
            break
        time.sleep(1)
    reached = False
    for _a in range(6):
        safe('POST', '/api/ui/click', {'x': 640, 'y': 374})
        for _ in range(40):
            sc = safe('GET', '/api/screen/state')
            if sc.get('scene_id') == 'town' and sc.get('screen') == 'playing':
                reached = True
                break
            time.sleep(1)
        if reached:
            break
    assert reached, 'never reached the town'
    time.sleep(3)
    res['town_fps'] = fps_median()
    res['town_stats'] = safe('POST', '/api/rpg/get_render_stats', {})
    assert go('to_farm', 'farm'), 'never reached the farm'
    time.sleep(6)   # let streaming (if any) settle
    safe('POST', '/api/rpg/walk_to', {'x': 6.0, 'y': 18.0, 'z': -30.0})
    for _ in range(90):
        if not safe('POST', '/api/rpg/walk_status', {}).get('active'):
            break
        time.sleep(0.5)
    time.sleep(4)
    p = player_pos()
    res['player'] = p
    res['bounds'] = safe('POST', '/api/rpg/play_bounds', {})
    # camera 4 u behind (north of) and 3 u above the player, looking south across the edge
    safe('POST', '/api/rpg/set_camera', {'x': p[0], 'y': p[1] + 3.0, 'z': p[2] + 4.0,
                                         'yaw': CAM_YAW, 'pitch': -12.0, 'detach': True})
    time.sleep(2.5)
    res['stats'] = safe('POST', '/api/rpg/get_render_stats', {})
    res['fps'] = fps_median()
    res['png'] = shot(LABEL)
    safe('POST', '/api/rpg/set_camera', {'detach': False})
    print('%s: player=%s bounds=%s' % (LABEL, p, res['bounds'].get('source')), flush=True)
    print('   chunks=%s far_tiles=%s faces=%s fps=%s (town fps %s) png=%s' % (
        res['stats'].get('visible_chunk_count'), res['stats'].get('far_tiles_drawn'),
        res['stats'].get('total_visible_faces'), res['fps'], res['town_fps'], res['png']), flush=True)
finally:
    proc.terminate()
    time.sleep(2)
    log.flush()
(OUT / ('rv_horizon_l4_%s.json' % LABEL)).write_text(json.dumps(res, indent=2), encoding='utf-8')
