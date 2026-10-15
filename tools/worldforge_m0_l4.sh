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
# Usage: [PREFIX=m0_l4b] bash tools/worldforge_m0_l4.sh [port]   (engine must NOT be running; one heavy
#        process at a time on the laptop — docs/evidence/worldforge_v2/f7_l4_baseline.json)
# Output: docs/evidence/worldforge_v2/$PREFIX_*.{json,txt,png} (PREFIX default m0_l4; run 2 after the
#         row 2b fixes used m0_l4b) + $PREFIX_run.txt (this script's own output, camera read-backs included)
# ============================================================================
set -u
PORT="${1:-8090}"
PREFIX="${PREFIX:-m0_l4}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PROJ="C:/Users/bpete/Documents/PhyxelProjects/WorldForgeBaseline"
EV="$ROOT/docs/evidence/worldforge_v2"
LOG="$TEMP/wfb_m0_engine.log"
API="http://localhost:$PORT"
mkdir -p "$EV"
exec > >(tee "$EV/${PREFIX}_run.txt") 2>&1

if tasklist //FI "IMAGENAME eq phyxel.exe" | grep -q phyxel.exe; then
  echo "an engine is already running - refusing (one heavy process at a time)"; exit 1; fi
[ -x "$ROOT/build/editor/Release/phyxel.exe" ] || { echo "no Release phyxel.exe"; exit 1; }

# pristine reset: same seed/recipe as F7 (plan hash must come back 9693137997320541182)
rm -f "$PROJ/worlds/default.db" "$PROJ/worlds/default.db-wal" "$PROJ/worlds/default.db-shm"

"$ROOT/build/editor/Release/phyxel.exe" --project "$PROJ" --port "$PORT" > "$LOG" 2>&1 &
ENGINE_PID=$!
for i in $(seq 1 80); do curl -s -m 3 "$API/api/status" >/dev/null 2>&1 && break; sleep 3; done
curl -s -m 10 "$API/api/worldforge/status" > "$EV/${PREFIX}_status_before.json"
echo "status before: $(head -c 200 "$EV/${PREFIX}_status_before.json")"

R=$(curl -s -m 20 -X POST "$API/api/worldforge/build" -H 'Content-Type: application/json' -d '{"residency_timeout_s": 900}')
echo "$R" > "$EV/${PREFIX}_build_start.json"; echo "build start: $R"
: > "$EV/${PREFIX}_job_progress.txt"; last=""
for i in $(seq 1 360); do
  J=$(curl -s -m 10 "$API/api/jobs" 2>/dev/null)
  S=$(echo "$J" | python -I -c "import json,sys
try:
  d=json.load(sys.stdin); j=[x for x in d.get('jobs',[]) if x.get('id')==1000000]
  print((j[0]['state']+' | '+j[0].get('message','')+' | '+str(j[0].get('units_done'))+'/'+str(j[0].get('units_total'))+' | '+str(round(j[0].get('elapsed_seconds',0)))+'s') if j else 'gone')
except Exception as e: print('err '+str(e))")
  if [ "$S" != "$last" ]; then echo "$(date +%H:%M:%S) $S" >> "$EV/${PREFIX}_job_progress.txt"; last="$S"; fi
  case "$S" in running*) sleep 10;; *) break;; esac
done
echo "job end: $S"
curl -s -m 15 "$API/api/worldforge/status" > "$EV/${PREFIX}_status.json"
curl -s -m 15 "$API/api/jobs" > "$EV/${PREFIX}_jobs.json"

