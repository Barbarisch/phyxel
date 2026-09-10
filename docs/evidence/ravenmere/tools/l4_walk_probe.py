import json, time, urllib.request, urllib.parse, sys
B='http://localhost:8090'
def call(method, path, body=None, t=60):
    req=urllib.request.Request(B+path, data=json.dumps(body).encode() if body is not None else None, method=method, headers={'Content-Type':'application/json'})
    return json.load(urllib.request.urlopen(req, timeout=t))
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
for label,(x,y,z) in [('croft @-37,6 marker',(-302,153,166)),('general_store marker',(-32,153,166)),('hall_house @16,4 marker',(184,153,166)),('tavern @36,18 marker',(319,153,103)),('croft @43,18 marker',(382,165,103)),('a paved street cell nearby',(-302,154,140))]:
    d=call('GET','/api/walk/probe?'+urllib.parse.urlencode(dict(x=x,y=y,z=z,radius=6)))
    if 'error' in d: print(label, d); continue
    print(f"{label}: settled={d['settled_feet']} fits={d['fits_at_start']} sup={d['supported_at_start']} reached={d['reached']}")
    for s in d['first_steps']:
        print('   step', s['dx'], s['dz'], [(t['h'],int(t['fits']),int(t['supported'])) for t in s['tries']])
