#!/bin/bash
# Loop step 4 as one script: pristine DB -> temporary definition.world -> build -> save -> restore -> copy -> L4.
S="$(cd "$(dirname "$0")" && pwd)"
P="C:/Users/bpete/Documents/PhyxelProjects/Ravenmere"
cd ~/Documents/GitHub/phyxel || exit 1
TAG=$1
powershell -NoProfile -Command "Get-Process phyxel -ErrorAction SilentlyContinue | Stop-Process -Force; Start-Sleep 1"
mkdir -p "$P/worlds/backup_$TAG"; cp "$P/worlds/town.db" "$P/worlds/town.db-wal" "$P/worlds/town.db-shm" "$P/worlds/backup_$TAG/" 2>/dev/null
rm -f "$P/worlds/town.db" "$P/worlds/town.db-wal" "$P/worlds/town.db-shm"
python - <<'PY'
import json
P=r'C:\Users\bpete\Documents\PhyxelProjects\Ravenmere\game.json'
g=json.load(open(P,encoding='utf-8'))
for s in g['scenes']:
    if s['id']=='town':
        s['definition']['world']={"type":"Flat","seed":7,"from":{"x":-2,"y":0,"z":-1},"to":{"x":1,"y":0,"z":1}}
json.dump(g, open(P,'w',encoding='utf-8'), indent=2); print('temporary world block set')
PY
(build/editor/Release/phyxel.exe --project "C:\Users\bpete\Documents\PhyxelProjects\Ravenmere" > "$S/editor_regen_$TAG.log" 2>&1 &)
python "$S/regen_town_pristine.py" 2>&1 | grep -v "^  \.\.\." | cut -c1-200 | head -8
python "$S/regen_finish.py" 2>&1 | grep -v "^  \.\.\." | cut -c1-300
python - <<'PY'
import json
P=r'C:\Users\bpete\Documents\PhyxelProjects\Ravenmere\game.json'
g=json.load(open(P,encoding='utf-8'))
for s in g['scenes']:
    if s['id']=='town': s['definition'].pop('world',None)
json.dump(g, open(P,'w',encoding='utf-8'), indent=2); print('temporary world block removed')
PY
rm -f "$P/build/Release/worlds/town.db-wal" "$P/build/Release/worlds/town.db-shm"; cp "$P/worlds/town.db" "$P/worlds/town.db-wal" "$P/worlds/town.db-shm" "$P/build/Release/worlds/" 2>/dev/null; cp "$P/game.json" "$P/build/Release/game.json"
echo "== build log: lots / hearths / doorsteps =="; grep -i "lot .*refus\|doorsteps:\|residents:" "$S/editor_regen_$TAG.log" | cut -c1-220 | tail -12
echo "== L4 interiors =="; python "$S/l4_town_interiors.py" 2>&1 | tail -18 | cut -c1-200
# leave nothing holding phyxel.exe open for the next build
powershell -NoProfile -Command "Get-Process phyxel -ErrorAction SilentlyContinue | Stop-Process -Force"
echo "editor stopped"
