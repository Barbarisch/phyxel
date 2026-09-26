"""V4b -- streaming-focus OWNERSHIP, live, on R-P2 (docs/PerfProgram2026-09.md section 16.4). L4.

Why V4 was redesigned: V4's control (stream_follow OFF) was resident at every chunk entry too, because in
the EDITOR the streaming anchor already follows the camera (Application.cpp:4245 sets
chunkManager->setPlayerPosition(camera->getPosition()) every frame). The design-check premise "a camera
path streams nothing without the focus override" came from ChunkManager's comment, not the host code,
and was false. What the override still owns is SHARING with WorldForge -- tested here against the real
second user (/api/worldforge/focus, holder "worldforge_focus"), each step's prediction in the table.
Usage: v4b_focus_ownership_live.py out.json"""
import json, sys, time

from rig_common import call, settle, wait_api

PATH = {'waypoints': [{'x': 40, 'y': 22, 'z': 40, 'yaw': 0, 'pitch': -10},
                      {'x': 120, 'y': 22, 'z': 40, 'yaw': 0, 'pitch': -10}],
        'speed_u_per_s': 8.0, 'loop': False}
steps = []


def step(name, predicted, fn):
    r = fn()
    ok = predicted(r)
    steps.append({'step': name, 'pass': bool(ok), 'response': r})
    print(('PASS ' if ok else 'FAIL ') + name)
    return r


wait_api()
settle()
step('worldforge_focus takes the focus', lambda r: r.get('success') and r.get('holder') == 'worldforge_focus',
     lambda: call('POST', '/api/worldforge/focus', {'x': 300, 'z': 40}))
step('path with stream_follow is REFUSED, naming the holder',
     lambda r: not r.get('success') and 'worldforge_focus' in r.get('error', '') and not r.get('playing'),
     lambda: call('POST', '/api/camera/path', dict(PATH, stream_follow=True)))
step('path WITHOUT stream_follow is allowed (it does not touch the focus)',
     lambda r: r.get('success') and r.get('focus_holder') == 'worldforge_focus',
     lambda: call('POST', '/api/camera/path', dict(PATH, stream_follow=False)))
step('stopping that path does not release the WorldForge focus',
     lambda r: r.get('success') and r.get('focus_released') is False and r.get('focus_holder') == 'worldforge_focus',
     lambda: call('POST', '/api/camera/path', {'stop': True}))
step('worldforge_focus releases its own hold', lambda r: r.get('released') is True and r.get('holder') == '',
     lambda: call('POST', '/api/worldforge/focus', {}))
step('path with stream_follow now takes the focus',
     lambda r: r.get('success') and r.get('playing') and r.get('stream_follow'),
     lambda: call('POST', '/api/camera/path', dict(PATH, stream_follow=True)))
time.sleep(0.5)
step('while the path holds it, the focus holder is camera_path',
     lambda r: r.get('focus_holder') == 'camera_path', lambda: call('GET', '/api/camera/path'))
step('worldforge_focus is REFUSED while the path holds it, naming camera_path',
     lambda r: not r.get('success') and r.get('holder') == 'camera_path',
     lambda: call('POST', '/api/worldforge/focus', {'x': 300, 'z': 40}))
step('an empty release by worldforge_focus does not clear the path\'s hold',
     lambda r: r.get('released') is False and r.get('holder') == 'camera_path',
     lambda: call('POST', '/api/worldforge/focus', {}))
step('stopping the path releases its focus',
     lambda r: r.get('success') and r.get('focus_released') is True and r.get('focus_holder') == '',
     lambda: call('POST', '/api/camera/path', {'stop': True}))
step('a camera set while a path plays stops it and says so',
     lambda r: r.get('path_stopped') is True,
     lambda: (call('POST', '/api/camera/path', dict(PATH, stream_follow=True)),
              call('POST', '/api/camera', {'mode': 'free', 'position': {'x': 16, 'y': 24, 'z': 44},
                                           'yaw': -90, 'pitch': -20}))[1])
step('...and the path\'s focus hold is released with it',
     lambda r: r.get('focus_holder') == '' and not r.get('playing'), lambda: call('GET', '/api/camera/path'))
ok = all(s['pass'] for s in steps)
json.dump({'rig': 'R-P2 (PerfRigStream)', 'steps': steps, 'pass': ok}, open(sys.argv[1], 'w'), indent=1)
print('V4b', 'PASS' if ok else 'FAIL')
sys.exit(0 if ok else 1)
