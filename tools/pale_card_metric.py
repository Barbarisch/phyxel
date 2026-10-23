"""pale_card_metric.py - how much of a capture is far-tree impostor card (G-178).

The impostor cards are flat, pale, desaturated mint (canopy tint lifted toward white by the
card shader's shade term); real voxel oaks are saturated green, grass is yellow-green, sky is
blue. Counting pixels that are bright (V >= 0.72), low-saturation (S <= 0.30) and green-hued
(hue 70-170 deg) inside the band between the sky and the near grass gives a number for "how
much impostor is on screen at this pose". It is a proxy - a pale cloud or a white wall would
count too - so it is only read at the SAME pose before and after.
Usage: python tools/pale_card_metric.py <png> [<png> ...]   (prints percent of the band)
"""
import colorsys
import struct
import sys
import zlib


def read_png_rgb(path):
    b = open(path, 'rb').read()
    assert b[:8] == bytes([137, 80, 78, 71, 13, 10, 26, 10]), 'not a PNG'
    pos, w, h, ctype, idat = 8, 0, 0, 0, b''
    while pos < len(b):
        ln, = struct.unpack('>I', b[pos:pos + 4]); tag = b[pos + 4:pos + 8]
        data = b[pos + 8:pos + 8 + ln]; pos += 12 + ln
        if tag == b'IHDR': w, h, bd, ctype = struct.unpack('>IIBB', data[:10]); assert bd == 8
        elif tag == b'IDAT': idat += data
        elif tag == b'IEND': break
    raw = zlib.decompress(idat)
    bpp = {2: 3, 6: 4}[ctype]
    stride = w * bpp
    rows, prev = [], bytearray(stride)
    p = 0
    for _ in range(h):
        f = raw[p]; line = bytearray(raw[p + 1:p + 1 + stride]); p += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            up = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + up) & 255
            elif f == 3: line[i] = (line[i] + ((a + up) >> 1)) & 255
            elif f == 4:
                pa, pb, pc = abs(up - c), abs(a - c), abs(a + up - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (up if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        rows.append(bytes(line)); prev = line
    return w, h, bpp, rows


def pale_fraction(path, band=(0.02, 0.55)):
    """Fraction of pixels in the vertical band [band0*h, band1*h) that read as impostor card."""
    w, h, bpp, rows = read_png_rgb(path)
    y0, y1 = int(h * band[0]), int(h * band[1])
    n = hit = 0
    for y in range(y0, y1):
        row = rows[y]
        for x in range(0, w, 2):
            r, g, b = row[x * bpp] / 255.0, row[x * bpp + 1] / 255.0, row[x * bpp + 2] / 255.0
            hh, s, v = colorsys.rgb_to_hsv(r, g, b)
            n += 1
            if v >= 0.72 and s <= 0.30 and 70.0 / 360.0 <= hh <= 170.0 / 360.0:
                hit += 1
    return hit / max(1, n), hit, n


if __name__ == '__main__':
    for p in sys.argv[1:]:
        f, hit, n = pale_fraction(p)
        print('%s: %.2f%% pale-card pixels in the mid band (%d of %d sampled)' % (p, 100.0 * f, hit, n))
