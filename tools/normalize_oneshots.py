#!/usr/bin/env python3
"""normalize_oneshots.py — loudness-normalize fetched one-shots (the missing
post-process step from docs/SoundSystemV2.md §4.6).

Why: fetched Freesound previews span an ~80x RMS range (measured 2026-09-02:
castmagic zaps at 0.03 vs pain grunts at 0.17), so quiet events vanish under
loud ones no matter what the catalog volume says — and amplifying via catalog
volume >1 clips (peaks already near 1.0). The fix is offline normalization:
decode each mp3, RMS-normalize to a per-category target with a peak guard,
write a 16-bit WAV next to it, delete the mp3, and rewrite sounds.json file
references + SOURCES.json row keys in place.

Idempotent: already-normalized .wav files are skipped. Run after every
fetch_cc0_sounds.py run.
"""
import glob
import json
import os

import numpy as np
import soundfile as sf

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOUNDS = os.path.join(REPO, "resources", "sounds")
CATALOG = os.path.join(SOUNDS, "sounds.json")
SOURCES = os.path.join(SOUNDS, "SOURCES.json")

# RMS targets per top-level one-shot dir. Ambience scatter stays quieter than
# gameplay cues by design (it underlays; combat must CUT).
TARGETS = {
    "sfx/combat": 0.15,
    "sfx/magic": 0.15,
    "sfx/birds": 0.10,
    "sfx/insects": 0.10,
    "sfx/animals": 0.10,
}
PEAK_GUARD = 0.97


def normalize_file(mp3_path, target_rms):
    data, sr = sf.read(mp3_path, always_2d=True)
    mono = data.mean(axis=1)
    rms = float(np.sqrt(np.mean(mono ** 2)))
    if rms < 1e-6:
        raise ValueError(f"{mp3_path}: silent file")
    scaled = mono * (target_rms / rms)
    peak = float(np.abs(scaled).max())
    if peak > PEAK_GUARD:                      # keep crest, cap the peak
        scaled *= PEAK_GUARD / peak
    out = os.path.splitext(mp3_path)[0] + ".wav"
    sf.write(out, scaled.astype(np.float32), sr, subtype="PCM_16")
    return out, rms, float(np.sqrt(np.mean(scaled ** 2)))


def main():
    with open(CATALOG) as f:
        catalog = json.load(f)
    with open(SOURCES) as f:
        sources = json.load(f)

    renames = {}  # rel mp3 -> rel wav
    for subdir, target in TARGETS.items():
        for mp3 in sorted(glob.glob(os.path.join(SOUNDS, subdir, "*.mp3"))):
            rel = os.path.relpath(mp3, SOUNDS).replace("\\", "/")
            out, before, after = normalize_file(mp3, target)
            os.remove(mp3)
            rel_out = os.path.relpath(out, SOUNDS).replace("\\", "/")
            renames[rel] = rel_out
            print(f"  {rel}: rms {before:.4f} -> {after:.4f}")

    # Rewrite catalog file references.
    changed = 0
    for ev in catalog.get("events", {}).values():
        ev["files"] = [renames.get(f, f) for f in ev.get("files", [])]
        changed += 1

    # Migrate SOURCES rows to the new keys, noting the normalization.
    for old, new in renames.items():
        if old in sources["files"]:
            row = sources["files"].pop(old)
            note = row.get("note", "")
            row["note"] = (note + "; " if note else "") + \
                "RMS-normalized + transcoded to WAV by tools/normalize_oneshots.py"
            sources["files"][new] = row

    with open(CATALOG, "w") as f:
        json.dump(catalog, f, indent=2)
    with open(SOURCES, "w") as f:
        json.dump(sources, f, indent=2)
    print(f"{len(renames)} files normalized; catalog + provenance rewritten.")


if __name__ == "__main__":
    main()
