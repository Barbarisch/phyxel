#!/usr/bin/env bash
# P-DP on S-2 (town): overdraw off/on, then the counterbalanced cost A/B. The town must already be built
# and verified (setup_s2.py). Each step runs on its own so one failure cannot silently skip the rest.
set -u
R=/g/Github/phyxel
P=$R/docs/evidence/perf2026-09/p1
cd "$R"
post() { curl -s -m 30 -X POST "localhost:8090$1" -H "Content-Type: application/json" -d "$2"; echo; }

echo "=== overdraw, prepass OFF"
post /api/debug/depth_prepass '{"enabled":false}'
python "$P/m_overdraw_marches.py" "$P/s2_poses.json" "$P/dp_s2_overdraw_off.json"; echo "step exit $?"

echo "=== overdraw, prepass ON"
post /api/debug/depth_prepass '{"enabled":true}'
sleep 1
python "$P/m_overdraw_marches.py" "$P/s2_poses.json" "$P/dp_s2_overdraw_on.json"; echo "step exit $?"

echo "=== cost A/B"
python tools/perf_harness.py sample --poses "$P/s2_poses.json" --ab "$P/ab_depth_prepass.json" --repeats 8 --frames 240 \
  --provenance "engine-generated: POST /api/settlement/build medieval/town seed 7 80x40 terrain (M4DensityBench) = 4 buildings" \
  --out "$P/s2_dp.jsonl" > "$P/s2_dp.log" 2>&1; echo "step exit $?"
for s in "GPU Frame" "Scene Pass/Static Geometry" "Scene Pass/Depth Prepass" "Scene Pass/Grass" "Scene Pass/Foliage"; do
  python tools/perf_harness.py compare "$P/s2_dp.jsonl" --scope "$s"
done
post /api/debug/depth_prepass '{"enabled":false}'
echo "RUN DONE"
