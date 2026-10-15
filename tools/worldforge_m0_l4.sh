#!/usr/bin/env bash
# ============================================================================
# WorldForge v2 — M0 end-of-milestone L4 on the WorldForgeBaseline lab
# (docs/WorldForgeV2Plan.md §5 M0 / §16). Re-runs the F7 baseline (8-site worldforge
# build, Release) on the M0 binary and records the "after" next to the F7 "before":
#   - sites 0 (town) and 5 (village) had a blocked street->door route in F7 and shipped
#     as "built"; M0 predicts they come back REFUSED (unwalkable) and are not checkpointed,
#     and that every hamlet logs a "walkability gate:" line (F7: none did).
#   - terrain_height?source=chunks vs source=generator at the town centre (F7: 127 vs 136).
#   - the two §16 before-poses re-captured with mode:"free" (same camera, read back).
# Usage: bash tools/worldforge_m0_l4.sh [port]   (engine must NOT be running; one heavy
#        process at a time on the laptop — docs/evidence/worldforge_v2/f7_l4_baseline.json)
# Output: docs/evidence/worldforge_v2/m0_l4_*.{json,txt,png}
# ============================================================================
set -u
PORT="${1:-8090}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PROJ="C:/Users/bpete/Documents/PhyxelProjects/WorldForgeBaseline"
EV="$ROOT/docs/evidence/worldforge_v2"
LOG="$TEMP/wfb_m0_engine.log"
API="http://localhost:$PORT"
mkdir -p "$EV"

if tasklist //FI "IMAGENAME eq phyxel.exe" | grep -q phyxel.exe; then
  echo "an engine is already running - refusing (one heavy process at a time)"; exit 1; fi
[ -x "$ROOT/build/editor/Release/phyxel.exe" ] || { echo "no Release phyxel.exe"; exit 1; }

# pristine reset: same seed/recipe as F7 (plan hash must come back 9693137997320541182)
rm -f "$PROJ/worlds/default.db" "$PROJ/worlds/default.db-wal" "$PROJ/worlds/default.db-shm"

"$ROOT/build/editor/Release/phyxel.exe" --project "$PROJ" --port "$PORT" > "$LOG" 2>&1 &
ENGINE_PID=$!
for i in $(seq 1 80); do curl -s -m 3 "$API/api/status" >/dev/null 2>&1 && break; sleep 3; done
curl -s -m 10 "$API/api/worldforge/status" > "$EV/m0_l4_status_before.json"
echo "status before: $(head -c 200 "$EV/m0_l4_status_before.json")"

R=$(curl -s -m 20 -X POST "$API/api/worldforge/build" -H 'Content-Type: application/json' -d '{"residency_timeout_s": 900}')
echo "$R" > "$EV/m0_l4_build_start.json"; echo "build start: $R"
: > "$EV/m0_l4_job_progress.txt"; last=""
for i in $(seq 1 360); do
  J=$(curl -s -m 10 "$API/api/jobs" 2>/dev/null)
  S=$(echo "$J" | python -I -c "import json,sys
try:
  d=json.load(sys.stdin); j=[x for x in d.get('jobs',[]) if x.get('id')==1000000]
  print((j[0]['state']+' | '+j[0].get('message','')+' | '+str(j[0].get('units_done'))+'/'+str(j[0].get('units_total'))+' | '+str(round(j[0].get('elapsed_seconds',0)))+'s') if j else 'gone')
except Exception as e: print('err '+str(e))")
  if [ "$S" != "$last" ]; then echo "$(date +%H:%M:%S) $S" >> "$EV/m0_l4_job_progress.txt"; last="$S"; fi
  case "$S" in running*) sleep 10;; *) break;; esac
done
echo "job end: $S"
curl -s -m 15 "$API/api/worldforge/status" > "$EV/m0_l4_status.json"
curl -s -m 15 "$API/api/jobs" > "$EV/m0_l4_jobs.json"

# surface truths at the town centre
curl -s -m 10 "$API/api/world/terrain_height?x=-1096&z=-1064" > "$EV/m0_l4_terrain_height_chunks.json"
curl -s -m 10 "$API/api/world/terrain_height?x=-1096&z=-1064&source=generator" > "$EV/m0_l4_terrain_height_generator.json"

# the two §16 poses (free camera; read back; only valid if the town streamed in)
curl -s -m 15 -X POST "$API/api/worldforge/focus" -H 'Content-Type: application/json' -d '{"x":-1096,"z":-1064}' >/dev/null
for i in $(seq 1 36); do SY=$(curl -s -m 10 "$API/api/world/terrain_height?x=-1096&z=-1064" | python -I -c "import json,sys;v=json.load(sys.stdin).get('surface_y');print('' if v is None else v)"); [ -n "$SY" ] && break; sleep 5; done
if [ -n "${SY:-}" ]; then
  sleep 20
  curl -s -m 10 -X POST "$API/api/camera" -H 'Content-Type: application/json' -d '{"mode":"free","position":{"x":-1096,"y":167,"z":-1004},"yaw":-90,"pitch":-45}' >/dev/null; sleep 6
  echo "camA: $(curl -s -m 10 "$API/api/camera" | python -I -c "import json,sys;d=json.load(sys.stdin);print(d['position'],d['yaw'],d['pitch'])")"
  A=$(curl -s -m 30 "$API/api/screenshot" | python -I -c "import json,sys;print(json.load(sys.stdin).get('path',''))"); [ -n "$A" ] && cp "$ROOT/$A" "$EV/m0_l4_after_site0_town_read.png"
  curl -s -m 10 -X POST "$API/api/camera" -H 'Content-Type: application/json' -d '{"mode":"free","position":{"x":-1084,"y":154,"z":-1070},"yaw":-90,"pitch":-30}' >/dev/null; sleep 6
  echo "camB: $(curl -s -m 10 "$API/api/camera" | python -I -c "import json,sys;d=json.load(sys.stdin);print(d['position'],d['yaw'],d['pitch'])")"
  B=$(curl -s -m 30 "$API/api/screenshot" | python -I -c "import json,sys;print(json.load(sys.stdin).get('path',''))"); [ -n "$B" ] && cp "$ROOT/$B" "$EV/m0_l4_after_site0_hallhouse_pinch.png"
fi
curl -s -m 10 -X POST "$API/api/worldforge/focus" -H 'Content-Type: application/json' -d '{}' >/dev/null
curl -s -m 20 -X POST "$API/api/world/save" -H 'Content-Type: application/json' -d '{}' >/dev/null
sleep 2
taskkill //PID "$(tasklist //FI "IMAGENAME eq phyxel.exe" | awk '/phyxel.exe/{print $2}')" //F >/dev/null 2>&1
sleep 3
sed 's/\x1b\[[0-9;]*m//g' "$LOG" > "$EV/m0_l4_engine.txt"
echo "== gate lines ==";     grep -E "walkability gate:|walkability: BLOCKED|gate BLOCKED" "$EV/m0_l4_engine.txt" | cut -c1-160
echo "== site outcomes =="; grep -E "WORLDFORGE\].*(BUILT|REFUSED)" "$EV/m0_l4_engine.txt" | cut -c1-160
echo "== spurs / fences / props after buildings =="; grep -E "door spurs:|fences:|yard props:" "$EV/m0_l4_engine.txt" | head -6 | cut -c1-160
echo "done"
