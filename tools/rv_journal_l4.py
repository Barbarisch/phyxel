"""rv_journal_l4.py - G-168 L4 on the SHIPPED build: does L open a journal that shows the quests?

User: "if there are side quests, it would be nice to have a window to see them 'L' key maybe".
The objectives strip showed only active titles. The journal (ToggleJournal, L by default)
shows Main Quest / Side Quests / Completed with each objective's description.

Chain under test:  L (a named, rebindable action) -> GameShell::updateJournalToggle
                   -> journal.visible -> hud_journal -> repeater over journalHudRecords -> screen

Measured, not eyeballed: the journal's panel area is sampled for the HUD's panel colour while
closed, open, and closed again; then an objective is completed through the real host-action
dispatcher and the open journal is captured again so the Completed section can be checked.
"""
import json
import subprocess
import time
import urllib.request
from pathlib import Path

REL = Path(r"C:/Users/bpete/Documents/PhyxelProjects/Ravenmere/build_probe/Release")
OUT = Path(r"C:/Users/bpete/Documents/GitHub/phyxel/docs/evidence/ravenmere")
PORT = 8124
B = 'http://127.0.0.1:%d' % PORT
# hud_journal: Center, offset (0,-40), 760x460 on the 1280x720 canvas -> x 260..1020, y 90..550.
# Sample well inside so the border does not count.
PANEL_BOX = (300, 130, 980, 500)


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
    dst = OUT / ('rv_journal_l4_%s.png' % name)
    dst.write_bytes((REL / r['path']).read_bytes())
    return dst


def panel_fraction(path):
    from PIL import Image
    im = Image.open(path).convert('RGB').crop(PANEL_BOX)
    px = list(im.getdata())
    beige = sum(1 for r, g, b in px if r > 200 and g > 190 and b > 160)
    return round(100.0 * beige / len(px), 1)


def press_journal():
    r = safe('POST', '/api/rpg/inject_input', {'keys': ['ToggleJournal'], 'hold': 0.15})
    time.sleep(1.5)
    return r


log = open(OUT / 'rv_journal_l4.log', 'w', encoding='utf-8', errors='replace')
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

    p = shot('1_closed'); res['closed_pct'] = panel_fraction(p)
    res['press_1'] = press_journal()
    p = shot('2_open'); res['open_pct'] = panel_fraction(p)
    print('journal area panel-coloured: closed %.1f%%  ->  after L %.1f%%' % (res['closed_pct'], res['open_pct']), flush=True)

    # Complete the first main objective through the REAL dispatcher (the same path a trigger or
    # dialogue node uses); the journal is still open, so the Completed section should appear
    # and the reveal trigger should surface main_2.
    res['complete'] = safe('POST', '/api/rpg/host_action',
                           {'action': {'type': 'complete_objective', 'id': 'main_1'}})
    time.sleep(2.0)
    p = shot('3_open_after_complete'); res['open_after_complete_pct'] = panel_fraction(p)

    res['press_2'] = press_journal()
    p = shot('4_closed_again'); res['closed_again_pct'] = panel_fraction(p)
    print('after completing main_1: %.1f%%   after L again: %.1f%%' % (res['open_after_complete_pct'], res['closed_again_pct']), flush=True)
    res['injected'] = (res['press_1'].get('injected'), res['press_2'].get('injected'))
finally:
    proc.terminate()
    time.sleep(2)
    log.flush()

# The sampled region also holds the combat-hint and objectives panels, which are the same beige
# (~14% of the box while the journal is closed), so the verdict is a DELTA against that baseline.
base = res.get('closed_pct', 0.0)
ok = (res.get('open_pct', 0.0) - base > 60.0
      and abs(res.get('closed_again_pct', 100.0) - base) < 3.0)
res['open_minus_closed_pct'] = round(res.get('open_pct', 0.0) - base, 1)
(OUT / 'rv_journal_l4.json').write_text(json.dumps(res, indent=2), encoding='utf-8')
print('\nVERDICT: %s' % ('L opens and closes a journal on the shipped exe' if ok
                         else 'NOT PROVEN - see rv_journal_l4.json'), flush=True)
