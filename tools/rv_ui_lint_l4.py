"""rv_ui_lint_l4.py - measure HUD overlap/cut-off on the SHIPPED build across the screens a player sees.

User (2026-09-24): "ui elements overlapping all the time". The test API's ui_lint reports every
child a visible HUD panel would cut off and every pair of visible panels that overlap. This runs
it at: town (playing), journal open (L), character sheet (C), pause menu, settings, keybindings,
and the farm, and writes rv_ui_lint_l4.json with the defect lists per screen.
Usage: python tools/rv_ui_lint_l4.py [label]
"""
import json
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

REL = Path(r"C:/Users/bpete/Documents/PhyxelProjects/Ravenmere/build_probe/Release")
OUT = Path(r"C:/Users/bpete/Documents/GitHub/phyxel/docs/evidence/ravenmere")
PORT = 8131
B = 'http://127.0.0.1:%d' % PORT
LABEL = sys.argv[1] if len(sys.argv) > 1 else 'run'


def safe(m, p, b=None, t=60):
    try:
        r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None,
                                   method=m, headers={'Content-Type': 'application/json'})
        return json.load(urllib.request.urlopen(r, timeout=t))
    except Exception as e:
        return {'err': str(e)[:200]}


def key(name, hold=0.15):
    safe('POST', '/api/rpg/inject_input', {'keys': [name], 'hold': hold})
    time.sleep(0.8)


def click(x, y):
    safe('POST', '/api/ui/click', {'x': x, 'y': y})
    time.sleep(0.8)


def lint(name, results):
    r = safe('POST', '/api/rpg/ui_lint', {})
    st = safe('GET', '/api/screen/state')
    d = r.get('defects', [])
    results.append({'screen': name, 'state': st.get('screen'), 'count': r.get('count', len(d)), 'defects': d,
                    'visible': sorted(m for m in (st.get('visible_menus') or []) if isinstance(m, str))})
    print('  %-28s state=%-9s defects=%s' % (name, st.get('screen'), r.get('count', len(d))), flush=True)
    for x in d[:8]:
        print('      ' + json.dumps(x)[:150], flush=True)


log = open(OUT / ('rv_ui_lint_l4_%s.log' % LABEL), 'w', encoding='utf-8', errors='replace')
proc = subprocess.Popen([str(REL / 'Ravenmere.exe'), '--test', str(PORT)], cwd=str(REL),
                        stdout=log, stderr=subprocess.STDOUT)
results = []
try:
    for _ in range(180):
        if 'err' not in safe('GET', '/api/state', t=3):
            break
        time.sleep(1)
    lint('main menu', results)
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
    time.sleep(4)
    lint('town playing', results)
    # G-184: five quests in the journal (the shipped start shows one) - the multi-row case the
    # journal's quest rows had failed (fixed 108 px rows under 134 px of wrapped text).
    for oid in ('main_2', 'side_rats', 'side_poacher', 'side_wolves', 'side_hermit'):
        safe('POST', '/api/rpg/host_action', {'action': {'type': 'reveal_objective', 'id': oid}})
    time.sleep(0.5)
    key('ToggleJournal');   lint('journal open (5 quests)', results);   key('ToggleJournal')
    # dialogue: start_dialogue runs the shell's NPC interact for a named NPC (a harness cannot
    # reliably stand in front of one and press Interact - the first runs measured nothing here)
    sd = safe('POST', '/api/rpg/start_dialogue', {'npc': 'Wynn'})
    time.sleep(1.5)
    print('  start_dialogue -> %s' % json.dumps(sd)[:120], flush=True)
    lint('dialogue open' if sd.get('dialogue_active') else 'dialogue open (NOT OPEN)', results)
    shot = safe('GET', '/api/screenshot')
    if shot.get('path'): (OUT / ('rv_ui_lint_l4_%s_dialogue.png' % LABEL)).write_bytes((REL / shot['path']).read_bytes())
    key('Escape'); time.sleep(0.5)
    key('ToggleCharacter'); lint('character sheet', results); key('Escape')
    key('Escape');          lint('pause menu', results)
    click(640, 384);        lint('settings main', results)
    click(900, 241);        lint('keybindings panel', results)
    key('Escape'); key('Escape'); key('Escape')
    safe('POST', '/api/rpg/fire_trigger', {'id': 'to_farm'})
    for _ in range(60):
        if safe('GET', '/api/screen/state').get('scene_id') == 'farm':
            break
        time.sleep(1)
    time.sleep(6)
    lint('farm playing', results)
    # combat: the wolves' encounter trigger starts a fight
    safe('POST', '/api/rpg/fire_trigger', {'id': 'encounter_wolves'})
    time.sleep(3)
    lint('combat (farm wolves)', results)
    shot = safe('GET', '/api/screenshot')
    if shot.get('path'): (OUT / ('rv_ui_lint_l4_%s_combat.png' % LABEL)).write_bytes((REL / shot['path']).read_bytes())
    # G-132: the window resized to the desktop (1536x960 on this laptop) - does the HUD follow?
    safe('POST', '/api/rpg/window_resize', {'w': 1536, 'h': 960})
    time.sleep(2.5)
    lint('combat @1536x960', results)
    shot = safe('GET', '/api/screenshot')
    if shot.get('path'): (OUT / ('rv_ui_lint_l4_%s_combat_wide.png' % LABEL)).write_bytes((REL / shot['path']).read_bytes())
    key('ToggleJournal'); lint('journal @1536x960 (5 quests)', results)
    shot = safe('GET', '/api/screenshot')
    if shot.get('path'): (OUT / ('rv_ui_lint_l4_%s_journal_wide.png' % LABEL)).write_bytes((REL / shot['path']).read_bytes())
    key('ToggleJournal')
finally:
    proc.terminate()
    time.sleep(2)
    log.flush()
(OUT / ('rv_ui_lint_l4_%s.json' % LABEL)).write_text(json.dumps(results, indent=2), encoding='utf-8')
total = sum(r['count'] for r in results)
print('\nTOTAL defects across %d screens: %d' % (len(results), total), flush=True)
