"""rv_party_l4.py - G-161 L4 on the SHIPPED build: does recruiting a companion show up?

The recruit machinery already worked - a `join_party` dialogue action adds to Core::Party, the
shell respawns absent members, and start_combat enlists them. What did not exist was any way to
SEE it, so a successful recruit looked exactly like a failed one. That is what the user hit
after passing the DC 12 check with Aldric.

This drives the REAL consequence, not a shortcut: `host_action` runs the very same
TriggerSystem::executeHostAction dispatcher a dialogue node uses, with the same payload the
authored Bram node carries. So the chain under test is

    join_party action -> Core::Party -> GameShell's party provider -> repeater -> screen

Captures before and after, because "the panel is there" is a visual claim.
"""
import json
import subprocess
import time
import urllib.request
from pathlib import Path

REL = Path(r"C:/Users/bpete/Documents/PhyxelProjects/Ravenmere/build_probe/Release")
OUT = Path(r"C:/Users/bpete/Documents/GitHub/phyxel/docs/evidence/ravenmere")
PORT = 8122
B = 'http://127.0.0.1:%d' % PORT


def safe(m, p, b=None, t=60):
    try:
        r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None,
                                   method=m, headers={'Content-Type': 'application/json'})
        return json.load(urllib.request.urlopen(r, timeout=t))
    except Exception as e:
        return {'err': str(e)[:200]}


def shot(name):
    r = safe('GET', '/api/screenshot')
    if not r.get('path'):
        return None
    dst = OUT / ('rv_party_l4_%s.png' % name)
    dst.write_bytes((REL / r['path']).read_bytes())
    return dst.name


log = open(OUT / 'rv_party_l4.log', 'w', encoding='utf-8', errors='replace')
proc = subprocess.Popen([str(REL / 'Ravenmere.exe'), '--test', str(PORT)], cwd=str(REL),
                        stdout=log, stderr=subprocess.STDOUT)
res = {'exe_mtime': time.strftime('%Y-%m-%d %H:%M:%S',
                                  time.localtime((REL / 'Ravenmere.exe').stat().st_mtime))}
try:
    for _ in range(180):
        if 'err' not in safe('GET', '/api/state', t=3):
            break
        time.sleep(1)
    reached = False
    for _a in range(6):
        safe('POST', '/api/ui/click', {'x': 640, 'y': 374})
        for _ack in range(12):   # G-116: the intro card gates the first scene - Continue sits at (640, 628)
            if safe('GET', '/api/screen/state').get('screen') == 'narrative':
                safe('POST', '/api/ui/click', {'x': 640, 'y': 628}); break
            time.sleep(0.25)
        for _ in range(40):
            sc = safe('GET', '/api/screen/state')
            if sc.get('scene_id') == 'town' and sc.get('screen') == 'playing':
                reached = True
                break
            time.sleep(1)
        if reached:
            break
    assert reached, 'never reached the town'
    time.sleep(5)

    res['shot_alone'] = shot('1_alone')
    print('travelling alone: capture=%s' % res['shot_alone'], flush=True)

    # The exact payload the authored Bram dialogue node carries.
    got = safe('POST', '/api/rpg/host_action',
               {'action': {'type': 'join_party', 'entity_id': 'npc_Bram', 'name': 'Bram'}})
    res['join_result'] = got
    print('join_party -> %s' % json.dumps(got)[:120], flush=True)
    time.sleep(3)
    res['shot_party'] = shot('2_with_bram')

    # A second companion, to prove the repeater grows rather than showing one fixed row.
    safe('POST', '/api/rpg/host_action',
         {'action': {'type': 'join_party', 'entity_id': 'npc_Wren', 'name': 'Wren'}})
    time.sleep(3)
    res['shot_two'] = shot('3_with_two')
    print('captures: %s / %s' % (res['shot_party'], res['shot_two']), flush=True)

    # Hurt one of them: the bar must track the LIVE entity, not a stored number.
    hb = safe('POST', '/api/rpg/entity_health', {'id': 'npc_Bram'})
    res['bram_health_before'] = hb
    if hb.get('has_health'):
        safe('POST', '/api/rpg/entity_damage', {'id': 'npc_Bram', 'amount': 6.0})
        time.sleep(2)
        res['bram_health_after'] = safe('POST', '/api/rpg/entity_health', {'id': 'npc_Bram'})
        res['shot_hurt'] = shot('4_bram_hurt')
        print('bram %s -> %s  capture=%s'
              % (hb.get('health'), res['bram_health_after'].get('health'), res['shot_hurt']),
              flush=True)
finally:
    proc.terminate()
    time.sleep(2)
    log.flush()

(OUT / 'rv_party_l4.json').write_text(json.dumps(res, indent=2), encoding='utf-8')
print('wrote %s' % (OUT / 'rv_party_l4.json'), flush=True)
