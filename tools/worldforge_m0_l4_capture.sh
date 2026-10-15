#!/usr/bin/env bash
# ============================================================================
# WorldForge v2 M0 L4 — targeted capture of a BUILT site on the SAVED lab world (no reset,
# no build): launch, focus the site, put the free camera next to a building, read the pose
# back, screenshot, kill. Written because the section-16 poses (town-relative, 60 u out) framed
# only jungle canopy at hamlet 2 (m0_l4d_after_site2_*.png). Usage:
#   bash tools/worldforge_m0_l4_capture.sh <prefix> <siteX> <siteZ> <camX> <camY> <camZ> <yaw> <pitch> [port]
# Output: docs/evidence/worldforge_v2/<prefix>_capture.png + <prefix>_capture.txt (pose read-back)
# ============================================================================
set -u
PREFIX="$1"; SX="$2"; SZ="$3"; CX="$4"; CY="$5"; CZ="$6"; YAW="$7"; PITCH="$8"; PORT="${9:-8090}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PROJ="C:/Users/bpete/Documents/PhyxelProjects/WorldForgeBaseline"
EV="$ROOT/docs/evidence/worldforge_v2"
LOG="$TEMP/wfb_capture_engine.log"
API="http://localhost:$PORT"
if tasklist //FI "IMAGENAME eq phyxel.exe" | grep -q phyxel.exe; then echo "an engine is already running - refusing"; exit 1; fi
"$ROOT/build/editor/Release/phyxel.exe" --project "$PROJ" --port "$PORT" > "$LOG" 2>&1 &
for i in $(seq 1 80); do curl -s -m 3 "$API/api/status" >/dev/null 2>&1 && break; sleep 3; done
curl -s -m 15 -X POST "$API/api/worldforge/focus" -H 'Content-Type: application/json' -d "{\"x\":$SX,\"z\":$SZ}" >/dev/null
for i in $(seq 1 40); do SY=$(curl -s -m 10 "$API/api/world/terrain_height?x=$SX&z=$SZ" | python -I -c "import json,sys;v=json.load(sys.stdin).get('surface_y');print('' if v is None else v)"); [ -n "$SY" ] && break; sleep 5; done
echo "site ($SX,$SZ) resident surface_y: ${SY:-none}" | tee "$EV/${PREFIX}_capture.txt"
sleep 25
curl -s -m 10 -X POST "$API/api/camera" -H 'Content-Type: application/json' -d "{\"mode\":\"free\",\"position\":{\"x\":$CX,\"y\":$CY,\"z\":$CZ},\"yaw\":$YAW,\"pitch\":$PITCH}" >/dev/null; sleep 8
echo "cam sent ($CX,$CY,$CZ) yaw $YAW pitch $PITCH; read back: $(curl -s -m 10 "$API/api/camera" | python -I -c "import json,sys;d=json.load(sys.stdin);print(d['position'],d['yaw'],d['pitch'])")" | tee -a "$EV/${PREFIX}_capture.txt"
P=$(curl -s -m 30 "$API/api/screenshot" | python -I -c "import json,sys;print(json.load(sys.stdin).get('path',''))"); [ -n "$P" ] && cp "$ROOT/$P" "$EV/${PREFIX}_capture.png" && echo "saved ${PREFIX}_capture.png" | tee -a "$EV/${PREFIX}_capture.txt"
curl -s -m 10 -X POST "$API/api/worldforge/focus" -H 'Content-Type: application/json' -d '{}' >/dev/null
sleep 2
taskkill //PID "$(tasklist //FI "IMAGENAME eq phyxel.exe" | awk '/phyxel.exe/{print $2}')" //F >/dev/null 2>&1
echo done
