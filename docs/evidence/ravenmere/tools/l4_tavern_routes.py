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
def path(a,b):
    d=get('/api/navgraph/path', x1=a[0], z1=a[1], y1=18, x2=b[0], z2=b[1], y2=18)
    return d['found'], [(round(w['x'],1),round(w['z'],1)) for w in d['waypoints']]
print('reeve->Maera       ', path((-12.5,14.5),(-13.5,-9.5)))
print('storeroom->Bram    ', path((-24.5,8.5),(-25.5,-2.5)))
print('storeroom->Hobb    ', path((-24.5,8.5),(-23.5,-2.5)))
print('door approach->in  ', path((-24.5,13.5),(-24.5,8.5)))
print('== tavern interior x -28..-22, z 11..-6 (O open, o tight, - none) ==')
for z in range(11,-7,-1):
    row=[]
    for x in range(-29,-20):
        d=get('/api/navgraph/column', x=x, z=z, y0=15, y1=22)
        s=[s for s in d['surfaces'] if 16.5 < s['feetY'] < 18.6]
        row.append('-' if not s else ('O' if s[0]['open'] else 'o'))
    print(f"z={z:3d}: "+' '.join(row))
