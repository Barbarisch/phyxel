#!/usr/bin/env python3
"""
Phyxel Visual Test Pipeline
============================
Standalone CI runner — no Claude or MCP required.

Usage:
    python scripts/run_visual_tests.py [--config Debug|Release] [--project <path>]
                                       [--skip-build] [--scenario <name>]
                                       [--output <dir>]

Exit codes:
    0  All scenarios passed
    1  One or more scenarios failed
    2  Pipeline infrastructure failure (build failed, engine won't start, etc.)
"""

import argparse
import asyncio
import base64
import json
import os
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

import httpx

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------

REPO_ROOT = Path(__file__).resolve().parent.parent
ENGINE_API = "http://localhost:8090"
API_TIMEOUT = 10.0
ENGINE_STARTUP_TIMEOUT = 30  # seconds to wait for HTTP API after launch
LOG_LINES_PER_TEST = 80
SCREENSHOT_DIR_DEFAULT = REPO_ROOT / "build" / "visual_test_results"
CHARTEST_PROJECT = r"C:\Users\jack\Documents\PhyxelProjects\CharacterTestbed"

CMAKE_PATHS = [
    r"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
    r"C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
    "cmake",
]


# ---------------------------------------------------------------------------
# Scenario definitions
# ---------------------------------------------------------------------------

SCENARIOS = {
    "mirror": {
        "description": "Mirror material reflection",
        "log_modules": ["RenderCoordinator"],
        "setup": [
            {"action": "clear_all_entities"},
            {"action": "fill_region", "x1": 10, "y1": 16, "z1": 10, "x2": 30, "y2": 16, "z2": 30, "material": "Stone"},
            {"action": "fill_region", "x1": 18, "y1": 17, "z1": 20, "x2": 22, "y2": 21, "z2": 20, "material": "Mirror"},
            {"action": "fill_region", "x1": 14, "y1": 17, "z1": 17, "x2": 15, "y2": 19, "z2": 19, "material": "glow"},
            {"action": "fill_region", "x1": 25, "y1": 17, "z1": 17, "x2": 26, "y2": 19, "z2": 19, "material": "Glass"},
            {"action": "set_camera", "x": 20, "y": 19, "z": 14, "yaw": -90, "pitch": -5},
        ],
        "captures": [
            {"overlay": "none", "label": "primary"},
            {"overlay": "normals", "label": "normals"},
            {"action": "set_camera", "x": 12, "y": 19, "z": 20, "yaw": 0, "pitch": -5},
            {"overlay": "none", "label": "side_angle"},
        ],
        "checks": [
            lambda stats, _logs: (
                stats.get("visible_chunk_count", 0) > 0,
                f"visible_chunk_count={stats.get('visible_chunk_count',0)} (expected >0)"
            ),
            lambda stats, _logs: (
                stats.get("mirror_pass_ran", False),
                f"mirror_pass_ran={stats.get('mirror_pass_ran')} (expected true)"
            ),
            lambda stats, _logs: (
                stats.get("reflection_draw_calls", 0) > 0,
                f"reflection_draw_calls={stats.get('reflection_draw_calls',0)} (expected >0)"
            ),
        ],
    },

    "shadow": {
        "description": "Directional shadow map",
        "log_modules": ["Rendering"],
        "setup": [
            {"action": "clear_all_entities"},
            {"action": "fill_region", "x1": 10, "y1": 16, "z1": 10, "x2": 30, "y2": 16, "z2": 30, "material": "Stone"},
            {"action": "fill_region", "x1": 19, "y1": 17, "z1": 19, "x2": 21, "y2": 24, "z2": 21, "material": "Wood"},
            {"action": "set_ambient_light", "level": 0.2},
            {"action": "set_camera", "x": 10, "y": 26, "z": 10, "yaw": 45, "pitch": -30},
        ],
        "captures": [
            {"overlay": "none", "label": "primary"},
        ],
        "checks": [
            lambda stats, _logs: (
                stats.get("visible_chunk_count", 0) > 0,
                f"visible_chunk_count={stats.get('visible_chunk_count',0)} (expected >0)"
            ),
        ],
    },

    "ssao": {
        "description": "Screen-space ambient occlusion",
        "log_modules": ["PostProcessor"],
        "setup": [
            {"action": "clear_all_entities"},
            {"action": "fill_region", "x1": 5, "y1": 16, "z1": 5, "x2": 35, "y2": 16, "z2": 35, "material": "Stone"},
            {"action": "fill_region", "x1": 14, "y1": 17, "z1": 14, "x2": 26, "y2": 20, "z2": 15, "material": "Stone"},
            {"action": "fill_region", "x1": 14, "y1": 17, "z1": 15, "x2": 15, "y2": 20, "z2": 26, "material": "Stone"},
            {"action": "set_camera", "x": 20, "y": 22, "z": 22, "yaw": 225, "pitch": -25},
        ],
        "captures": [
            {"overlay": "none", "label": "primary"},
        ],
        "checks": [
            lambda stats, _logs: (
                stats.get("visible_chunk_count", 0) > 0,
                f"visible_chunk_count={stats.get('visible_chunk_count',0)} (expected >0)"
            ),
        ],
    },
}


