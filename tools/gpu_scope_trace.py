"""gpu_scope_trace.py - run the editor on a project and record GPU pass timings until the device dies.

Diagnostic for the streamed-surround device loss (2026-09-23): VK_ERROR_DEVICE_LOST ~90 chunks
into streaming, in the shipped exe AND the Debug editor, no validation output. Samples
/api/debug/gpu_scopes + /api/debug/load_state every 2 s into a jsonl, stops on the first
"vkQueueSubmit failed" in the engine log (or after --seconds), kills the editor it started, and
prints the last samples' heaviest scopes.
Usage: python tools/gpu_scope_trace.py <project-dir> [--seconds 300] [--port 8090]
"""
import argparse
import json
import subprocess
import time
import urllib.request
from pathlib import Path

ap = argparse.ArgumentParser()
ap.add_argument('project')
ap.add_argument('--seconds', type=float, default=300.0)
ap.add_argument('--port', type=int, default=8090)
ap.add_argument('--exe', default=r'C:/Users/bpete/Documents/GitHub/phyxel/phyxel.exe')
ap.add_argument('--out', default=r'C:/Users/bpete/Documents/GitHub/phyxel/docs/evidence/ravenmere/gpu_scope_trace')
a = ap.parse_args()
B = 'http://127.0.0.1:%d' % a.port
OUT = Path(a.out)
LOG = OUT.with_suffix('.log')
JSONL = OUT.with_suffix('.jsonl')


def safe(m, p, b=None, t=20):
    try:
        r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None,
                                   method=m, headers={'Content-Type': 'application/json'})
        return json.load(urllib.request.urlopen(r, timeout=t))
    except Exception as e:
        return {'err': str(e)[:120]}


log = open(LOG, 'w', encoding='utf-8', errors='replace')
proc = subprocess.Popen([a.exe, '--project', a.project], cwd=str(Path(a.exe).parent),
                        stdout=log, stderr=subprocess.STDOUT)
rows = []
try:
    for _ in range(240):
        if 'err' not in safe('GET', '/api/state', t=3):
            break
        time.sleep(1)
    t0 = time.monotonic()
    with open(JSONL, 'w', encoding='utf-8') as f:
        while time.monotonic() - t0 < a.seconds:
            row = {'t': round(time.monotonic() - t0, 1),
                   'scopes': safe('GET', '/api/debug/gpu_scopes'),
                   'load': safe('GET', '/api/debug/load_state')}
            rows.append(row)
            f.write(json.dumps(row) + '\n'); f.flush()
            log.flush()
            txt = open(LOG, encoding='utf-8', errors='replace').read()
            if 'vkQueueSubmit failed' in txt:
                row['device_lost'] = True
                print('DEVICE LOST at t=%.1fs' % row['t'], flush=True)
                break
            time.sleep(2.0)
finally:
    proc.terminate()
    time.sleep(2)
    try:
        proc.kill()
    except Exception:
        pass

print('%d samples -> %s' % (len(rows), JSONL))
for row in rows[-6:]:
    sc = row['scopes']
    if 'scopes' not in sc:
        print(' t=%5.1f  %s' % (row['t'], sc.get('err', sc)))
        continue
    top = sorted([s for s in sc['scopes'] if s.get('depth') == 1], key=lambda s: -s.get('ms', 0))[:6]
    print(' t=%5.1f fps=%s  %s  | load=%s' % (
        row['t'], sc.get('fps'), ', '.join('%s %.1fms' % (s['name'], s['ms']) for s in top),
        json.dumps(row['load'])[:120]), flush=True)
