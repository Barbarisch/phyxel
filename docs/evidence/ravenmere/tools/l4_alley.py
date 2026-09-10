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
print('== alley x=-21 z -7..-3 and the old fence line x=-20 ==')
for (x,z) in [(-21,-3),(-21,-4),(-21,-5),(-21,-6),(-21,-7),(-20,-5),(-20,-3),(-19,-5)]:
    d=get('/api/navgraph/column', x=x, z=z, y0=15, y1=24)
    cubes=' '.join(f"{c['y']}{c['voxel'][0]}" for c in d['cubes'])
    surf=' | '.join(f"f{s['feetY']:.2f}h{s['headroomMicro']}e{s['edge']}sl{s['slack']}" for s in d['surfaces'])
    print(f"({x},{z}) [{cubes}] {surf or 'NONE'}")
d=get('/api/navgraph/path', x1=-12.5, z1=14.5, y1=18, x2=-13.5, z2=-9.5, y2=18)
print('reeve->Maera', d['found'], [(round(w['x'],1),round(w['z'],1)) for w in d['waypoints']])
