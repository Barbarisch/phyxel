"""Cursor-grab probe: does the --test game grab/confine the OS mouse?

Samples, while the packaged game is running (screen=playing) and again after a
HARD kill (the worst case for lingering state):
  - GetClipCursor rect vs the full virtual-screen rect (clipped => grabbed)
  - GetCursorInfo CURSOR_SHOWING flag (hidden => grabbed)
  - foreground-window title (context: grab only engages while game focused)

Usage: python cursor_probe.py <label>   (label = "red" | "green")
Appends one record to cursor_probe_evidence.json.
"""
import ctypes
import ctypes.wintypes as wt
import json
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

LABEL = sys.argv[1] if len(sys.argv) > 1 else "unlabeled"
PORT = 8100
BASE = f"http://127.0.0.1:{PORT}"
RELDIR = Path.home() / "Documents" / "PhyxelProjects" / "RpgGapProbe" / "build" / "Release"
EXE = RELDIR / "RpgGapProbe.exe"
SCRATCH = Path(__file__).parent
EVIDENCE = SCRATCH / "cursor_probe_evidence.json"

u32 = ctypes.windll.user32


class CURSORINFO(ctypes.Structure):
    _fields_ = [("cbSize", wt.DWORD), ("flags", wt.DWORD),
                ("hCursor", ctypes.c_void_p), ("ptScreenPos", wt.POINT)]


def sample(tag):
    clip = wt.RECT()
    u32.GetClipCursor(ctypes.byref(clip))
    full = (u32.GetSystemMetrics(76), u32.GetSystemMetrics(77),          # SM_XVIRTUALSCREEN/Y
            u32.GetSystemMetrics(76) + u32.GetSystemMetrics(78),         # + SM_CXVIRTUALSCREEN
            u32.GetSystemMetrics(77) + u32.GetSystemMetrics(79))
    ci = CURSORINFO(); ci.cbSize = ctypes.sizeof(CURSORINFO)
    u32.GetCursorInfo(ctypes.byref(ci))
    fg = u32.GetForegroundWindow()
    buf = ctypes.create_unicode_buffer(256)
    u32.GetWindowTextW(fg, buf, 256)
    rec = {"tag": tag,
           "clip_rect": [clip.left, clip.top, clip.right, clip.bottom],
           "virtual_screen": list(full),
           "cursor_clipped": [clip.left, clip.top, clip.right, clip.bottom] != list(full),
           "cursor_showing": bool(ci.flags & 1),   # CURSOR_SHOWING
           "foreground_window": buf.value}
    print(f"[{tag}] {json.dumps(rec)}")
    return rec


def api(path, timeout=3):
    with urllib.request.urlopen(BASE + path, timeout=timeout) as r:
        return json.loads(r.read().decode())


record = {"label": LABEL, "exe_mtime": time.ctime(EXE.stat().st_mtime),
          "started": time.strftime("%H:%M:%S"), "samples": []}

record["samples"].append(sample("before_launch"))

proc = subprocess.Popen([str(EXE), "--test", str(PORT)], cwd=str(RELDIR),
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
try:
    deadline = time.time() + 60
    up = False
    while time.time() < deadline:
        try:
            api("/api/state"); up = True; break
        except Exception:
            time.sleep(0.5)
    record["api_up"] = up
    if up:
        try:
            record["screen"] = api("/api/screen/state").get("screen")
        except Exception as e:
            record["screen"] = repr(e)
    # try to force focus so the grab (if any) engages deterministically
    hwnd = u32.FindWindowW(None, "RpgGapProbe")
    record["found_window"] = bool(hwnd)
    if hwnd:
        u32.SetForegroundWindow(hwnd)
    time.sleep(1.0)
    record["samples"].append(sample("running_focused"))
    time.sleep(1.0)
    record["samples"].append(sample("running_focused_2"))
finally:
    proc.kill()          # HARD kill on purpose — the lingering-lock scenario
    time.sleep(1.5)
    record["samples"].append(sample("after_hard_kill"))
    # Always free the user's mouse afterward, whatever the measurement showed.
    u32.ClipCursor(None)
    record["samples"].append(sample("after_probe_release"))

data = json.loads(EVIDENCE.read_text()) if EVIDENCE.exists() else {"runs": []}
data["runs"].append(record)
EVIDENCE.write_text(json.dumps(data, indent=2))
print(f"\nAppended '{LABEL}' run to {EVIDENCE}")
