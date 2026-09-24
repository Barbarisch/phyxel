"""rv_settings_l4.py - G-179 L4 on the SHIPPED build: ESC leaves Settings and the keybindings panel.

User (2026-09-24 manual play): "cant exit key bindings even after hitting escape a bunch of times".
The keybindings sub-panel had no Back button and the shell's ESC handler did not know the
Settings state.

Walk: Playing -> ESC -> Paused -> click Settings -> Settings (main) -> click Keybindings...
-> keybindings sub-panel visible -> ESC -> main visible, still Settings -> ESC -> Paused -> ESC
-> Playing. Then the rebind case: Settings -> Keybindings -> click a key button (capture) ->
ESC -> capture cancelled AND still on the keybindings panel (that ESC belonged to the capture)
-> ESC -> main. Observables: /api/screen/state {screen, visible_menus}. Layout coordinates from
resources/ui (pause Settings button 540,360 200x48; Keybindings... 700,224 400x34; kb_Jump
410,362 190x34; both Back buttons at 540,650/690).
Usage: python tools/rv_settings_l4.py [label]
"""
import json
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

REL = Path(r"C:/Users/bpete/Documents/PhyxelProjects/Ravenmere/build_probe/Release")
OUT = Path(r"C:/Users/bpete/Documents/GitHub/phyxel/docs/evidence/ravenmere")
PORT = 8130
B = 'http://127.0.0.1:%d' % PORT
LABEL = sys.argv[1] if len(sys.argv) > 1 else 'run'


def safe(m, p, b=None, t=60):
    try:
        r = urllib.request.Request(B + p, data=json.dumps(b).encode() if b is not None else None,
                                   method=m, headers={'Content-Type': 'application/json'})
        return json.load(urllib.request.urlopen(r, timeout=t))
    except Exception as e:
        return {'err': str(e)[:200]}


def state():
    s = safe('GET', '/api/screen/state')
    return s.get('screen'), sorted(m for m in (s.get('visible_menus') or []) if isinstance(m, str))


def esc():
    safe('POST', '/api/rpg/inject_input', {'keys': ['Escape'], 'hold': 0.15})
    time.sleep(0.8)


def click(x, y):
    safe('POST', '/api/ui/click', {'x': x, 'y': y})
    time.sleep(0.8)


steps = []


def record(name, expect_screen=None, expect_visible=None, expect_hidden=None):
    sc, vis = state()
    ok = True
    if expect_screen is not None and sc != expect_screen: ok = False
    for v in (expect_visible or []):
        if v not in vis: ok = False
    for h in (expect_hidden or []):
        if h in vis: ok = False
    steps.append({'step': name, 'screen': sc, 'visible': vis, 'ok': ok})
    print('  %-42s screen=%-9s visible=%s  %s' % (name, sc, [v for v in vis if v.startswith('settings') or v.startswith('pause')], 'ok' if ok else 'FAIL'), flush=True)
    return ok


log = open(OUT / ('rv_settings_l4_%s.log' % LABEL), 'w', encoding='utf-8', errors='replace')
proc = subprocess.Popen([str(REL / 'Ravenmere.exe'), '--test', str(PORT)], cwd=str(REL),
                        stdout=log, stderr=subprocess.STDOUT)
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
    time.sleep(4)

    print('--- menu walk', flush=True)
    record('start: playing', 'playing')
    esc();               record('ESC -> paused', 'paused')
    click(640, 384);     record('click Settings -> settings main', 'settings', ['settings:main'])
    click(900, 241);     record('click Keybindings... -> sub-panel', 'settings', ['settings:keybindings'], ['settings:main'])
    esc();               record('ESC -> back to settings main', 'settings', ['settings:main'], ['settings:keybindings'])
    esc();               record('ESC -> paused', 'paused')
    esc();               record('ESC -> playing', 'playing')

    print('--- rebind cancel keeps you on the panel', flush=True)
    esc();               record('ESC -> paused (again)', 'paused')
    click(640, 384);     record('Settings', 'settings', ['settings:main'])
    click(900, 241);     record('Keybindings...', 'settings', ['settings:keybindings'])
    click(505, 379);     record('click kb_Jump (capture armed)', 'settings', ['settings:keybindings'])
    esc();               record('ESC cancels the capture, STAYS on the panel', 'settings', ['settings:keybindings'], ['settings:main'])
    esc();               record('ESC -> settings main', 'settings', ['settings:main'], ['settings:keybindings'])

    print('--- the Back button on the panel', flush=True)
    click(900, 241);     record('Keybindings...', 'settings', ['settings:keybindings'])
    click(640, 707);     record('click Back on the panel -> main', 'settings', ['settings:main'], ['settings:keybindings'])
    click(640, 674);     record('click Back on settings -> paused', 'paused')
finally:
    proc.terminate()
    time.sleep(2)
    log.flush()

(OUT / ('rv_settings_l4_%s.json' % LABEL)).write_text(json.dumps(steps, indent=2), encoding='utf-8')
fails = [s for s in steps if not s['ok']]
print('\nVERDICT: %s' % ('ESC and Back leave the keybindings panel and Settings at every level (%d steps)' % len(steps)
                         if not fails else 'NOT PROVEN - %d step(s) failed: %s' % (len(fails), [f['step'] for f in fails])), flush=True)
