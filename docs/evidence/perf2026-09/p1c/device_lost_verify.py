"""Verify the far-shadow stale-cache fix (2026-09-26): after launching the engine on a settlement world,
watch the log for DURATION seconds and report (a) whether the trigger path fired -- setTreeExclusions
retiring every far tile ("Tree exclusion zones set") -- and (b) whether the device was lost (VkResult=-4).
A run only counts as evidence if the trigger fired. Appends one JSON row per run to OUT.
Usage: device_lost_verify.py <log_baseline_line> <label> <out.jsonl> [duration_s=150]"""
import json, subprocess, sys, time, urllib.request

B = 'http://127.0.0.1:8090'
base, label, out = int(sys.argv[1]), sys.argv[2], sys.argv[3]
duration = float(sys.argv[4]) if len(sys.argv) > 4 else 150.0


def log_since():
    return subprocess.run(['awk', f'NR>{base}', 'G:/Github/phyxel/phyxel.log'],
                          capture_output=True, text=True).stdout


t0, frames, lost_at, trig_at = time.time(), None, None, None
while time.time() - t0 < duration:
    txt = log_since()
    t = round(time.time() - t0, 1)
    if trig_at is None and 'Tree exclusion zones set' in txt:
        trig_at = t
    if 'VkResult=-4' in txt:
        lost_at = t
        break
    try:
        frames = json.load(urllib.request.urlopen(B + '/api/debug/engine_timing', timeout=5)).get('frameCount', frames)
    except Exception:
        pass
    time.sleep(3)
txt = log_since()
row = {'label': label, 'duration_s': duration, 'trigger_fired_s': trig_at,
       'exclusion_events': txt.count('Tree exclusion zones set'),
       'lost': lost_at is not None, 'lost_at_s': lost_at, 'last_frameCount': frames}
print(json.dumps(row))
open(out, 'a').write(json.dumps(row) + '\n')
