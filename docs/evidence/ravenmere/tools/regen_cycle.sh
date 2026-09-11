#!/bin/bash
# Loop step 4 as one script: pristine DB -> temporary definition.world -> build -> save -> restore -> copy -> L4.
S="C:/Users/bpete/AppData/Local/Temp/claude/C--Users-bpete-Documents-GitHub-phyxel/efa88b40-714e-4933-8898-15ff92a04801/scratchpad"
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
        s['definition']['world']={"type":"Flat","seed":7,"from":{"x":-3,"y":0,"z":-2},"to":{"x":2,"y":0,"z":2}}
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
python - <<'PY'
# Post-build reload (AFTER the temporary world block is gone - with it present the
# loader REGENERATES flat terrain over the built town and the save writes it back; regen #18): the pre-build markers were built over; a cellar->town round trip
# re-runs the scene loader on the BUILT world (stale records replaced), then save.
import json,time,urllib.request
B='http://localhost:8090'
def call(m,p,b=None,t=120):
    req=urllib.request.Request(B+p,data=json.dumps(b).encode() if b is not None else None,method=m,headers={'Content-Type':'application/json'})
    return json.load(urllib.request.urlopen(req,timeout=t))
for sid in ('cellar','town'):
    call('POST','/api/scene/transition',{'scene_id':sid})
    for i in range(120):
        a=call('GET','/api/scene/active')
        if a.get('id')==sid and not a.get('transitioning'): break
        time.sleep(1)
    time.sleep(6)
print('post-build reload save:', call('POST','/api/world/save',{'all':True}))
PY
rm -f "$P/build/Release/worlds/town.db-wal" "$P/build/Release/worlds/town.db-shm"; cp "$P/worlds/town.db" "$P/worlds/town.db-wal" "$P/worlds/town.db-shm" "$P/build/Release/worlds/" 2>/dev/null; cp "$P/game.json" "$P/build/Release/game.json"
echo "== build log: lots / hearths / doorsteps =="; grep -i "lot .*refus\|doorsteps:\|residents:" "$S/editor_regen_$TAG.log" | cut -c1-220 | tail -12
echo "== L4 interiors =="; python "$S/l4_town_interiors.py" 2>&1 | tail -18 | cut -c1-200
# leave nothing holding phyxel.exe open for the next build
powershell -NoProfile -Command "Get-Process phyxel -ErrorAction SilentlyContinue | Stop-Process -Force"
echo "editor stopped"
