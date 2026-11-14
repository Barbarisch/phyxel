import json, sys
sys.argv = ['x', r'C:\Users\jack\AppData\Local\Temp\claude\88187f369423edff\e326662c-cb88-4b3e-8111-42d79f622943\scratchpad\f29_poses.json']
exec(open(r'C:\Users\jack\AppData\Local\Temp\claude\88187f369423edff\e326662c-cb88-4b3e-8111-42d79f622943\scratchpad\f214c_gate.py').read().split('poses = json.load')[0])
poses = json.load(open(sys.argv[1]))
call('POST', '/api/debug/foliage', {'windStrength': 0.0}); call('POST', '/api/debug/gi', {'enabled': False})
x, y, z, yaw, pitch = poses['top_down']
call('POST', '/api/camera', {'mode': 'free', 'position': {'x': x, 'y': y, 'z': z}, 'yaw': yaw, 'pitch': pitch})
res = {}
for cast in (True, False):
    call('POST', '/api/debug/voxfound_preview', {'cast': {'sprigs': cast, 'faces': cast}})
    call('POST', '/api/debug/voxfound_preview', {'lod_auto': True, 'lod_px': 8, 'lod_sprig': False}); A = shot('f214c_diag_A_cast%d.png' % cast, 4)
    call('POST', '/api/debug/voxfound_preview', {'lod_sprig': True}); B = shot('f214c_diag_B_cast%d.png' % cast, 4)
    res['casters_on' if cast else 'casters_off'] = {'luma_A': round(float(A.mean()),2), 'luma_B': round(float(B.mean()),2), 'changed_pct': round(changed(B, A),2)}
call('POST', '/api/debug/voxfound_preview', {'cast': {'sprigs': True, 'faces': True}, 'lod_auto': False})
call('POST', '/api/debug/foliage', {'windStrength': 0.05}); call('POST', '/api/debug/gi', {'enabled': True})
print(json.dumps(res))
