"""ceiling_sweep.py — does a FIXED surface change brightness purely because the camera moved?

THE EXPERIMENT. A large enclosed ceiling sits at y=50 over clear ground. The camera flies straight
up beneath it, looking at it, so the SAME surface stays huge and centred in frame while its
distance from the camera falls from 30 u to 6 u. Nothing in the world changes. The probe grid
reaches about 22 u above the viewer, so the ceiling starts outside coverage and ends inside it.

At every height, three readings:
  ON    the shipped configuration
  OFF   /api/debug/gi false, which takes the IDENTICAL fallback path in gi_field.glsl as being
        out of the grid, so it is a flat reference line: the value a surface gets with no answer
  cov   debug view 4, the fraction of the measured box the shader says it had an answer for

PREDICTION, written before the run. OFF is flat at every distance, because the fallback cannot
depend on distance. ON equals OFF while the ceiling is beyond ~22 u, then departs from it as
coverage arrives. The departure point coincides with cov rising off zero. If ON tracks OFF the
whole way, or departs somewhere unrelated to cov, my account of the system is wrong.

NOON, clock paused, tone map OFF, each state settled 7 s so the probe field's temporal blend has
converged (this measures the steady value, not the transient the user sees while moving).
"""
import json, sys, time, urllib.request
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, 'tools'); import lighting_lab as lab

B = 'http://127.0.0.1:8090'
OUT = Path(sys.argv[1])
CX, CZ, CY = 60.5, -15.5, 50
HEIGHTS = [20, 24, 26, 28, 30, 32, 36, 40, 44]


def call(m, p, b=None, t=180):
    r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None,
                               method=m, headers={'Content-Type': 'application/json'})
    return json.load(urllib.request.urlopen(r, timeout=t))


def srgb_to_linear(x):
    return np.where(x <= 0.04045, x / 12.92, ((x + 0.055) / 1.055) ** 2.4)


def cam(y, pitch=88.0):
    call('POST', '/api/camera', {'mode': 'free', 'position': {'x': CX, 'y': float(y), 'z': CZ},
                                 'yaw': 0.0, 'pitch': pitch})


def shot(mode, exposure, settle):
    call('POST', '/api/debug/shadow', {'mode': mode})
    call('POST', '/api/debug/tonemap', {'curve': 0, 'exposure': exposure})
    time.sleep(settle)
    return Path(call('GET', '/api/screenshot')['path'])


def viewport_rect():
    """Sky-up vs ground-down: every 3D pixel changes, docked panels do not. The density filter
    drops the one-line status bar, which also changes because it prints the camera position."""
    cam(30, 85.0); a = np.asarray(Image.open(shot(0, 8.0, 3)).convert('RGB'), dtype=np.int16)
    cam(30, -85.0); b = np.asarray(Image.open(shot(0, 8.0, 3)).convert('RGB'), dtype=np.int16)
    m = np.abs(a - b).max(axis=2) > 10
    rows = np.where(m.mean(axis=1) > 0.35)[0]
    cols = np.where(m.mean(axis=0) > 0.35)[0]
    return int(cols.min()), int(cols.max()), int(rows.min()), int(rows.max())


def main():
    lab.require_engine()
    call('POST', '/api/daynight/set', {'enabled': True, 'paused': True, 'timeOfDay': 12.0})
    VX0, VX1, VY0, VY1 = viewport_rect()
    H = VY1 - VY0
    CXp, CYp = (VX0 + VX1) / 2.0, (VY0 + VY1) / 2.0
    hw = int(0.05 * H)
    bx0, bx1, by0, by1 = int(CXp - hw), int(CXp + hw), int(CYp - hw), int(CYp + hw)
    print('viewport x %d..%d y %d..%d; measured box %d..%d, %d..%d'
          % (VX0, VX1, VY0, VY1, bx0, bx1, by0, by1))

    rows, tiles = [], []
    for y in HEIGHTS:
        d = CY - y
        cam(y)
        call('POST', '/api/debug/gi', {'enabled': True})
        p_on = shot(0, 16.0, 7.0)
        p_cov = shot(4, 1.0, 2.5)
        call('POST', '/api/debug/gi', {'enabled': False})
        p_off = shot(0, 16.0, 7.0)

        def lum(p):
            lin = srgb_to_linear(np.asarray(Image.open(p).convert('RGB'), dtype=np.float32) / 255.0)
            L = 0.2126 * lin[..., 0] + 0.7152 * lin[..., 1] + 0.0722 * lin[..., 2]
            return float(L[by0:by1, bx0:bx1].mean())
        on, off = lum(p_on), lum(p_off)
        cov = np.asarray(Image.open(p_cov).convert('RGB'), dtype=np.float32)
        frac = float((cov[by0:by1, bx0:bx1, 0] > 128).mean())
        rows.append((y, d, on, off, frac))
        print('  camera y=%2d  ceiling %2d u above:  field ON %.4f   no answer %.4f   ON/OFF %.2fx   covered %.2f'
              % (y, d, on, off, on / max(off, 1e-9), frac), flush=True)

        im = Image.open(p_on).convert('RGB'); dr = ImageDraw.Draw(im)
        dr.rectangle([bx0, by0, bx1, by1], outline=(255, 0, 255), width=3)
        dr.text((bx1 + 8, by0), '%d u above\nON %.4f\nOFF %.4f\ncovered %.0f%%' % (d, on, off, 100 * frac), fill=(255, 255, 0))
        tiles.append(im.crop((VX0, VY0, VX1, VY1)).resize((420, int(420 * H / (VX1 - VX0)))))

    call('POST', '/api/debug/gi', {'enabled': True})
    call('POST', '/api/debug/shadow', {'mode': 0}); call('POST', '/api/debug/tonemap', {'curve': 1, 'exposure': 8.0})

    offs = [r[3] for r in rows]
    print('\n  no-answer reference across the whole sweep: %.4f .. %.4f (spread %.1f%%) - flat, as it must be'
          % (min(offs), max(offs), 100 * (max(offs) - min(offs)) / max(offs)))
    for y, d, on, off, frac in rows:
        bar = '#' * int(round(40 * on / max(max(r[2] for r in rows), 1e-9)))
        print('  %2d u above  covered %3.0f%%  ON %.4f  %s' % (d, 100 * frac, on, bar))

    tw, th = tiles[0].size
    m = Image.new('RGB', (tw * 3, th * 3))
    [m.paste(t, ((i % 3) * tw, (i // 3) * th)) for i, t in enumerate(tiles)]
    m.save(OUT / 'ceiling_sweep.png')
    (OUT / 'ceiling_sweep.json').write_text(json.dumps(
        [{'camera_y': r[0], 'ceiling_distance': r[1], 'field_on': r[2], 'no_answer': r[3], 'covered': r[4]}
         for r in rows], indent=2), encoding='utf-8')
    print('\nmontage:', OUT / 'ceiling_sweep.png')
    return 0


if __name__ == '__main__':
    sys.exit(main())
