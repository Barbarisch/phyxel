#!/bin/sh
# One rung of the S-3 fixed-pose attribution (PerfProgram 2026-09 section 16.2), against the engine
# already running on that rung's project: wait for the API, settle at the anchor (fingerprint-only
# pass also re-takes the reload fingerprint with the current anchor), derive the poses from the
# generator's layout, then sample noon/night ABBA.
# Usage: run_rung_attrib.sh <prefix> <width> <depth> <buildings>
set -e
cd "$(dirname "$0")"
P=$1; W=$2; D=$3; N=$4
python -u city_build.py --fingerprint-only "C-$N" "$W" "$D" "$P" > /dev/null
python -u city_poses.py "$P" "$W" "$D" > /dev/null
cd ../../../..
python -u tools/perf_harness.py sample \
  --poses "docs/evidence/perf2026-09/p1c/${P}_poses.json" \
  --ab docs/evidence/perf2026-09/p1c/tod_noon_night.json --repeats 2 --frames 240 \
  --provenance "engine-generated: POST /api/settlement/build tier:city ${W}x${D} seed 7 density 1.5 -> ${N} buildings (p1c/${P}_build.json)" \
  --out "docs/evidence/perf2026-09/p1c/attrib_${P}.jsonl"