# capture target: site 0 (the town) if it BUILT, else the first BUILT site (status.site_list +
# ledger); nothing built -> no A/B, no captures (a refused site is evicted on focus release).
EVW=$(cygpath -m "$EV")   # python on Windows cannot open a /c/... path
TGT=$(python -I -c "import json
st=json.load(open('$EVW/${PREFIX}_status.json'))
led={s['id']:s for s in st.get('ledger',{}).get('sites',[])}
sites={s['id']:s for s in st.get('site_list',[])}
for i in [0]+[k for k in sorted(sites) if k!=0]:
    if led.get(i,{}).get('status')=='built' and i in sites:
        s=sites[i]; print(i, s['x'], s['z'], s['surfaceY']); break")
echo "capture target (id x z surfaceY): ${TGT:-none}"
if [ -n "$TGT" ]; then
  set -- $TGT; SID=$1; CX=$2; CZ=$3; SY0=$4
  curl -s -m 15 -X POST "$API/api/worldforge/focus" -H 'Content-Type: application/json' -d "{\"x\":$CX,\"z\":$CZ}" >/dev/null
  for i in $(seq 1 36); do SY=$(curl -s -m 10 "$API/api/world/terrain_height?x=$CX&z=$CZ" | python -I -c "import json,sys;v=json.load(sys.stdin).get('surface_y');print('' if v is None else v)"); [ -n "$SY" ] && break; sleep 5; done
  # the two surface truths at the site centre (F7: town 127 chunks vs 136 generator)
  curl -s -m 10 "$API/api/world/terrain_height?x=$CX&z=$CZ" > "$EV/${PREFIX}_terrain_height_chunks.json"
  curl -s -m 10 "$API/api/world/terrain_height?x=$CX&z=$CZ&source=generator" > "$EV/${PREFIX}_terrain_height_generator.json"
  echo "terrain_height site $SID: chunks $(cat "$EV/${PREFIX}_terrain_height_chunks.json" | head -c 160) | generator $(cat "$EV/${PREFIX}_terrain_height_generator.json" | head -c 160)"
  if [ -n "${SY:-}" ]; then
    sleep 20
    # the two section-16 poses, relative to the site centre (site 0 with surfaceY 136 reproduces
    # the F7 poses exactly: A (-1096,167,-1004) yaw -90 pitch -45; B (-1084,154,-1070) yaw -90 pitch -30)
    AY=$((SY0 + 31)); AZ=$((CZ + 60)); BX=$((CX + 12)); BY=$((SY0 + 18)); BZ=$((CZ - 6))
    curl -s -m 10 -X POST "$API/api/camera" -H 'Content-Type: application/json' -d "{\"mode\":\"free\",\"position\":{\"x\":$CX,\"y\":$AY,\"z\":$AZ},\"yaw\":-90,\"pitch\":-45}" >/dev/null; sleep 6
    echo "camA (site $SID): $(curl -s -m 10 "$API/api/camera" | python -I -c "import json,sys;d=json.load(sys.stdin);print(d['position'],d['yaw'],d['pitch'])")"
    A=$(curl -s -m 30 "$API/api/screenshot" | python -I -c "import json,sys;print(json.load(sys.stdin).get('path',''))"); [ -n "$A" ] && cp "$ROOT/$A" "$EV/${PREFIX}_after_site${SID}_read.png"
    curl -s -m 10 -X POST "$API/api/camera" -H 'Content-Type: application/json' -d "{\"mode\":\"free\",\"position\":{\"x\":$BX,\"y\":$BY,\"z\":$BZ},\"yaw\":-90,\"pitch\":-30}" >/dev/null; sleep 6
    echo "camB (site $SID): $(curl -s -m 10 "$API/api/camera" | python -I -c "import json,sys;d=json.load(sys.stdin);print(d['position'],d['yaw'],d['pitch'])")"
    B=$(curl -s -m 30 "$API/api/screenshot" | python -I -c "import json,sys;print(json.load(sys.stdin).get('path',''))"); [ -n "$B" ] && cp "$ROOT/$B" "$EV/${PREFIX}_after_site${SID}_close.png"
  fi
fi
curl -s -m 10 -X POST "$API/api/worldforge/focus" -H 'Content-Type: application/json' -d '{}' >/dev/null
curl -s -m 20 -X POST "$API/api/world/save" -H 'Content-Type: application/json' -d '{}' >/dev/null
sleep 2
taskkill //PID "$(tasklist //FI "IMAGENAME eq phyxel.exe" | awk '/phyxel.exe/{print $2}')" //F >/dev/null 2>&1
sleep 3
sed 's/\x1b\[[0-9;]*m//g' "$LOG" > "$EV/${PREFIX}_engine.txt"
echo "== gate lines ==";     grep -E "walkability gate:|walkability: BLOCKED|gate BLOCKED" "$EV/${PREFIX}_engine.txt" | cut -c1-160
echo "== site outcomes =="; grep -E "WORLDFORGE\].*(BUILT|REFUSED)" "$EV/${PREFIX}_engine.txt" | cut -c1-160
echo "== spurs / fences / props after buildings =="; grep -E "door spurs:|fences:|yard props:" "$EV/${PREFIX}_engine.txt" | head -6 | cut -c1-160
echo "done"
