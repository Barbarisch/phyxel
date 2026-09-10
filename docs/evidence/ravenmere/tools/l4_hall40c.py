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
    time.sleep(6)
call('POST','/api/world/fill',{'from':{'x':200,'y':60,'z':200},'to':{'x':200,'y':60,'z':200},'material':'Stone'}); time.sleep(8)
call('POST','/api/world/clear',{'from':{'x':200,'y':60,'z':200},'to':{'x':200,'y':60,'z':200}}); time.sleep(8)
for z in (11,10,9,8):
    row=[]
    for x in range(42,47):
        dd=get('/api/navgraph/column', x=x, z=z, y0=15, y1=24)
        cubes=' '.join(f"{c['y']}{c['voxel'][0]}" for c in dd['cubes'])
        s=[q for q in dd['surfaces'] if 16.5 < q['feetY'] < 19.5]
        row.append(f"x{x}:[{cubes}] {'-' if not s else 'f%.2f h%d e%s' % (s[0]['feetY'], s[0]['headroomMicro'], s[0]['edge'])}")
    print(f"z={z}: " + ' | '.join(row))
for (x,z) in [(44,10),(44,9)]:
    r=call('POST','/api/world/scan_micro', {'x1':x,'y1':16,'z1':z,'x2':x,'y2':19,'z2':z})
    print('micro', (x,z), [(c['y'], sum(c['counts'].values()), list(c['counts'].keys())) for c in r.get('cells',[])])
    d=get('/api/walk/probe', x=x*9+4, y=156, z=z*9+4, radius=3)
    print('probe', (x,z), 'settled', d['settled_feet'], 'fits', d['fits_at_start'], 'reached', d['reached'])
