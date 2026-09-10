import json, time, urllib.request, urllib.parse, collections, re
B='http://localhost:8090'
def call(method, path, body=None, t=120):
    req=urllib.request.Request(B+path, data=json.dumps(body).encode() if body is not None else None, method=method, headers={'Content-Type':'application/json'})
    return json.load(urllib.request.urlopen(req, timeout=t))
t0=time.time()
while time.time()-t0 < 1200:
    jobs=call('GET','/api/jobs'); js=jobs if isinstance(jobs,list) else jobs.get('jobs',[])
    j=[x for x in js if x.get('id')==1000000]
    if j and j[0].get('state') not in ('running','queued','pending'):
        print('job final:', json.dumps({k:j[0].get(k) for k in ('state','elapsed_seconds','message','units_done','units_total')})); break
    if j: print('  ...', j[0].get('state'), j[0].get('units_done'), '/', j[0].get('units_total'), j[0].get('message'))
    time.sleep(15)
res=call('GET','/api/jobs')
js=res if isinstance(res,list) else res.get('jobs',[])
j=[x for x in js if x.get('id')==1000000]
if j: print('result keys:', list((j[0].get('result') or {}).keys())[:20]); print('result:', json.dumps(j[0].get('result'))[:900])
time.sleep(3)
print('save:', call('POST','/api/world/save',{'all':True}, t=120))
objs=call('GET','/api/placed_objects')['objects']
key=collections.Counter((re.sub(r'_\d+$','',o['id']),o['position']['x'],o['position']['y'],o['position']['z']) for o in objs)
dups=[k for k,c in key.items() if c>1]
print('placed objects:', len(objs), 'duplicate groups:', len(dups), dups[:5])
print('houses:', sorted(o['id'] for o in objs if o['id'].startswith('house_')))
