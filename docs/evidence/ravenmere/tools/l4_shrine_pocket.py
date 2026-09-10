"""Dump NavGraph columns around the shrine pocket where run 33 stalled at (-13.8,-4.6)."""
import json, time, urllib.request, urllib.parse
B='http://localhost:8090'
def call(method, path, body=None, t=60):
    req=urllib.request.Request(B+path, data=json.dumps(body).encode() if body is not None else None, method=method, headers={'Content-Type':'application/json'})
    return json.load(urllib.request.urlopen(req, timeout=t))
def get(path, **q): return call('GET', path+'?'+urllib.parse.urlencode(q))
for i in range(180):
    try: call('GET','/api/state'); break
    except Exception: time.sleep(1)
if call('GET','/api/scene/active').get('id') != 'town':
    call('POST','/api/scene/transition',{'scene_id':'town'})
    for i in range(90):
        a=call('GET','/api/scene/active')
        if a.get('id')=='town' and not a.get('transitioning'): break
        time.sleep(1)
    time.sleep(8)
call('POST','/api/world/fill',{'from':{'x':200,'y':60,'z':200},'to':{'x':200,'y':60,'z':200},'material':'Stone'}); time.sleep(8)
call('POST','/api/world/clear',{'from':{'x':200,'y':60,'z':200},'to':{'x':200,'y':60,'z':200}}); time.sleep(8)
def col(x,z):
    d=get('/api/navgraph/column', x=x, z=z, y0=15, y1=22)
    surf=' | '.join(f"f{s['feetY']:.2f}h{s['headroomMicro']}e{s['edge']}" for s in d.get('surfaces',[]))
    cubes=' '.join(f"{c['y']}{c['voxel'][0]}{'!' if c['obstacle'] else ''}" for c in d.get('cubes',[]))
    return f"[{cubes}] {surf or 'NONE'}"
print('== grid x -21..-11, z -3..-11 (row = z) ==')
for z in range(-3,-12,-1):
    print(f"z={z:4d}: " + ' || '.join(f"x{x}:{col(x,z)}" for x in range(-21,-10)))
print('== objects near the shrine ==')
objs=call('GET','/api/objects/placed') if True else []
try:
    for o in objs.get('objects',objs) if isinstance(objs,dict) else objs:
        p=o.get('position',{}); 
        if -22<=p.get('x',999)<=-10 and -12<=p.get('z',999)<=-2: print(o.get('id'),o.get('template'),p)
except Exception as e: print('objects:',e)
print('== path from the pocket and to it ==')
for a,b in [((-13.5,-4.5),(-13.5,-9.5)),((-18.5,-7.5),(-13.5,-9.5)),((-13.5,14.5),(-13.5,-4.5))]:
    r=call('POST','/api/rpg/navgraph_path',{'x1':a[0],'z1':a[1],'y1':18,'x2':b[0],'z2':b[1],'y2':18})
    print(a,'->',b,'found',r.get('found'),[ (round(w['x'],1),round(w['z'],1)) for w in r.get('waypoints',[])])
