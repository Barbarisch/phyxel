#!/usr/bin/env python3
"""fetch_cc0_beds.py — replace synthesized ambience beds with REAL CC0 field recordings.

Why this exists: the synthesized wind beds (gen_ambience_beds.py) failed the ear test —
the differentiated-noise "shimmer" layer chopped at 7-11 Hz reads as rattling metal, not
leaves (user verdict 2026-09-02: "100 rattling loose metal fan blades"). Real recordings
replace every bed; the generator keeps only its one-shots (drip, gust).

Pipeline per slot:
  Freesound CC0 search (top-rated, name-excluded) -> download OGG preview -> decode
  (soundfile) -> mono -> resample to 44.1 kHz if needed -> take a segment from the middle
  (skips recorder-handling noise at head/tail) -> equal-power loop-condition (1.5 s
  crossfade) -> ASSERT the loop seam programmatically (same falsifiable test as the
  generator) -> RMS-normalize to the bed target -> write WAV -> SOURCES.json row.

Usage:
    set FREESOUND_API_KEY=<key>
    python tools/fetch_cc0_beds.py [--dry-run]
"""
import argparse
import json
import os
import sys
import urllib.parse
import urllib.request

import numpy as np
import soundfile as sf

# Windows consoles default to cp1252 — Freesound names contain arbitrary Unicode
# and a print() must never kill a fetch run.
sys.stdout.reconfigure(encoding="utf-8", errors="replace")

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOUNDS_DIR = os.path.join(REPO, "resources", "sounds")
AMB_DIR = os.path.join(SOUNDS_DIR, "ambience")
SOURCES_PATH = os.path.join(SOUNDS_DIR, "SOURCES.json")
API = "https://freesound.org/apiv2"
SR = 44100
SEGMENT_SEC = 45.0     # bed length before loop conditioning
FADE_SEC = 1.5         # equal-power wrap crossfade
TARGET_RMS = 0.05      # uniform bed loudness (quiet underlay; scatter sits on top)

# One slot per bed. Queries favor steady wind/air/insect textures; excludes kick out
# names that promise music, rain, speech, or heavy bird content (birds must come from
# the day/night-gated scatter, not be baked into a bed that loops at night too).
SLOTS = [
    {"out": "forest_bed.wav",    "query": "forest wind trees leaves ambience",
     "exclude": ["rain", "bird", "music", "storm", "thunder", "loop machine", "synth"]},
    {"out": "enchanted_bed.wav", "query": "forest night calm ambience atmosphere",
     "exclude": ["rain", "music", "scary", "horror", "synth", "talk"]},
    {"out": "jungle_bed.wav",    "query": "jungle rainforest insects ambience",
     "exclude": ["rain", "music", "talk", "monkey"]},
    # plains + savanna: text search kept surfacing junk ("waterflow", electronic
    # music), so these two are CURATED picks found via tag search (tag:wind
    # tag:grass, rating 5.0 field recordings by felix.blume — dry-grass wind,
    # mic close to the ground; slight baked-in crickets/birds accepted at the
    # quiet bed level). pick_id pins the exact recording.
    {"out": "plains_bed.wav",    "query": "wind in dry grass", "pick_id": 666176},
    {"out": "savanna_bed.wav",   "query": "wind in dry grass tree", "pick_id": 666177},
    {"out": "desert_bed.wav",    "query": "desert wind sand ambience",
     "fallbacks": ["desert ambience"],
     "exclude": ["rain", "music", "storm", "synth", "city", "plane", "traffic", "hum"]},
    {"out": "snow_bed.wav",      "query": "winter wind forest ambience",
     "fallbacks": ["cold wind ambience nature", "wind snow ambience"],
     "exclude": ["rain", "music", "synth", "beach", "sea", "surf", "city", "neighborhood",
                 "town", "traffic", "bird"]},
    {"out": "tundra_bed.wav",    "query": "arctic wind ambience",
     "exclude": ["rain", "music", "synth"]},
    {"out": "cave_bed.wav",      "query": "cave ambience drone deep",
     "exclude": ["music", "synth", "scary", "monster", "talk"]},
]


def api_get(path, params, key):
    params = dict(params, token=key)
    url = f"{API}{path}?{urllib.parse.urlencode(params)}"
    req = urllib.request.Request(url, headers={"User-Agent": "PhyxelEngine-audio-fetch/1.0"})
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.load(r)


def download(url, dest):
    req = urllib.request.Request(url, headers={"User-Agent": "PhyxelEngine-audio-fetch/1.0"})
    with urllib.request.urlopen(req, timeout=120) as r, open(dest, "wb") as f:
        f.write(r.read())


def condition_loop(x, fade_sec=FADE_SEC):
    f = int(fade_sec * SR)
    t = np.linspace(0.0, 1.0, f)
    out = x[:-f].copy()
    out[:f] = out[:f] * np.sqrt(t) + x[-f:] * np.sqrt(1.0 - t)
    return out


def measure_loop_seam(x, window_sec=0.03):
    """Returns (rms_discontinuity_db, click_ratio) at the wrap point — the SAME
    quantities the assertion gates, so segment selection optimizes exactly what
    ships."""
    w = int(window_sec * SR)
    rms_tail = np.sqrt(np.mean(x[-w:] ** 2))
    rms_head = np.sqrt(np.mean(x[:w] ** 2))
    db = abs(20 * np.log10(max(rms_tail, 1e-9) / max(rms_head, 1e-9)))
    seam_jump = abs(float(x[0]) - float(x[-1]))
    typical = max(np.percentile(np.abs(np.diff(x)), 99), 1e-9)
    return db, seam_jump / typical


