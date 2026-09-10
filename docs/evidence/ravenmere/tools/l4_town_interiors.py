"""L4 for increment 4: does the runtime NavGraph reach the town's building interiors?
Runs against the Release EDITOR on port 8090 with the Ravenmere project loaded.
Writes docs/evidence/ravenmere/inc4_navgraph_town_interiors.json."""
import json, time, urllib.request, urllib.parse, sys
B='http://localhost:8090'
def call(method, path, body=None, t=60):
    req=urllib.request.Request(B+path, data=json.dumps(body).encode() if body is not None else None, method=method, headers={'Content-Type':'application/json'})
    return json.load(urllib.request.urlopen(req, timeout=t))
def get(path, **q): return call('GET', path+'?'+urllib.parse.urlencode(q))
for i in range(180):
    try: call('GET','/api/state'); break
    except Exception: time.sleep(1)
else: sys.exit('editor never answered')
print('editor up after', i, 's')
if call('GET','/api/scene/active').get('id') != 'town':
    print('transition:', call('POST','/api/scene/transition',{'scene_id':'town'}))
    for i in range(90):
        a=call('GET','/api/scene/active')
        if a.get('id')=='town' and not a.get('transitioning'): break
        time.sleep(1)
    time.sleep(8)
# poke a far voxel so the editor rebuilds its nav grid on the town (the editor has no per-scene rebuild)
call('POST','/api/world/fill',{'from':{'x':200,'y':60,'z':200},'to':{'x':200,'y':60,'z':200},'material':'Stone'}); time.sleep(8)
call('POST','/api/world/clear',{'from':{'x':200,'y':60,'z':200},'to':{'x':200,'y':60,'z':200}}); time.sleep(8)
street=dict(x1=-36.0,z1=15.0,y1=18)
def path(x2,z2,y2=18, **src):
    s=dict(street); s.update(src)
    return get('/api/navgraph/path', **s, x2=x2, z2=z2, y2=y2)
named={'street->Hobb (bar)':(-22.0,-3.0), 'street->Bram (stair rail)':(-26.0,-3.0), 'street->hatch':(-25.0,12.8), 'street->street control':(-13.0,14.5)}
out={'named':{}, 'houses':{}}
for k,(x,z) in named.items():
    r=path(x,z); out['named'][k]={'found':r.get('found'), 'waypoints':len(r.get('waypoints',[])), 'error':r.get('error')}
    print(f"{k:28s} found={r.get('found')} wps={len(r.get('waypoints',[]))} {r.get('error') or ''}")
r=get('/api/navgraph/path', x1=-25.0,z1=-7.6,y1=17, x2=-22.0,z2=-3.0,y2=18); out['named']['doorstep->Hobb']={'found':r.get('found')}; print('doorstep->Hobb found=',r.get('found'))
objs=call('GET','/api/placed_objects')['objects']
houses=[o for o in objs if o['id'].startswith('house_')]
tot_ok=tot=0
for h in houses:
    d=call('GET','/api/placed_object?id='+h['id'])
    lo=d.get('bounding_min') or d.get('object',{}).get('bounding_min'); hi=d.get('bounding_max') or d.get('object',{}).get('bounding_max')
    if not lo: print(h['id'],'no bounds', json.dumps(d)[:200]); continue
    # interior sample grid: 1 cube inset from the bbox, every 2 cubes, at ground-floor feet height
    pts=[]; ok=0; fails=[]
    y2 = lo['y'] + 2   # bbox min y = foundation; ground floor feet ~ min+1..+2 (surfaceAt has a 1-cube slack)
    for x in range(int(lo['x'])+2, int(hi['x'])-1, 2):
        for z in range(int(lo['z'])+2, int(hi['z'])-1, 2):
            r=path(x+0.5, z+0.5, y2); pts.append((x,z,bool(r.get('found'))))
            if r.get('found'): ok+=1
            else: fails.append((x,z))
    tot_ok+=ok; tot+=len(pts)
    out['houses'][h['id']]={'bbox':[lo,hi],'reachable':ok,'sampled':len(pts),'unreachable':fails}
    print(f"{h['id']:9s} bbox x{lo['x']}..{hi['x']} z{lo['z']}..{hi['z']} y{lo['y']}..{hi['y']}  interior samples reachable {ok}/{len(pts)}  fails={fails[:6]}")
print(f"TOTAL interior samples reachable from the street: {tot_ok}/{tot}")
out['total']={'reachable':tot_ok,'sampled':tot}
json.dump(out, open('C:/Users/bpete/Documents/GitHub/phyxel/docs/evidence/ravenmere/inc4_navgraph_town_interiors.json','w'), indent=1)

# --- the hall's street door after the passage-cell + doorstep fixes (G-68 / G-71) ---
def col(x,z):
    d=get('/api/navgraph/column', x=x, z=z, y0=16, y1=22)
    return ' '.join(f"{c['y']}{c['voxel'][0]}{'!' if c['obstacle'] else ''}" for c in d['cubes']), ' | '.join(f"f{s['feetY']:.2f}h{s['headroomMicro']}e{s['edge']}" for s in d['surfaces'] if s['feetY']<19.5)
print('== hall door column x=-15, z 12..7 (street side z=11 -> inside z=8) ==')
for z in (12,11,10,9,8,7):
    c,sf=col(-15,z); print(f"  (-15,{z}) [{c}] {sf or 'NO SURFACE'}")
r=get('/api/navgraph/path', x1=-36,z1=15,y1=18, x2=-15.5,z2=8.5,y2=18.6); print('street -> hall inside (-15.5,8.5): found=', r.get('found'), 'wps', len(r.get('waypoints',[])))
print('== doorsteps log ==')

# --- the story's anchors in the regenerated tavern (door on the NORTH wall at (-25,10)) ---
print('== story anchors ==')
for name,(x,z,y) in {'Hobb behind the bar (-22.5,-3.5)':(-22.5,-3.5,18.4),'Bram at the stair rail (-26.5,-3.5)':(-26.5,-3.5,18.4),'hatch behind the tavern, south side (-25,-7.5)':(-25.0,-7.5,17.0),'Maera at the shrine (-14,-11)':(-14.0,-11.0,18.0),'farm exit (60,15)':(60.0,15.0,18.0)}.items():
    r=get('/api/navgraph/path', x1=-36,z1=15,y1=18, x2=x, z2=z, y2=y)
    print(f"  street -> {name:48s} found={r.get('found')} wps={len(r.get('waypoints',[]))}")
