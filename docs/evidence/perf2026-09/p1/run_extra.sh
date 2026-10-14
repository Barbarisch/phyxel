#!/usr/bin/env bash
# Second P1 pass: tier cost with shading removed (lights off; raster-only mode 11). Usage: run_extra.sh <tag> <poses> "<prov>"
set -u; TAG=$1; POSES=$2; PROV=$3
D=$(cd "$(dirname "$0")" && pwd); R=$(cd "$D/../../../.." && pwd); cd "$R"
for AB in nolights_main_no_micro nolights_main_no_sub raster_main_no_micro raster_main_no_sub; do
  echo "=== $TAG $AB"
  python tools/perf_harness.py sample --poses "$POSES" --ab "$D/ab_$AB.json" --repeats 8 --frames 240 \
    --provenance "$PROV" --out "$D/${TAG}_${AB}.jsonl" > "$D/${TAG}_${AB}.log" 2>&1
  echo "exit $?"
done
curl -s -X POST localhost:8090/api/debug/shadow -H "Content-Type: application/json" -d '{"mode":0}' > /dev/null
echo "EXTRA DONE"
