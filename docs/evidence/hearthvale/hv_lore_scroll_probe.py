"""Scrollable-panel verification probe (Hearthvale Lore submenu).

Red baseline captured earlier this arc: text past a panel's bottom edge was
simply CLIPPED with no way to reach it (PresentationPolish.md #6). Green =
POST /api/rpg/ui_scroll over the lore panel shifts its content (pixel diff
inside the panel region between two captures) and the scroll is 'consumed'.

Steps:
  1. main-menu screenshot (Lore button present)
  2. ui_click Lore (640,444) -> submenu; screenshot A (lore top)
  3. ui_scroll {x:640,y:320,delta:-3} -> consumed must be true
  4. screenshot B; numeric pixel diff of panel region (340,160)-(940,480)
  5. control: diff a region OUTSIDE the panel (title strip) ~ unchanged
  6. ui_scroll delta:+10 (scroll back to top) -> screenshot C ~= A
  7. ui_click Back (640,544) -> screen back to main menu
"""
import json, sys, time, urllib.request

BASE = "http://127.0.0.1:8101"

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
            s = api("GET", "/api/state")
            if s:
                return s
        except Exception:
            pass
        time.sleep(1.5)
    raise SystemExit("engine never became responsive")

def shot(tag):
    r = api("POST", "/api/rpg/capture_screenshot", {})
    if not r.get("success"):
        raise SystemExit(f"screenshot {tag} failed: {r}")
    print(f"  [shot] {tag}: {r['path']} ({r['width']}x{r['height']})")
    return r["path"]

def region_diff(png_a, png_b, x0, y0, x1, y1):
    """Mean abs channel diff + changed-pixel count inside a rect."""
    from PIL import Image
    a = Image.open(png_a).convert("RGB")
    b = Image.open(png_b).convert("RGB")
    ca, cb = a.crop((x0, y0, x1, y1)), b.crop((x0, y0, x1, y1))
    pa, pb = ca.tobytes(), cb.tobytes()
    n = len(pa)
    total = 0
    changed_px = 0
    w = x1 - x0
    for i in range(0, n, 3):
        d = abs(pa[i]-pb[i]) + abs(pa[i+1]-pb[i+1]) + abs(pa[i+2]-pb[i+2])
        total += d
        if d > 24:
            changed_px += 1
    return total / (n // 3), changed_px

def main():
    wait_ready()
    time.sleep(2.0)  # let the menu scene + camera path settle

    scr = api("POST", "/api/rpg/get_screen_state", {})
    print(f"screen: {scr.get('screen')} menus={scr.get('visible_menus')}")

    p_menu = shot("main_menu")

    r = api("POST", "/api/rpg/ui_click", {"x": 640, "y": 444})
    print(f"click Lore -> consumed={r.get('consumed')}")
    if not r.get("consumed"):
        raise SystemExit("FAIL: Lore button click not consumed")
    time.sleep(0.8)
    p_a = shot("lore_top")

    # Noise floor: the menuWorld orbit camera animates the background, so
    # capture a second frame with NO input and measure how much the panel
    # region changes on its own. The scroll diff must beat this clearly.
    time.sleep(0.6)
    p_a2 = shot("lore_top_2")
    mean_nf, chg_nf = region_diff(p_a, p_a2, 340, 160, 940, 480)
    print(f"noise floor:  mean={mean_nf:.3f} changed_px={chg_nf}")

    r = api("POST", "/api/rpg/ui_scroll", {"x": 640, "y": 320, "delta": -3})
    print(f"ui_scroll -3 -> ok={r.get('ok')} consumed={r.get('consumed')}")
    if not r.get("consumed"):
        raise SystemExit("FAIL: scroll not consumed by any scrollable panel")
    time.sleep(0.6)
    p_b = shot("lore_scrolled")

    # panel rect authored at (340,160) size (600,320)
    mean_in, chg_in = region_diff(p_a, p_b, 340, 160, 940, 480)
    # control strip: the title band above the panel, should be ~static
    mean_out, chg_out = region_diff(p_a, p_b, 340, 40, 940, 120)
    print(f"panel diff:   mean={mean_in:.3f} changed_px={chg_in}")
    print(f"control diff: mean={mean_out:.3f} changed_px={chg_out}")

    r = api("POST", "/api/rpg/ui_scroll", {"x": 640, "y": 320, "delta": 10})
    time.sleep(0.6)
    p_c = shot("lore_back_to_top")
    mean_rt, chg_rt = region_diff(p_a, p_c, 340, 160, 940, 480)
    print(f"round-trip diff vs A: mean={mean_rt:.3f} changed_px={chg_rt}")

    r = api("POST", "/api/rpg/ui_click", {"x": 640, "y": 544})
    print(f"click Back -> consumed={r.get('consumed')}")
    time.sleep(0.5)
    scr = api("POST", "/api/rpg/get_screen_state", {})
    print(f"screen after Back: menus={scr.get('visible_menus')}")

    verdict = []
    verdict.append(("scroll shifted panel content", chg_in > 2000))
    verdict.append(("scroll diff beats background noise 3x", chg_in > 3 * max(chg_nf, 1)))
    verdict.append(("scroll-up returns near noise floor", chg_rt < max(3 * chg_nf, chg_in / 4)))
    ok = all(v for _, v in verdict)
    for name, v in verdict:
        print(f"  {'PASS' if v else 'FAIL'}: {name}")
    print("VERDICT:", "PASS" if ok else "FAIL")
    print("SHOTS:", json.dumps({"menu": p_menu, "a": p_a, "b": p_b, "c": p_c}))
    sys.exit(0 if ok else 1)

main()