# ---------------------------------------------------------------------------
# HTTP helpers
# ---------------------------------------------------------------------------

async def api_get(path: str) -> dict:
    async with httpx.AsyncClient(timeout=API_TIMEOUT) as client:
        r = await client.get(f"{ENGINE_API}{path}")
        r.raise_for_status()
        return r.json()


async def api_post(path: str, body: dict = None) -> dict:
    async with httpx.AsyncClient(timeout=API_TIMEOUT) as client:
        r = await client.post(f"{ENGINE_API}{path}", json=body or {})
        r.raise_for_status()
        return r.json()


async def api_command(action: str, params: dict = None) -> dict:
    return await api_post("/api/command", {"action": action, "params": params or {}})


# ---------------------------------------------------------------------------
# Engine lifecycle
# ---------------------------------------------------------------------------

def find_cmake() -> str:
    for p in CMAKE_PATHS:
        if Path(p).exists() or p == "cmake":
            return p
    raise RuntimeError("cmake not found")


def build_engine(config: str) -> bool:
    cmake = find_cmake()
    print(f"[build] cmake --build build --config {config}")
    result = subprocess.run(
        [cmake, "--build", str(REPO_ROOT / "build"), "--config", config],
        cwd=str(REPO_ROOT),
        capture_output=False,  # let it stream to stdout
    )
    return result.returncode == 0


def find_exe(config: str) -> Path | None:
    candidates = [
        REPO_ROOT / "build" / "editor" / config / "phyxel.exe",
        REPO_ROOT / "build" / "game" / config / "phyxel.exe",
        REPO_ROOT / "phyxel.exe",
    ]
    return next((p for p in candidates if p.exists()), None)


def launch_engine(config: str, project: str) -> subprocess.Popen:
    exe = find_exe(config)
    if exe is None:
        raise RuntimeError(f"Engine exe not found for config={config}. Build first.")
    args = [str(exe), "--project", project]
    print(f"[launch] {' '.join(args)}")
    return subprocess.Popen(args, cwd=str(REPO_ROOT))


async def wait_for_api(timeout: int = ENGINE_STARTUP_TIMEOUT) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            r = await api_get("/api/status")
            if "error" not in r:
                return True
        except Exception:
            pass
        await asyncio.sleep(1.0)
    return False


# ---------------------------------------------------------------------------
# Test execution
# ---------------------------------------------------------------------------

async def set_log_level(module: str, level: str = "debug"):
    await api_command("set_log_level", {"module": module, "level": level})


async def run_setup_step(step: dict):
    action = step["action"]
    params = {k: v for k, v in step.items() if k != "action"}
    result = await api_command(action, params)
    if "error" in result:
        print(f"  [warn] {action} returned error: {result['error']}")


async def take_screenshot(output_dir: Path, label: str) -> Path | None:
    result = await api_command("screenshot", {})
    path_str = result.get("path") or result.get("filepath")
    if not path_str:
        print(f"  [warn] screenshot returned no path: {result}")
        return None
    src = Path(path_str)
    dst = output_dir / f"{label}.png"
    if src.exists():
        import shutil
        shutil.copy2(src, dst)
        return dst
    return None


async def get_render_stats() -> dict:
    try:
        return await api_command("get_render_stats", {})
    except Exception as e:
        return {"error": str(e)}


def clear_log() -> None:
    """Truncate phyxel.log before each scenario so reads are scenario-scoped."""
    log_path = REPO_ROOT / "phyxel.log"
    if log_path.exists():
        log_path.write_text("", encoding="utf-8")


