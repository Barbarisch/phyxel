"""WaterCore.md 25.26 M-T1 - the default murkiness, A/B: the same pose captured with `water_render_core {default_clarity}` at
0 (the old derived clear water, ~10.8 m Secchi), 3 m (the proposed default) and 1.5 m; on the creek bench (the creek running)
and the Small bench pond. One contact sheet per bench for the owner to pick.

    python tools/water_murk_compare.py --bench creek|pond
Writes docs/evidence/water_core_h1/murk_<bench>.png (+ the single frames).
"""
import argparse, pathlib, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set
from PIL import Image, ImageDraw

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_h1"
BENCH = {"creek": ("http://127.0.0.1:8114", {"x": 12.0, "y": 18.2, "z": 34.5, "yaw": -12.0, "pitch": -16.0}),
         "pond": ("http://127.0.0.1:8111", {"x": 105.2, "y": 18.6, "z": 13.5, "yaw": 180.0, "pitch": -24.0})}


def shot(api):
    r = api.get("/api/screenshot", timeout=90)
    path = r.get("path") or next((s.get("path") for s in r.get("screenshots", [])), None)
    img = Image.open(path).convert("RGB")
    w, h = img.size
    return img.crop((int(w * 0.15), int(h * 0.05), int(w * 0.75), int(h * 0.75)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bench", choices=list(BENCH), required=True)
    args = ap.parse_args()
    url, pose = BENCH[args.bench]
    api = Api(url); api.wait_status(60)
    if args.bench == "pond":
        from water_splash_demo import build_pond
        build_pond(api); time.sleep(8)
    camera_set(api, pose); time.sleep(2)
    EV.mkdir(parents=True, exist_ok=True)
    tiles = []
    for c, label in ((0.0, "old: clear (~10.8 m)"), (5.0, "5 m"), (3.0, "3 m"), (1.5, "1.5 m")):
        api.debug("water_render_core", {"default_clarity": c}); time.sleep(1.0)
        img = shot(api); img.save(EV / f"murk_{args.bench}_{str(c).replace('.', 'p')}.png")
        d = ImageDraw.Draw(img); d.rectangle((0, 0, 330, 34), fill=(0, 0, 0)); d.text((8, 8), label, fill=(255, 255, 255))
        tiles.append(img)
    api.debug("water_render_core", {"default_clarity": 5.0})
    w, h = tiles[0].size
    sheet = Image.new("RGB", (w * 2, h * 2))
    for i, t in enumerate(tiles): sheet.paste(t, ((i % 2) * w, (i // 2) * h))
    sheet.save(EV / f"murk_{args.bench}.png"); print("wrote", EV / f"murk_{args.bench}.png")


if __name__ == "__main__":
    main()