def assert_loop_seam(x, name, tolerance_db=3.0):
    db, click = measure_loop_seam(x)
    assert db < tolerance_db, f"{name}: loop seam RMS discontinuity {db:.2f} dB"
    assert click <= 4.0, f"{name}: seam click (ratio {click:.2f})"


def process(raw_path, out_path, name):
    data, in_sr = sf.read(raw_path, always_2d=True)
    mono = data.mean(axis=1)
    if in_sr != SR:
        from scipy.signal import resample_poly
        from math import gcd
        g = gcd(in_sr, SR)
        mono = resample_poly(mono, SR // g, in_sr // g)
    dur = len(mono) / SR

    need = SEGMENT_SEC + FADE_SEC
    if dur < 12.0:
        raise ValueError(f"{name}: only {dur:.1f}s decoded — too short for a bed")
    seg_len = min(need, dur - 4.0)  # leave 2 s head+tail margin

    # Segment SEARCH, not just the center: gusty recordings can be 4+ dB louder
    # at one end of an arbitrary window (the tundra Arctic_1 case). Each
    # candidate offset is fully processed (normalize + loop-condition) and
    # MEASURED with the same seam metric the assertion gates — selecting on any
    # proxy metric (e.g. 0.5 s RMS windows) picked segments that still failed
    # the 30 ms wrap check.
    seg_n = int(seg_len * SR)
    max_start = len(mono) - seg_n - int(2.0 * SR)
    candidates = np.linspace(int(2.0 * SR), max(max_start, int(2.0 * SR) + 1), 24).astype(int)

    def prepare(cand):
        s = mono[cand:cand + seg_n].astype(np.float64)
        rms = np.sqrt(np.mean(s ** 2))
        if rms < 1e-5:
            return None, float("inf"), float("inf")
        s = s * (TARGET_RMS / rms)
        peak = np.abs(s).max()
        if peak > 0.98:                      # soft safety, keep crest intact
            s *= 0.98 / peak
        s = condition_loop(s)
        db, click = measure_loop_seam(s)
        return s, db, click

    best = None
    for cand in candidates:
        s, db, click = prepare(cand)
        if s is None or click > 4.0:
            continue
        if best is None or db < best[1]:
            best = (s, db, cand)
    if best is None:
        raise ValueError(f"{name}: no candidate segment yields a clean loop")
    seg, seam_db, chosen = best
    print(f"   segment search: seam {seam_db:.2f} dB at offset {chosen/SR:.1f}s")
    assert_loop_seam(seg, name)

    sf.write(out_path, (np.clip(seg, -1, 1)).astype(np.float32), SR, subtype="PCM_16")
    return len(seg) / SR


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    key = os.environ.get("FREESOUND_API_KEY", "")
    if not key:
        sys.exit("FREESOUND_API_KEY not set")

    with open(SOURCES_PATH) as f:
        sources = json.load(f)
    os.makedirs(AMB_DIR, exist_ok=True)

    used_ids = set()  # no two biomes share one recording
    for slot in SLOTS:
        flt = 'license:"Creative Commons 0" duration:[30 TO 600]'
        excludes = [e.lower() for e in slot.get("exclude", [])]
        pick = None
        if "pick_id" in slot:
            # Curated pick: fetch that sound's record directly.
            s = api_get(f"/sounds/{slot['pick_id']}/", {
                "fields": "id,name,username,license,duration,previews,url,avg_rating"}, key)
            if "creativecommons.org/publicdomain/zero" not in s.get("license", ""):
                print(f"== {slot['out']}: pinned id {slot['pick_id']} is no longer CC0 — SKIPPED")
                continue
            pick = s
            print(f"== {slot['out']}: pinned freesound id {slot['pick_id']}")
        for q in ([] if pick else [slot["query"]] + slot.get("fallbacks", [])):
            r = api_get("/search/text/", {
                "query": q, "filter": flt, "sort": "rating_desc",
                "fields": "id,name,username,license,duration,previews,url,avg_rating",
                "page_size": 10,
            }, key)
            results = r.get("results", [])
            print(f"== {slot['out']}: '{q}' -> {r.get('count', 0)} CC0 hits")
            for s in results:
                lowname = s["name"].lower()
                if s["id"] in used_ids:
                    print(f"   skip (already used by another biome): {s['name']}")
                    continue
                if any(e in lowname for e in excludes):
                    print(f"   skip: {s['name']}")
                    continue
                pick = s
                break
            if pick:
                break
        if not pick:
            print("   !! nothing suitable — bed left unchanged")
            continue
        print(f"   pick: {pick['duration']:.0f}s rating {pick.get('avg_rating', 0):.1f} "
              f"by {pick['username']}: {pick['name']}")
        used_ids.add(pick["id"])
        if args.dry_run:
            continue

        raw = os.path.join(AMB_DIR, "_raw_preview.ogg")
        download(pick["previews"]["preview-hq-ogg"] + f"?token={key}", raw)
        out_path = os.path.join(AMB_DIR, slot["out"])
        secs = process(raw, out_path, slot["out"])
        os.remove(raw)
        print(f"   wrote {slot['out']} ({secs:.1f}s loop, seam asserted)")

        sources["files"][f"ambience/{slot['out']}"] = {
            "source_url": pick["url"],
            "license": "CC0-1.0",
            "author": pick["username"],
            "freesound_id": pick["id"],
            "query": slot["query"],
            "note": f"centered {secs:.0f}s segment of the OGG preview, mono, RMS-normalized to "
                    f"{TARGET_RMS}, {FADE_SEC}s equal-power loop-conditioned + seam-asserted "
                    "(fetch_cc0_beds.py)",
        }

    if not args.dry_run:
        with open(SOURCES_PATH, "w") as f:
            json.dump(sources, f, indent=2)
        print("\nSOURCES.json updated. Rewire ambience.json to the *_bed.wav files.")


if __name__ == "__main__":
    main()