def read_log_tail(lines: int = LOG_LINES_PER_TEST, module: str | None = None) -> list[str]:
    log_path = REPO_ROOT / "phyxel.log"
    if not log_path.exists():
        return []
    content = log_path.read_text(encoding="utf-8", errors="replace").splitlines()
    if module:
        content = [l for l in content if f"[{module}]" in l or f"] {module}" in l]
    return content[-lines:]


async def run_scenario(name: str, scenario: dict, output_dir: Path) -> dict:
    scen_dir = output_dir / name
    scen_dir.mkdir(parents=True, exist_ok=True)

    result = {
        "name": name,
        "description": scenario["description"],
        "passed": True,
        "failures": [],
        "screenshots": [],
        "stats": {},
        "log_lines": [],
    }

    print(f"\n{'='*60}")
    print(f"SCENARIO: {name} — {scenario['description']}")
    print('='*60)

    # Clear log so this scenario's reads only see its own output
    clear_log()

    # Set log levels
    for mod in scenario.get("log_modules", []):
        await set_log_level(mod, "debug")
        print(f"  [log] {mod} → debug")

    # Execute setup steps
    print("  [setup]")
    for step in scenario.get("setup", []):
        await run_setup_step(step)
        await asyncio.sleep(0.1)

    # Wait one render frame
    await asyncio.sleep(1.5)

    # Captures
    last_stats: dict = {}
    capture_idx = 0
    for item in scenario.get("captures", []):
        if "action" in item:
            # Camera move between captures
            await run_setup_step(item)
            await asyncio.sleep(0.5)
            continue

        overlay = item.get("overlay", "none")
        label = f"{capture_idx:02d}_{item.get('label', overlay)}"
        capture_idx += 1

        # Set overlay
        overlay_modes = {"none": (-1, False), "wireframe": (0, True), "normals": (1, True),
                         "hierarchy": (2, True), "uv": (3, True), "emissive": (4, True)}
        mode_id, enabled = overlay_modes.get(overlay, (-1, False))
        await api_command("set_debug_overlay", {"enabled": enabled, "mode": max(0, mode_id)})
        await asyncio.sleep(0.1)

        png = await take_screenshot(scen_dir, label)
        if png:
            result["screenshots"].append(str(png))
            print(f"  [capture] {label} → {png.name}")

        last_stats = await get_render_stats()

        # Restore overlay off
        await api_command("set_debug_overlay", {"enabled": False, "mode": 0})

    result["stats"] = last_stats

    # Grab logs
    for mod in scenario.get("log_modules", []):
        result["log_lines"].extend(read_log_tail(LOG_LINES_PER_TEST, mod))

    # Run checks
    print("  [checks]")
    for check_fn in scenario.get("checks", []):
        try:
            ok, msg = check_fn(last_stats, result["log_lines"])
            status = "PASS" if ok else "FAIL"
            print(f"    [{status}] {msg}")
            if not ok:
                result["passed"] = False
                result["failures"].append(msg)
        except Exception as e:
            result["passed"] = False
            result["failures"].append(f"Check raised exception: {e}")

    return result


# ---------------------------------------------------------------------------
# HTML report generation
# ---------------------------------------------------------------------------

