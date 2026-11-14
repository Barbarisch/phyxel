"""Side-by-side viewport crops: level 0 everywhere | P=4 | P=8 (same pose, wind 0, GI off, world hidden)."""
import os, sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
BOX = (242, 45, 1196, 670)   # x0, y0, x1, y1
pose = sys.argv[1]
names = [("L0", "level 0 everywhere"), ("P4", "size on screen, P=4"), ("P8", "size on screen, P=8")]
ims = [Image.open(os.path.join(HERE, f"f214_{pose}_{n}.png")).convert("RGB").crop(BOX) for n, _ in names]
w, h = ims[0].size
sheet = Image.new("RGB", (w * 3, h + 24), (20, 20, 20))
dr = ImageDraw.Draw(sheet)
for i, (im, (_, label)) in enumerate(zip(ims, names)):
    sheet.paste(im, (i * w, 24))
    dr.text((i * w + 8, 6), f"{pose}: {label}", fill=(255, 255, 255))
sheet.save(os.path.join(HERE, f"f214_sheet_{pose}.png"))
