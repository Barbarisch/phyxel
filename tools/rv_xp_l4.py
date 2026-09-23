"""rv_xp_l4.py - G-172 L4 on the SHIPPED build: does a kill move the XP bar the player sees?

The unit tests pin the arithmetic. This proves the whole chain on the real exe:
    kill an enemy -> death event -> grantXP -> CharacterProgression::awardXP -> the sheet
                  -> GameShell's HUD providers -> the bar and label on screen

Town NPCs are m_invulnerable (you cannot murder the quest-givers), so a kill test has to happen
where the hostiles are: Hollin Farm. The probe transitions there, kills a wolf through the damage
FUNNEL - the path that raises the death event the kill hook listens to - and reads the sheet and
the screen either side.

Captures BOTH poses so the bar can be compared by eye, not just by number.
"""
import json
import subprocess
import time
import urllib.request
from pathlib import Path

REL = Path(r"C:/Users/bpete/Documents/PhyxelProjects/Ravenmere/build_probe/Release")
OUT = Path(r"C:/Users/bpete/Documents/GitHub/phyxel/docs/evidence/ravenmere")
PORT = 8121
B = 'http://127.0.0.1:%d' % PORT


def safe(m, p, b=None, t=60):
    try:
        r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None,
                                   method=m, headers={'Content-Type': 'application/json'})
        return json.load(urllib.request.urlopen(r, timeout=t))
    except Exception as e:
        return {'err': str(e)[:200]}


def sheet():
    s = (safe('GET', '/api/rpg/sheet') or {}).get('sheet') or {}
    return s.get('experiencePoints'), s


def shot(name):
    r = safe('GET', '/api/screenshot')
    if not r.get('path'):
        return None
    dst = OUT / ('rv_xp_l4_%s.png' % name)
    dst.write_bytes((REL / r['path']).read_bytes())
    return dst.name


log = open(OUT / 'rv_xp_l4.log', 'w', encoding='utf-8', errors='replace')
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

    xp0, sh0 = sheet()
    res['xp_start'] = xp0
    res['shot_town'] = shot('1_town')
    print('town: XP=%s  capture=%s' % (xp0, res['shot_town']), flush=True)

    # To the farm, where the wolves are.
    res['triggers_town'] = [t.get('id') for t in (safe('GET', '/api/rpg/list_triggers') or {}).get('triggers', [])]
    safe('POST', '/api/rpg/fire_trigger', {'id': 'to_farm'})
    for _ in range(40):
        sc = safe('GET', '/api/screen/state')
        if sc.get('scene_id') and sc.get('scene_id') != 'town':
            break
        time.sleep(1)
    time.sleep(6)
    res['scene'] = safe('GET', '/api/screen/state').get('scene_id')
    print('scene now: %s' % res['scene'], flush=True)

    # Find a DAMAGEABLE enemy: town NPCs are invulnerable, so probe by actually hitting for 0
    # and checking the health moved. A victim chosen by "has health" alone is how the first
    # attempt at this silently tested nothing.
    st = safe('GET', '/api/state')
    victim, before_hp = None, None
    for e in st.get('entities', []):
        eid = e.get('id', '')
        if not eid or eid == 'player':
            continue
        h = safe('POST', '/api/rpg/entity_health', {'id': eid})
        if not (h.get('has_health') and h.get('alive')):
            continue
        probe = safe('POST', '/api/rpg/entity_damage', {'id': eid, 'amount': 1.0})
        h2 = safe('POST', '/api/rpg/entity_health', {'id': eid})
        if (h2.get('health') or 0) < (h.get('health') or 0):
            victim, before_hp = eid, h.get('health')
            break
    res['victim'] = victim
    res['victim_hp'] = before_hp
    assert victim, 'no DAMAGEABLE entity in scene %s' % res['scene']
    print('victim = %s (hp %s)' % (victim, before_hp), flush=True)

    for _ in range(15):
        safe('POST', '/api/rpg/entity_damage', {'id': victim, 'amount': 100.0})
        time.sleep(0.5)
        h = safe('POST', '/api/rpg/entity_health', {'id': victim})
        if not h.get('alive') or (h.get('health') or 0) <= 0:
            break
    time.sleep(2.5)
    hf = safe('POST', '/api/rpg/entity_health', {'id': victim})
    res['victim_alive_after'] = hf.get('alive')
    res['victim_hp_after'] = hf.get('health')

    xp1, sh1 = sheet()
    res['xp_after_kill'] = xp1
    res['kill_award'] = (xp1 or 0) - (xp0 or 0)
    res['shot_after'] = shot('2_after_kill')
    print('after kill: alive=%s hp=%s  XP=%s (delta %s)  capture=%s'
          % (res['victim_alive_after'], res['victim_hp_after'], xp1,
             res['kill_award'], res['shot_after']), flush=True)
finally:
    proc.terminate()
    time.sleep(2)
    log.flush()

(OUT / 'rv_xp_l4.json').write_text(json.dumps(res, indent=2), encoding='utf-8')
ok = res.get('victim_alive_after') is False and (res.get('kill_award') or 0) > 0
print('\nVERDICT: %s' % ('kill -> XP works end to end on the shipped exe' if ok else
                         'NOT PROVEN - see rv_xp_l4.json (a kill that did not happen proves nothing)'),
      flush=True)
