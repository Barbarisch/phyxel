#!/usr/bin/env bash
# O16 / D71 (a): the city L4 on CityLab's creek site - the RELEASE editor, a fresh DB, worldforge_build on the pinned
# city (the recipe's settlementFords on), the ledger and the engine's own log read back, screenshots at stated poses.
# Arg: TAG -> docs/evidence/worldforge_v2/citylab_l4/citylab_l4_${TAG}_*.
set -u
TAG=$1
PORT=8600; API="http://localhost:$PORT"
ROOT="C:/Users/bpete/Documents/GitHub/phyxel"; PROJ="C:/Users/bpete/Documents/PhyxelProjects/CityLab"
EV="$ROOT/docs/evidence/worldforge_v2/citylab_l4"; LOG="$TEMP/citylab_${TAG}_engine.log"
EXE="$ROOT/build/editor/Release/phyxel.exe"
mkdir -p "$EV" "$PROJ/worlds"
tasklist //FI "IMAGENAME eq phyxel.exe" | grep -q phyxel.exe && { echo "an engine is already running - not touching it"; exit 1; }
stop_engine() { powershell.exe -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name='phyxel.exe'\" | Where-Object { \$_.CommandLine -match 'PhyxelProjects/CityLab' } | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force }" >/dev/null 2>&1; }
shot() {   # name x y z yaw pitch
  curl -s -m 10 -X POST "$API/api/camera" -H 'Content-Type: application/json' -d "{\"mode\":\"free\",\"position\":{\"x\":$2,\"y\":$3,\"z\":$4},\"yaw\":$5,\"pitch\":$6}" >/dev/null
  sleep 40
  P=$(curl -s -m 30 "$API/api/screenshot" | python -I -c "import json,sys;print(json.load(sys.stdin).get('path',''))")
  [ -n "$P" ] && cp "$ROOT/$P" "$EV/citylab_l4_${TAG}_$1.png" && echo "shot $1: pose ($2,$3,$4) yaw $5 pitch $6 -> citylab_l4_${TAG}_$1.png"
  curl -s -m 10 "$API/api/render/stats" > "$EV/citylab_l4_${TAG}_stats_$1.json"; echo "  stats $1: $(head -c 300 "$EV/citylab_l4_${TAG}_stats_$1.json")"
}
: > "$LOG"
rm -f "$PROJ/worlds/default.db" "$PROJ/worlds/default.db-wal" "$PROJ/worlds/default.db-shm" 2>/dev/null
echo "START $(date '+%F %T') exe $(ls -l --time-style=+%F_%T "$EXE" | awk '{print $6}') commit $(git -C "$ROOT" log -1 --format=%h) game.json $(sha256sum "$PROJ/game.json" | cut -c1-16)"
"$EXE" --project "$PROJ" --port "$PORT" >> "$LOG" 2>&1 &
for i in $(seq 1 100); do curl -s -m 3 "$API/api/status" >/dev/null 2>&1 && break; sleep 3; done
sleep 40
grep -a "Selected GPU" "$LOG" | head -1 | cut -c1-160
for i in $(seq 1 30); do curl -s -m 10 "$API/api/worldforge/status" > "$EV/citylab_l4_${TAG}_status0.json" 2>/dev/null && grep -q '"site_list"' "$EV/citylab_l4_${TAG}_status0.json" && break; sleep 5; done
python -I -c "
import json,sys
d=json.load(open(sys.argv[1]))
print('params echo: settlementFords', d.get('params',{}).get('settlementFords'), '| planHash', d.get('planHash', d.get('plan_hash')))
for s in d.get('site_list',[]): print('  site', s['id'], s.get('tier'), 'at', s['x'], s['z'], s['width'], 'x', s['depth'], 'surfaceY', round(s.get('surfaceY',0),1))
" "$EV/citylab_l4_${TAG}_status0.json"
CITY=$(python -I -c "
import json,sys
d=json.load(open(sys.argv[1])); c=[s for s in d.get('site_list',[]) if s.get('tier')=='city']
print(c[0]['id'], c[0]['x'], c[0]['z'], int(c[0]['surfaceY'])) if c else print(-1,0,0,0)
" "$EV/citylab_l4_${TAG}_status0.json")
read -r CID CX CZ CY <<< "$CITY"
[ "$CID" = "-1" ] && { echo "NO city site in the plan - stopping"; stop_engine; exit 1; }
echo "--- the city is site $CID at ($CX, $CZ), surfaceY $CY; focusing residency"
curl -s -m 30 -X POST "$API/api/worldforge/focus" -H 'Content-Type: application/json' -d "{\"x\":$CX,\"z\":$CZ}" | cut -c1-200; echo
sleep 75
T0=$(date +%s)
curl -s -m 30 -X POST "$API/api/worldforge/build" -H 'Content-Type: application/json' -d "{\"sites\":[$CID],\"residency_timeout_s\":420}" > "$EV/citylab_l4_${TAG}_build.json"
head -c 400 "$EV/citylab_l4_${TAG}_build.json"; echo
JOB=$(python -I -c "import json,sys; d=json.load(open(sys.argv[1])); print(d.get('job_id', d.get('job', d.get('id', ''))))" "$EV/citylab_l4_${TAG}_build.json")
echo "job $JOB"
for i in $(seq 1 480); do
  curl -s -m 10 "$API/api/job/$JOB" > "$EV/citylab_l4_${TAG}_job.json" 2>/dev/null
  if grep -q '"state": *"complete"\|"state":"complete"\|"cancelled"\|"failed"' "$EV/citylab_l4_${TAG}_job.json"; then break; fi
  tasklist //FI "IMAGENAME eq phyxel.exe" | grep -q phyxel.exe || { echo "THE ENGINE DIED during the job"; break; }
  sleep 5
done
echo "--- job after $(( $(date +%s) - T0 )) s"; head -c 1200 "$EV/citylab_l4_${TAG}_job.json"; echo
curl -s -m 30 "$API/api/worldforge/status" > "$EV/citylab_l4_${TAG}_status.json"
python -I -c "
import json,sys
d=json.load(open(sys.argv[1])); site=int(sys.argv[2])
led=d.get('ledger',{})
ent=[e for e in led.get('sites',[]) if e.get('id')==site]
if not ent: print('NO ledger entry for the city'); sys.exit(0)
e=ent[0]; m=e.get('map') or {}
print('LEDGER: site', site, 'status', e.get('status'), 'buildings', e.get('buildings'), 'lotFailures', e.get('lotFailures'), 'residents', e.get('residents'), 'error', e.get('error'))
print('   keys', sorted(k for k in e.keys() if k!='map'))
if m:
    w=m.get('wall',{})
    print('   MAP: tier', m.get('tier'), 'lots', len(m.get('lots',[])), 'of target', m.get('target_lots'), 'ways', len(m.get('ways',[])), 'crossings', len(m.get('crossings',[])), 'cells', sum(len(c.get('cells',[])) for c in m.get('crossings',[])), 'wall present', w.get('present'), 'gates', len(w.get('gates',[])), 'water gaps', len(w.get('water_gaps', w.get('waterGaps',[]))), 'market note', m.get('market_note'))
    for c in m.get('crossings',[]): print('   crossing way', c.get('way'), 'cells', c.get('cells'))
    json.dump(m, open(sys.argv[3],'w'), indent=1)
" "$EV/citylab_l4_${TAG}_status.json" "$CID" "$EV/citylab_l4_${TAG}_map.json"
sed 's/\x1b\[[0-9;]*m//g' "$LOG" | grep -aE "walkability gate|BLOCKED|square dressing|town wall:|ways: the market|lot .*refused|REFUSED|refused outcome|worldforge.*site|residents|Resident|ford fill|BUILT" | tail -40 > "$EV/citylab_l4_${TAG}_log.txt"
echo "--- the engine's own log (citylab_l4_${TAG}_log.txt)"; cut -c1-260 "$EV/citylab_l4_${TAG}_log.txt"
echo "--- residents / npcs"
curl -s -m 20 "$API/api/npcs" | head -c 600; echo
curl -s -m 20 "$API/api/entities" | python -I -c "import json,sys
try:
    d=json.load(sys.stdin); l=d.get('entities', d if isinstance(d,list) else [])
    print('entities', len(l))
except Exception as ex: print('entities: unreadable', ex)"
curl -s -m 10 -X POST "$API/api/daynight/set" -H 'Content-Type: application/json' -d '{"timeOfDay":11,"paused":true}' >/dev/null
# poses: an overview from the south looking north over the whole site; the first ford from above its street; the market
shot overview "$CX" $((CY + 110)) $((CZ + 190)) -90 -35
if [ -s "$EV/citylab_l4_${TAG}_map.json" ]; then
  POSES=$(python -I -c "
import json,sys
m=json.load(open(sys.argv[1])); ox=m['origin']['x']; oz=m['origin']['z']; cy=int(sys.argv[2])
cs=m.get('crossings',[])
if cs:
    c=cs[min(1,len(cs)-1)]['cells'][0]; print('ford', ox+c[0], cy+9, oz+c[1]+13, -90, -32); print('ford_gate', ox+c[0]-12, cy+7, oz+c[1], 0, -18)
for a in m.get('areas',[]):
    if a.get('landuse')=='market':
        r=a['rect']; x=r[0] if isinstance(r,list) else r['x']; z=r[1] if isinstance(r,list) else r['z']; w=r[2] if isinstance(r,list) else r['w']; d=r[3] if isinstance(r,list) else r['d']
        print('market', ox+x+w//2, cy+22, oz+z+d+18, -90, -40)
" "$EV/citylab_l4_${TAG}_map.json" "$CY")
  while read -r NAME PX PY PZ YAW PITCH; do [ -n "${NAME:-}" ] && shot "$NAME" "$PX" "$PY" "$PZ" "$YAW" "$PITCH"; done <<< "$POSES"
fi
echo "--- stopping the engine"; stop_engine
echo "END $(date '+%F %T')"
