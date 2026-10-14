"""CityBench device-loss investigation (2026-09-25): poll the GPU scopes every ~2 s from launch until the
device is lost, to tell a LOAD problem (foliage/probe cost climbing toward the ~2 s driver timeout) from
a SUDDEN fault (normal frames, then a hang). Established so far: loss ~30 s after launch at defaults;
none with GI off; none with grass+foliage off; still lost with grass off (foliage on); pre-change probe
shader loses too (not a GI-2 regression).
Usage: device_lost_trend.py <log_baseline_line> out.json"""
import json, subprocess, sys, time, urllib.request

B = 'http://127.0.0.1:8090'
base = int(sys.argv[1])


def get(p, t=5):
    return json.load(urllib.request.urlopen(B + p, timeout=t))


def lost():
    out = subprocess.run(['awk', f'NR>{base}', 'G:/Github/phyxel/phyxel.log'], capture_output=True, text=True).stdout
    return 'VkResult=-4' in out


rows, t0 = [], time.time()
while time.time() - t0 < 150:
    row = {'t': round(time.time() - t0, 1)}
    try:
        g = get('/api/debug/gpu_timing?frames=30')
        sc = {s['key']: (s['median_ms'], s.get('max_ms')) for s in g['scopes']}
        row['gpu_frame'] = (g['gpu_frame_ms'] or {}).get('max_ms')
        for k in ('Scene Pass/Foliage', 'Scene Pass/Grass', 'GI Probes', 'Scene Pass/Static Geometry', 'Shadow Pass'):
            if k in sc:
                row[k] = sc[k]
        row['foliage_n'] = next((s['n'] for s in g['scopes'] if s['key'] == 'Scene Pass/Foliage'), None)
    except Exception as e:
        row['err'] = type(e).__name__
    row['lost'] = lost()
    rows.append(row)
    print(json.dumps(row))
    if row['lost']:
        break
    time.sleep(2)
json.dump(rows, open(sys.argv[2], 'w'), indent=1)