def build_html_report(results: list[dict], run_time: str, output_dir: Path) -> Path:
    rows = []
    for r in results:
        status_cls = "pass" if r["passed"] else "fail"
        status_label = "PASS" if r["passed"] else "FAIL"
        failures_html = ""
        if r["failures"]:
            items = "".join(f"<li>{f}</li>" for f in r["failures"])
            failures_html = f"<ul class='failures'>{items}</ul>"

        screenshots_html = ""
        for png in r["screenshots"]:
            png_path = Path(png)
            rel = png_path.relative_to(output_dir)
            screenshots_html += f'<img src="{rel}" title="{png_path.name}" />'

        stats_json = json.dumps(r["stats"], indent=2)
        log_text = "\n".join(r["log_lines"][-30:])

        rows.append(f"""
        <div class="scenario {status_cls}">
          <h2>{r['name']} — <span class="status">{status_label}</span></h2>
          <p>{r['description']}</p>
          {failures_html}
          <div class="screenshots">{screenshots_html}</div>
          <details><summary>Render Stats</summary><pre>{stats_json}</pre></details>
          <details><summary>Log Tail ({len(r['log_lines'])} lines)</summary><pre>{log_text}</pre></details>
        </div>
        """)

    all_pass = all(r["passed"] for r in results)
    summary = f"{'ALL PASS' if all_pass else 'FAILURES DETECTED'} — {sum(r['passed'] for r in results)}/{len(results)} scenarios passed"

    html = f"""<!DOCTYPE html>
<html><head>
<meta charset="utf-8">
<title>Phyxel Visual Test Report — {run_time}</title>
<style>
  body {{ font-family: monospace; background: #1a1a2e; color: #e0e0e0; padding: 20px; }}
  h1 {{ color: #a8dadc; }}
  .scenario {{ border: 1px solid #333; border-radius: 6px; margin: 16px 0; padding: 16px; }}
  .pass {{ border-color: #4caf50; }}
  .fail {{ border-color: #f44336; }}
  .status {{ font-weight: bold; }}
  .pass .status {{ color: #4caf50; }}
  .fail .status {{ color: #f44336; }}
  .failures li {{ color: #ff8a80; }}
  .screenshots img {{ max-height: 300px; margin: 4px; border: 1px solid #555; }}
  pre {{ background: #0d1117; padding: 12px; overflow: auto; font-size: 12px; }}
  summary {{ cursor: pointer; color: #90caf9; }}
</style>
</head><body>
<h1>Phyxel Visual Test Report</h1>
<p><strong>Run time:</strong> {run_time}</p>
<p><strong>Summary:</strong> {summary}</p>
{''.join(rows)}
</body></html>"""

    report_path = output_dir / "report.html"
    report_path.write_text(html, encoding="utf-8")
    return report_path


# ---------------------------------------------------------------------------
# Main pipeline
# ---------------------------------------------------------------------------

async def main(args: argparse.Namespace) -> int:
    output_dir = Path(args.output)
    output_dir.mkdir(parents=True, exist_ok=True)
    run_time = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    # 1. Build
    if not args.skip_build:
        print("\n[pipeline] Building engine...")
        if not build_engine(args.config):
            print("[pipeline] Build FAILED — aborting")
            return 2

    # 2. Launch engine
    engine_proc = None
    try:
        print(f"\n[pipeline] Launching engine with project: {args.project}")
        engine_proc = launch_engine(args.config, args.project)

        print(f"[pipeline] Waiting for API (up to {ENGINE_STARTUP_TIMEOUT}s)...")
        if not await wait_for_api():
            print("[pipeline] Engine API did not come up — aborting")
            return 2
        print("[pipeline] Engine API responsive")

        # Extra wait for project load
        await asyncio.sleep(2.0)

    except Exception as e:
        print(f"[pipeline] Launch failed: {e}")
        return 2

    # 3. Run scenarios
    scenario_names = [args.scenario] if args.scenario else list(SCENARIOS.keys())
    results = []
    try:
        for name in scenario_names:
            if name not in SCENARIOS:
                print(f"[pipeline] Unknown scenario '{name}' — skipping")
                continue
            result = await run_scenario(name, SCENARIOS[name], output_dir)
            results.append(result)
    finally:
        # 4. Stop engine
        if engine_proc and engine_proc.poll() is None:
            print("\n[pipeline] Stopping engine...")
            engine_proc.terminate()
            try:
                engine_proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                engine_proc.kill()
            print("[pipeline] Engine stopped")

    # 5. Generate report
    report = build_html_report(results, run_time, output_dir)
    print(f"\n[pipeline] Report written: {report}")

    passed = sum(r["passed"] for r in results)
    total = len(results)
    print(f"[pipeline] {passed}/{total} scenarios passed")

    return 0 if all(r["passed"] for r in results) else 1


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Phyxel Visual Test Pipeline")
    parser.add_argument("--config", default="Debug", choices=["Debug", "Release"])
    parser.add_argument("--project", default=CHARTEST_PROJECT, help="Game project directory")
    parser.add_argument("--skip-build", action="store_true", help="Skip cmake build step")
    parser.add_argument("--scenario", default=None, help="Run only this scenario (default: all)")
    parser.add_argument("--output", default=str(SCREENSHOT_DIR_DEFAULT), help="Output directory for screenshots and report")
    args = parser.parse_args()

    sys.exit(asyncio.run(main(args)))
