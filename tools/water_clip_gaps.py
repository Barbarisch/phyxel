"""WaterCore.md 27.15: find where a pond clip's water look breaks - every frame against a still reference.

A BLOCK (12 x 12 px) is FLAGGED when its average colour (chromaticity: r, g, b over their sum - shading from waves and ripples moves brightness far
more than hue) differs from the reference frame by more than --thr, inside the pond (pixels the reference shows as water:
blue share above --water-b). Water drawn over the grass bed tints it blue; where the water is missing the bed shows bare
(yellow-green), and a wall seen edge-on shows the bed through a vertical face (yellow) - both move the chromaticity a lot.

    python tools/water_clip_gaps.py clip.mp4 [--ref 7-9] [--thr 0.03] [--out dir] [--frames N]
Prints flagged-pixel counts per second (max frame), the worst frames' times and their bounding boxes, writes overlays of the
--frames worst frames (flagged pixels red) and a JSON of the per-frame counts next to the clip (or in --out).
"""
import argparse, json, pathlib, shutil, subprocess
import numpy as np
from PIL import Image


def frames(path, w, h):
    p = subprocess.Popen([shutil.which("ffmpeg"), "-loglevel", "error", "-i", str(path), "-f", "rawvideo", "-pix_fmt", "rgb24", "-"], stdout=subprocess.PIPE)
    n = w * h * 3
    while True:
        b = p.stdout.read(n)
        if len(b) < n: break
        yield np.frombuffer(b, np.uint8).reshape(h, w, 3)


BLOCK = 12   # px: the grass under the water sways in the wind - per pixel its colour flickers; a block's average does not


def chroma(img):
    h, w = img.shape[0] // BLOCK * BLOCK, img.shape[1] // BLOCK * BLOCK
    f = img[:h, :w].astype(np.float32).reshape(h // BLOCK, BLOCK, w // BLOCK, BLOCK, 3).mean(axis=(1, 3)) + 1.0
    return f / f.sum(axis=2, keepdims=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("clip")
    ap.add_argument("--ref", default="7-9", help="seconds a-b of a still pond (median frame is the reference)")
    ap.add_argument("--thr", type=float, default=0.03, help="chromaticity distance that flags a pixel")
    ap.add_argument("--water-b", type=float, default=0.30, help="blue share above which the reference pixel is water")
    ap.add_argument("--ui", default="0,0,170,60", help="x0,y0 of the pond view: pixels left of x0 or above y0 are UI (ignored)")
    ap.add_argument("--min-px", type=int, default=0, help="a frame counts as broken above this many flagged BLOCKS")
    ap.add_argument("--frames", type=int, default=6)
    ap.add_argument("--out", default=None)
    a = ap.parse_args()
    clip = pathlib.Path(a.clip)
    pr = subprocess.run([shutil.which("ffprobe"), "-v", "error", "-show_entries", "stream=width,height,r_frame_rate", "-of", "json", str(clip)], capture_output=True, text=True)
    st = json.loads(pr.stdout)["streams"][0]; w, h = st["width"], st["height"]; num, den = st["r_frame_rate"].split("/"); fps = float(num) / float(den)
    r0, r1 = (float(x) for x in a.ref.split("-"))
    allf = list(frames(clip, w, h))
    ref = np.median(np.stack(allf[int(r0 * fps):int(r1 * fps)]), axis=0).astype(np.uint8)
    rc = chroma(ref)
    _, _, ux, uy = (int(x) for x in a.ui.split(","))
    mask = rc[..., 2] > a.water_b
    mask[:, :(ux + BLOCK - 1) // BLOCK] = False; mask[:(uy + BLOCK - 1) // BLOCK, :] = False
    counts, boxes = [], []
    for img in allf:
        d = np.abs(chroma(img) - rc).sum(axis=2)
        fl = (d > a.thr) & mask
        counts.append(int(fl.sum()))
        ys, xs = np.nonzero(fl)
        boxes.append([int(xs.min()) * BLOCK, int(ys.min()) * BLOCK, int(xs.max() + 1) * BLOCK, int(ys.max() + 1) * BLOCK] if len(xs) else None)
    counts = np.array(counts)
    print(f"{clip.name}: {len(counts)} frames at {fps:g} fps; pond mask {int(mask.sum())} px; reference {a.ref} s; thr {a.thr}")
    print(f"frames over {a.min_px} px: {int((counts > a.min_px).sum())}  (reference window max {int(counts[int(r0 * fps):int(r1 * fps)].max())})")
    for s in range(int(len(counts) / fps) + 1):
        seg = counts[int(s * fps):int((s + 1) * fps)]
        if len(seg): print(f"  {s:3d} s  max {int(seg.max()):6d}  frames over {int((seg > a.min_px).sum()):2d}")
    out = pathlib.Path(a.out) if a.out else clip.parent
    out.mkdir(parents=True, exist_ok=True)
    worst = []
    for i in np.argsort(-counts):
        if counts[i] <= a.min_px or len(worst) >= a.frames: break
        if any(abs(i - j) < fps / 2 for j in worst): continue
        worst.append(int(i))
    for i in worst:
        img = allf[i].copy(); d = np.abs(chroma(img) - rc).sum(axis=2); fl = (d > a.thr) & mask
        ov = img.copy(); big = np.kron(fl, np.ones((BLOCK, BLOCK), bool)); ov[:big.shape[0], :big.shape[1]][big] = (255, 0, 0)
        Image.fromarray(np.hstack([img, ov])).save(out / f"{clip.stem}_gap_{i / fps:06.2f}s.png")
        print(f"  worst t {i / fps:6.2f} s: {counts[i]} px, box {boxes[i]}")
    (out / f"{clip.stem}_gaps.json").write_text(json.dumps({"clip": clip.name, "fps": fps, "ref": a.ref, "thr": a.thr, "mask_px": int(mask.sum()),
                                                           "counts": counts.tolist(), "boxes": boxes, "worst_s": [i / fps for i in worst]}))


if __name__ == "__main__":
    main()
