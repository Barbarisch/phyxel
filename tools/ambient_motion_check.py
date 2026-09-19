"""Camera-motion flicker probe at a pose where ambient VARIES spatially: the door room's far wall
(dark interior, bright exterior 4 u away). Read at pose A, move the camera 5 u out of the room for
a second (the probe grid re-snaps), return, read at +0.3 / +1 / +3 s. Grid-relative probes: the
wall inherits exterior probes' history and brightens, then fades. World-stable probes: flat."""
import sys, time
from pathlib import Path
sys.path.insert(0, 'tools'); import lighting_lab as lab, ambient_model_check as amc
poses = {n: (cam, mode, reg) for n, cam, mode, reg in amc.poses()}
name = sys.argv[1] if len(sys.argv) > 1 else 'in_door_wall'
camA, mode, reg = poses[name]
lab.post('/api/daynight/set', {'enabled': True, 'paused': True, 'timeOfDay': 12.0})
lab.post('/api/debug/tonemap', {'curve': 0, 'exposure': 16.0}); lab.post('/api/debug/shadow', {'mode': mode})
def read():
    return amc.region_lum(Path(lab.get('/api/screenshot')['path']), reg)
lab.set_camera(camA); time.sleep(8); base = read()
print('%s baseline at A: %.5f' % (name, base), flush=True)
worst = 0.0
for trip in range(3):
    away = dict(camA); away['x'] += 5.0; away['z'] -= 6.0; away['y'] += 3.0
    lab.set_camera(away); time.sleep(1.0)
    lab.set_camera(camA); time.sleep(0.3); r0 = read(); time.sleep(0.7); r1 = read(); time.sleep(2.0); r3 = read()
    worst = max(worst, abs(r0 / base - 1), abs(r1 / base - 1), abs(r3 / base - 1))
    print('trip %d: back at A  +0.3s %.5f (%+.0f%%)  +1s %.5f (%+.0f%%)  +3s %.5f (%+.0f%%)' % (
        trip, r0, 100 * (r0 / base - 1), r1, 100 * (r1 / base - 1), r3, 100 * (r3 / base - 1)), flush=True)
print('worst deviation after a trip: %.0f%%' % (100 * worst))
lab.post('/api/debug/shadow', {'mode': 0}); lab.post('/api/debug/tonemap', {'curve': 1, 'exposure': 8.0})
