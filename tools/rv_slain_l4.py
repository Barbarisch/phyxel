"""rv_slain_l4.py - G-171 L4 on the SHIPPED build: do cleared enemies stay cleared?

User: "Wolf enemies respawn when coming back to the hallow farm". The farm's three wolves are
authored NPCs in the scene's `npcs` list, so re-entering the scene re-ran the list. Now a kill
is recorded in the profile's SlainLedger (keyed by scene + entity id) and the loader skips it.

Chain under test:
    kill (damage funnel -> death event -> ledger.record)  ->  leave the scene  ->  return
    ->  SceneManager names the scene for the loader  ->  loadNPCs skips the recorded ids

Farmhands must still spawn (the ledger is per-id, not per-scene wipe), and a CONTROL run of the
same sequence with no kills must bring all three wolves back - otherwise "absent" could just
mean the scene failed to spawn anything.
"""
import json
import subprocess
import time
import urllib.request
from pathlib import Path

REL = Path(r"C:/Users/bpete/Documents/PhyxelProjects/Ravenmere/build_probe/Release")
OUT = Path(r"C:/Users/bpete/Documents/GitHub/phyxel/docs/evidence/ravenmere")
PORT = 8125
B = 'http://127.0.0.1:%d' % PORT
WOLVES = ['npc_Wolf', 'npc_Wolf2', 'npc_AlphaWolf']
FARMERS = ['npc_Dunstan', 'npc_Oswin']


def safe(m, p, b=None, t=60):
    try:
        r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None,
                                   method=m, headers={'Content-Type': 'application/json'})
        return json.load(urllib.request.urlopen(r, timeout=t))
    except Exception as e:
        return {'err': str(e)[:200]}


def scene():
    return safe('GET', '/api/screen/state').get('scene_id')


def go(trigger_id, want_scene):
    safe('POST', '/api/rpg/fire_trigger', {'id': trigger_id})
    for _ in range(60):
        if scene() == want_scene:
            time.sleep(5)   # let the scene's NPCs land
            return True
        time.sleep(1)
    return False


def present(ids):
    out = {}
    for eid in ids:
        h = safe('POST', '/api/rpg/entity_health', {'id': eid})
        out[eid] = bool(h.get('has_health')) and bool(h.get('alive'))
    return out


def kill(eid):
    for _ in range(20):
        safe('POST', '/api/rpg/entity_damage', {'id': eid, 'amount': 100.0})
        time.sleep(0.4)
        h = safe('POST', '/api/rpg/entity_health', {'id': eid})
        if not h.get('alive'):
            return True
    return False


def run(tag, do_kill):
    log = open(OUT / ('rv_slain_l4_%s.log' % tag), 'w', encoding='utf-8', errors='replace')
    proc = subprocess.Popen([str(REL / 'Ravenmere.exe'), '--test', str(PORT)], cwd=str(REL),
                            stdout=log, stderr=subprocess.STDOUT)
    res = {'tag': tag, 'kill': do_kill}
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
        time.sleep(4)

        assert go('to_farm', 'farm'), 'never reached the farm'
        res['first_visit'] = present(WOLVES + FARMERS)
        print('[%s] first visit  : %s' % (tag, res['first_visit']), flush=True)
        if do_kill:
            res['killed'] = {w: kill(w) for w in WOLVES}
            print('[%s] killed       : %s' % (tag, res['killed']), flush=True)
            time.sleep(3)   # combat_victory -> wolves_dead -> save_game
        assert go('back_to_town', 'town'), 'never got back to town'
        assert go('to_farm', 'farm'), 'never returned to the farm'
        res['second_visit'] = present(WOLVES + FARMERS)
        print('[%s] second visit : %s' % (tag, res['second_visit']), flush=True)
    finally:
        proc.terminate()
        time.sleep(2)
        log.flush()
    return res


results = {'control': run('control', False), 'kill': run('kill', True)}
(OUT / 'rv_slain_l4.json').write_text(json.dumps(results, indent=2), encoding='utf-8')

c, k = results['control'], results['kill']
control_ok = all(c['second_visit'].get(w) for w in WOLVES)                # no kills -> all back
kill_ok = (all(k['first_visit'].get(w) for w in WOLVES)                   # they were there
           and all(k.get('killed', {}).get(w) for w in WOLVES)            # they died
           and not any(k['second_visit'].get(w) for w in WOLVES)          # they stayed dead
           and all(k['second_visit'].get(f) for f in FARMERS))            # farmers untouched
print('\nCONTROL (no kills, leave, return): wolves back = %s' % control_ok)
print('KILL    (kill, leave, return)   : wolves stayed dead = %s, farmers present = %s'
      % (not any(k['second_visit'].get(w) for w in WOLVES), all(k['second_visit'].get(f) for f in FARMERS)))
print('\nVERDICT: %s' % ('cleared enemies stay cleared on the shipped exe, and the control still respawns'
                         if control_ok and kill_ok else 'NOT PROVEN - see rv_slain_l4.json'), flush=True)
