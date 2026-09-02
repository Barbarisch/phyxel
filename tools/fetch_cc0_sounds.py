#!/usr/bin/env python3
"""fetch_cc0_sounds.py — scripted CC0 fetch from Freesound (docs/SoundSystemV2.md §4.6).

The audio analog of fetch_cc0_textures.py: manifest in, provenance-tracked assets out.
Exists because the categories AI/synthesis does badly — birdsong, animal vocalizations,
footsteps — must come from real recordings, and Wikimedia/xeno-canto material is mostly
CC BY-SA (share-alike; excluded for shipped assets). Freesound's API filters to true CC0.

Usage:
    set FREESOUND_API_KEY=<key from freesound.org/apiv2/apply>
    python tools/fetch_cc0_sounds.py [--manifest tools/audio_fetch_manifest.json] [--dry-run]

Auth note: a plain API token grants search + PREVIEW downloads (128 kbps mp3 — fine for
distant ambience scatter under a bed; miniaudio decodes mp3). Full-quality originals need
OAuth2; upgrade later if a hero sound demands it.

Every fetched file gets a SOURCES.json row {source_url, license, author, freesound_id,
query} written automatically — the tool is the only writer for fetched rows.
"""
import argparse
import json
import os
import re
import sys
import urllib.parse
import urllib.request

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOUNDS_DIR = os.path.join(REPO, "resources", "sounds")
SOURCES_PATH = os.path.join(SOUNDS_DIR, "SOURCES.json")
API = "https://freesound.org/apiv2"


def api_get(path, params, key):
    params = dict(params, token=key)
    url = f"{API}{path}?{urllib.parse.urlencode(params)}"
    req = urllib.request.Request(url, headers={"User-Agent": "PhyxelEngine-audio-fetch/1.0"})
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.load(r)


def download(url, dest, key):
    req = urllib.request.Request(f"{url}?token={key}" if "token=" not in url else url,
                                 headers={"User-Agent": "PhyxelEngine-audio-fetch/1.0"})
    with urllib.request.urlopen(req, timeout=60) as r, open(dest, "wb") as f:
        f.write(r.read())


def slugify(s):
    return re.sub(r"[^a-z0-9]+", "_", s.lower()).strip("_")[:48]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--manifest", default=os.path.join(REPO, "tools", "audio_fetch_manifest.json"))
    ap.add_argument("--dry-run", action="store_true", help="search + report, download nothing")
    args = ap.parse_args()

    key = os.environ.get("FREESOUND_API_KEY", "")
    if not key:
        sys.exit("FREESOUND_API_KEY not set — get one at https://freesound.org/apiv2/apply")

    with open(args.manifest) as f:
        manifest = json.load(f)
    with open(SOURCES_PATH) as f:
        sources = json.load(f)

    fetched = []
    for slot in manifest["slots"]:
        q = slot["query"]
        out_dir = os.path.join(SOUNDS_DIR, slot["dir"])
        os.makedirs(out_dir, exist_ok=True)
        flt = f'license:"Creative Commons 0" duration:[{slot.get("minSec", 1)} TO {slot.get("maxSec", 12)}]'
        r = api_get("/search/text/", {
            "query": q, "filter": flt, "sort": "rating_desc",
            "fields": "id,name,username,license,duration,previews,url",
            "page_size": slot.get("count", 3) * 2,   # headroom for skips
        }, key)

        results = r.get("results", [])[: slot.get("count", 3) * 2]
        print(f"== '{q}' -> {r.get('count', 0)} CC0 hits")
        taken = 0
        excludes = [e.lower() for e in slot.get("exclude", [])]
        for s in results:
            if taken >= slot.get("count", 3):
                break
            if s.get("license") and "creativecommons.org/publicdomain/zero" not in s["license"]:
                continue  # belt-and-braces: trust the filter but verify the row
            lowname = s["name"].lower()
            if any(e in lowname for e in excludes):
                print(f"   skip (excluded term): {s['name']}")
                continue
            fname = f'{slot["prefix"]}_{slugify(s["name"])}_{s["id"]}.mp3'
            rel = f'{slot["dir"]}/{fname}'
            dest = os.path.join(out_dir, fname)
            preview = s["previews"]["preview-hq-mp3"]
            print(f"   {s['duration']:5.1f}s  by {s['username']:<20} {fname}")
            if not args.dry_run:
                download(preview, dest, key)
                sources["files"][rel] = {
                    "source_url": s["url"],
                    "license": "CC0-1.0",
                    "author": s["username"],
                    "freesound_id": s["id"],
                    "query": q,
                    "note": "preview-quality mp3 (token auth); OAuth2 for original if needed",
                }
            fetched.append(rel)
            taken += 1

    if not args.dry_run:
        with open(SOURCES_PATH, "w") as f:
            json.dump(sources, f, indent=2)
        print(f"\n{len(fetched)} files fetched; SOURCES.json updated.")
        print("Next: add them to variation pools in sounds.json / scatter in ambience.json.")
    else:
        print(f"\nDRY RUN: {len(fetched)} files would be fetched.")


if __name__ == "__main__":
    main()
