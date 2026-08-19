"""Theme + appear-animation verification probe (Hearthvale main menu).

RED baselines (captured this morning, pre-change, archived):
  - docs/evidence/hearthvale/lore_scroll_top.png era shots: main menu buttons are
    slate GREY (buttonBg 0.25,0.25,0.30 -> R ~= G < B): the warm-button check
    FAILS on shot_1787151745775.png.
  - lore_top vs lore_top_2 (1.1s apart, 0 changed px): an opened screen rendered
    its FULL settled state from the first frame - no appear animation existed.

GREEN (this build):
  T1 theme: Begin button center pixel is warm (R > B by a wide margin) and the
     SAME pixel on the archived red PNG fails the same check.
  T2 anim: open Lore -> capture immediately vs after 1.5s: panel region differs
     (mid slide/fade), and a REOPEN replays it (second immediate capture also
     differs from settled).
  T3 settled == stable: two captures 1s apart after settling are ~identical.
"""
import json, sys, time, urllib.request

BASE = "http://127.0.0.1:8101"
RED_MENU_PNG = r"C:\Users\bpete\Documents\PhyxelProjects\Hearthvale\build\Release\screenshots\shot_1787151745775.png"

def api(method, path, body=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(BASE + path, data=data, method=method,
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=30) as resp:
        return json.loads(resp.read().decode())

def wait_ready(timeout=90):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            if api("GET", "/api/state"):
                return
        except Exception:
            pass
        time.sleep(1.5)
    raise SystemExit("engine never became responsive")

def shot(tag):
    r = api("POST", "/api/rpg/capture_screenshot", {})
    if not r.get("success"):
        raise SystemExit(f"screenshot {tag} failed: {r}")
    print(f"  [shot] {tag}: {r['path']}")
    return r["path"]

def px(png, x, y):
    from PIL import Image
    return Image.open(png).convert("RGB").getpixel((x, y))

def region_changed(png_a, png_b, x0, y0, x1, y1, thresh=24):
    from PIL import Image
    a = Image.open(png_a).convert("RGB").crop((x0, y0, x1, y1)).tobytes()
    b = Image.open(png_b).convert("RGB").crop((x0, y0, x1, y1)).tobytes()
    changed = 0
    for i in range(0, len(a), 3):
        if abs(a[i]-b[i]) + abs(a[i+1]-b[i+1]) + abs(a[i+2]-b[i+2]) > thresh:
            changed += 1
    return changed

def main():
    wait_ready()
    time.sleep(2.5)  # menu + camera path settle; main-menu anims (<=1s) settled too

    results = []

    # ── T1: theme ──
    p_menu = shot("main_menu_ember")
    r, g, b = px(p_menu, 640, 384)      # Begin button center
    rr, rg, rb = px(RED_MENU_PNG, 640, 384)
    print(f"button px now  R{r} G{g} B{b}  |  red-baseline R{rr} G{rg} B{rb}")
    results.append(("T1a ember button is warm (R > B+40)", r > b + 40))
    results.append(("T1b red baseline FAILS same check", not (rr > rb + 40)))

    # ── T2: appear animation on submenu open ──
    r = api("POST", "/api/rpg/ui_click", {"x": 640, "y": 444})   # Lore
    if not r.get("consumed"):
        raise SystemExit("FAIL: Lore click not consumed")
    p_early = shot("lore_early")        # immediately - mid slide/fade
    time.sleep(1.6)
    p_set = shot("lore_settled")
    chg_anim = region_changed(p_early, p_set, 340, 160, 940, 500)
    print(f"early vs settled changed px: {chg_anim}")
    results.append(("T2a submenu open animates (early != settled)", chg_anim > 5000))

    # settled stability (the old red pair was 0 px; background may add noise)
    time.sleep(1.0)
    p_set2 = shot("lore_settled_2")
    chg_stable = region_changed(p_set, p_set2, 340, 160, 940, 500)
    print(f"settled vs settled+1s changed px: {chg_stable}")
    results.append(("T3 settled state is stable", chg_stable < chg_anim / 4))

    # reopen replays the animation
    api("POST", "/api/rpg/ui_click", {"x": 640, "y": 544})       # Back
    time.sleep(0.7)
    api("POST", "/api/rpg/ui_click", {"x": 640, "y": 444})       # Lore again
    p_early2 = shot("lore_early_reopen")
    time.sleep(1.6)
    p_set3 = shot("lore_settled_reopen")
    chg_replay = region_changed(p_early2, p_set3, 340, 160, 940, 500)
    print(f"reopen early vs settled changed px: {chg_replay}")
    results.append(("T2b reopening replays the animation", chg_replay > 5000))

    ok = all(v for _, v in results)
    for name, v in results:
        print(f"  {'PASS' if v else 'FAIL'}: {name}")
    print("VERDICT:", "PASS" if ok else "FAIL")
    print("SHOTS:", json.dumps({"menu": p_menu, "early": p_early, "settled": p_set,
                                "early2": p_early2}))
    sys.exit(0 if ok else 1)

main()
