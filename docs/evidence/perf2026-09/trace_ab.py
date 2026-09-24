import json, time, urllib.request, statistics
from firstlook import call, sample, pose
out=open('trace_ab_4090.jsonl','a')
for name,p in {'exterior':[8,30,40,-90,-20],'interior':[12.5,18.4,3.5,180,-8]}.items():
    pose(p)
    print('==',name)
    for trace in (1,0,1,0):
        call('GET','/api/debug/light_occupancy?trace=%d'%trace, t=40)
        time.sleep(1.0)
        r=sample(20); r.update(pose=name,trace=trace); out.write(json.dumps(r)+'\n'); out.flush()
        print(' trace=%d static %.2f grass %.2f foliage %.2f gi %.2f scene %.2f fps %.1f'%(trace,r['static'],r['grass'],r['foliage'],r['gi'],r['scene'],r['fps']),flush=True)
call('GET','/api/debug/light_occupancy?trace=1', t=40)
