"""Two measurements per pose (docs/PerfProgram2026-09.md §12 follow-up, prompted by the Forward+ and
GTA 6 lighting videos):

  M1 OVERDRAW   Static Geometry fragment-shader invocations (pipeline stats, normal shading) divided by
                the pixels static geometry covers in the final image. > 1 means fragments are shaded and
                then hidden (overdraw) or shaded as 2x2 quad helpers of tiny faces (quad overshading).
                Every one of those runs the full light loop, marches included.
  M2 MARCHES    Per covered pixel, how many point/spot lights pass the shader's own gates
                (dist < radius and N.L > 0) and therefore march: debug mode 18 writes count/32 into R.
                If most in-range lights are genuine (not duplicates), per-tile culling (L2) cannot
                remove them; only fewer/cheaper/cached visibility can.

TRANSFER CHECK (the control): with exposure 1 and curve 0 (raw linear), mode 11 paints static
geometry at linear 0.5, which must read ~188 after the sRGB-encoded capture. If it does not, the
mode-18 decode is invalid and the script says so instead of reporting numbers.

Covered pixels: the offscreen scene image is window-sized (PostProcessor is created with the window
extent) and the editor viewport STRETCHES it into its panel (ImGui::Image(tex, contentRegion)), so the
covered FRACTION of the panel equals the covered fraction of the offscreen image. HUD overlays (HP bar,
compass) hide ~1-2% of the panel, a small undercount of covered pixels.
"""
import json, os, statistics, sys, time, urllib.request

import numpy as np
from PIL import Image

B = 'http://127.0.0.1:8090'
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..', '..'))
VIEW = (242, 43, 1197, 672)          # viewport content rect in the window capture (x0, y0, x1, y1)
OFFSCREEN_PX = 1600 * 900            # window-sized scene target


def call(m, p, b=None, t=60):
    r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={'Content-Type': 'application/json'})
    return json.load(urllib.request.urlopen(r, timeout=t))


def capture():
    time.sleep(1.0)
    s = call('GET', '/api/screenshot')
    path = s.get('path') or s.get('file')
    img = np.array(Image.open(os.path.join(REPO, path)).convert('RGB')).astype(np.int32)
    x0, y0, x1, y1 = VIEW
    return img[y0:y1, x0:x1], path


def srgb_to_linear(c):
    c = c / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def static_frag_invocations(n=12):
    call('POST', '/api/debug/pipeline_stats', {'enabled': True})
    time.sleep(1.0)
    seen, vals, px = set(), [], []
    while len(vals) < n:
        time.sleep(0.05)
        s = call('GET', '/api/debug/gpu_scopes')
        st = s.get('static_geometry_pipeline_stats')
        if not st or s.get('serial') in seen:
            continue
        seen.add(s['serial'])
        vals.append(st['frag_invocations'])
        px.append(st['input_primitives'])
    call('POST', '/api/debug/pipeline_stats', {'enabled': False})
    return statistics.median(vals), statistics.median(px)


def measure(pose_name, pose):
    call('POST', '/api/camera', {'mode': 'free', 'position': {'x': pose[0], 'y': pose[1], 'z': pose[2]},
                                 'yaw': pose[3], 'pitch': pose[4]})
    time.sleep(3)
    orig = call('POST', '/api/debug/tonemap', {})
    out = {'pose': pose_name, 'pose_xyzyp': pose, 'tonemap_original': {k: orig.get(k) for k in ('exposure', 'curve')}}
    try:
        call('POST', '/api/debug/tonemap', {'exposure': 1.0, 'curve': 0})
        # --- mode 11: transfer check + covered mask
        call('POST', '/api/debug/shadow', {'mode': 11})
        img11, p11 = capture()
        vals, counts = np.unique(img11.reshape(-1, 3), axis=0, return_counts=True)
        dom = vals[np.argmax(counts)]                    # most common colour = the flat static grey
        grey = int(round(dom.mean()))
        covered = np.all(np.abs(img11 - dom) <= 3, axis=2)
        frac = float(covered.mean())
        transfer_ok = bool(abs(grey - 188) <= 3 and int(max(dom)) - int(min(dom)) <= 2)
        out.update({'mode11_capture': p11, 'static_grey_rgb': dom.tolist(), 'transfer_ok': transfer_ok,
                    'covered_fraction': frac, 'covered_px_est': frac * OFFSCREEN_PX})
        # --- mode 18: marches per covered pixel
        call('POST', '/api/debug/shadow', {'mode': 18})
        img18, p18 = capture()
        marches = np.rint(srgb_to_linear(img18[..., 0]) * 32.0).astype(int)
        m = marches[covered]
        hist = np.bincount(m, minlength=33)[:33]
        out.update({'mode18_capture': p18,
                    'marches_mean_per_covered_px': float(m.mean()) if m.size else None,
                    'marches_median': float(np.median(m)) if m.size else None,
                    'frac_covered_px_with_any_march': float((m > 0).mean()) if m.size else None,
                    'marches_hist_0_32': hist.tolist()})
    finally:
        call('POST', '/api/debug/shadow', {'mode': 0})
        call('POST', '/api/debug/tonemap', out['tonemap_original'])
    # --- normal shading: fragment invocations
    fi, prims = static_frag_invocations()
    out.update({'static_frag_invocations_median': fi, 'static_input_primitives_median': prims,
                'overdraw_ratio': fi / out['covered_px_est'] if out['covered_px_est'] else None})
    ls = call('GET', '/api/debug/light_stats')
    out['lights'] = {k: ls[k] for k in ('registered', 'uploaded_point', 'unique_positions_uploaded', 'duplicate_uploads')}
    return out


if __name__ == '__main__':
    poses = json.load(open(sys.argv[1]))
    res = [measure(k, v) for k, v in poses.items()]
    json.dump(res, open(sys.argv[2], 'w'), indent=1)
    for r in res:
        print(f"{r['pose']:9s} transfer_ok={r['transfer_ok']} grey={r['static_grey_rgb']} "
              f"covered={r['covered_fraction']*100:.1f}% (~{r['covered_px_est']/1e6:.2f} Mpx) "
              f"frag_inv={r['static_frag_invocations_median']/1e6:.2f} M -> overdraw x{r['overdraw_ratio']:.2f} | "
              f"marches/px mean {r['marches_mean_per_covered_px']:.2f} median {r['marches_median']:.0f} "
              f"any {r['frac_covered_px_with_any_march']*100:.0f}% | lights {r['lights']}")
