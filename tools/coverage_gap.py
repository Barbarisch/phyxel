"""coverage_gap.py — how big is the step a surface crosses when it leaves probe coverage?

WHY THIS IS THE RIGHT INSTRUMENT. Ambient comes from a probe grid covering +-48 u in x/z and
+-24 u in y around the viewer. gi_field.glsl takes exactly ONE fallback path when it has no answer
for a point, and that same path is taken in both of these cases:
    the point lies outside the grid            (phxGiIrradiance returns false, inGrid false)
    the field is switched off  (/api/debug/gi)  (bit 3 of occupancyBox.w clear)
Both end at `return open;` — phxAmbientAtmos(N, 1.0, sky), i.e. "assume this point sees the whole
sky". So toggling the field measures the SIZE of the step a surface takes when it crosses the
coverage boundary, with the camera standing perfectly still. No small distant target, no projection
maths, no angular-size problem: the two states are captured from an identical pose.

What this does NOT show on its own: that real scenes contain geometry outside the box. That is the
job of the coverage view (debug mode 4) captured separately in the Ravenmere town.

PREDICTION, written before the run: enclosed surfaces jump several-fold brighter with no answer,
and open outdoor surfaces barely move. If everything moves by the same small amount, the fallback
is not the problem I claim it is.

Existing lab rig (tools/ambient_model_check.py), verified from the world before measuring.
NOON, clock paused; tone map OFF; each state settled so the temporal blend has converged.
"""
import json, sys, time, urllib.request
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, 'tools'); import lighting_lab as lab, ambient_model_check as amc

B = 'http://127.0.0.1:8090'
OUT = Path(sys.argv[1])
POSES = ['wall_open', 'wall_street', 'in_control_open', 'in_sealed', 'in_sealed_micro', 'in_door_wall']
WHAT = {'wall_open':      'exterior wall, nothing within 16 u',
        'wall_street':    'exterior wall facing another 13 u away',
        'in_control_open': 'open-roof room, sunlit floor (control: must barely move)',
        'in_sealed':      'sealed room interior',
        'in_sealed_micro': 'room sealed by a ONE-MICRO roof',
        'in_door_wall':   'far wall of a room lit only through a 1-wide door'}


def call(m, p, b=None, t=120):
    r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None,
                               method=m, headers={'Content-Type': 'application/json'})
    return json.load(urllib.request.urlopen(r, timeout=t))


def main():
    lab.require_engine()
    poses = {n: (cam, mode, reg) for n, cam, mode, reg in amc.poses()}
    call('POST', '/api/daynight/set', {'enabled': True, 'paused': True, 'timeOfDay': 12.0})
    call('POST', '/api/debug/tonemap', {'curve': 0, 'exposure': 16.0})
    rows, tiles = [], []
    for name in POSES:
        cam, mode, reg = poses[name]
        lab.set_camera(cam)
        call('POST', '/api/debug/shadow', {'mode': mode})
        vals = {}
        for on in (True, False):
            call('POST', '/api/debug/gi', {'enabled': on})
            time.sleep(7.0)                      # the field blends over refreshes; let it converge
            p = Path(call('GET', '/api/screenshot')['path'])
            vals[on] = (amc.region_lum(p, reg), p)
        lit, p_on = vals[True]; unlit, p_off = vals[False]
        rows.append((name, lit, unlit, unlit / max(lit, 1e-9)))
        print('  %-16s %-46s  covered %.4f   NO ANSWER %.4f   x%.2f'
              % (name, WHAT[name], lit, unlit, unlit / max(lit, 1e-9)), flush=True)
        pair = []
        for lbl, pth in (('probe field answers', p_on), ('no answer: assume open sky', p_off)):
            im = Image.open(pth).convert('RGB'); w, h = im.size
            d = ImageDraw.Draw(im)
            d.rectangle([int(reg[0] * w), int(reg[1] * h), int(reg[2] * w), int(reg[3] * h)],
                        outline=(255, 0, 255), width=3)
            d.text((int(reg[0] * w) + 6, int(reg[1] * h) + 6), '%s\n%s' % (name, lbl), fill=(255, 255, 0))
            pair.append(im.crop((245, 45, 1195, 670)).resize((430, 283)))
        tiles.extend(pair)
    call('POST', '/api/debug/gi', {'enabled': True})
    call('POST', '/api/debug/shadow', {'mode': 0}); call('POST', '/api/debug/tonemap', {'curve': 1, 'exposure': 8.0})

    m = Image.new('RGB', (430 * 2, 283 * len(POSES)))
    [m.paste(t, ((i % 2) * 430, (i // 2) * 283)) for i, t in enumerate(tiles)]
    m.save(OUT / 'coverage_gap.png')
    (OUT / 'coverage_gap.json').write_text(json.dumps(
        [{'pose': r[0], 'what': WHAT[r[0]], 'covered': r[1], 'no_answer': r[2], 'ratio': r[3]} for r in rows],
        indent=2), encoding='utf-8')
    print('\nmontage:', OUT / 'coverage_gap.png')
    return 0


if __name__ == '__main__':
    sys.exit(main())
