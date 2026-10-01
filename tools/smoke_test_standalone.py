#!/usr/bin/env python3
"""Launch and verify a real Phyxel standalone through its opt-in test API."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
import urllib.request
from pathlib import Path


def _request(base: str, path: str, body: dict | None = None, timeout: float = 2.0):
    data = json.dumps(body).encode("utf-8") if body is not None else None
    request = urllib.request.Request(
        f"{base}{path}",
        data=data,
        headers={"Content-Type": "application/json"} if data else {},
        method="POST" if data else "GET",
    )
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return json.loads(response.read().decode("utf-8"))


def evaluate_probe(project: dict, screen: dict, render: dict) -> list[str]:
    """Return contract violations from one running standalone probe."""
    errors = []
    if not project.get("standalone"):
        errors.append("/api/project/info did not identify a standalone game")
    if not project.get("game"):
        errors.append("standalone game name is missing")
    if screen.get("screen") in (None, "unknown"):
        errors.append("screen state is unavailable")
    if render.get("error"):
        errors.append(f"render stats failed: {render['error']}")
    if render.get("visible_chunk_count", 0) <= 0:
        errors.append("no visible chunks were rendered")
    if render.get("total_visible_faces", 0) <= 0:
        errors.append("no visible world faces were rendered")
    return errors


def _camera_position(state: dict):
    try:
        position = state["camera"]["position"]
        return (float(position["x"]), float(position["y"]), float(position["z"]))
    except (KeyError, TypeError, ValueError):
        return None


def smoke_test(executable: Path, port: int, startup_timeout: float) -> dict:
    executable = executable.resolve()
    if not executable.is_file():
        return {"success": False, "errors": [f"executable not found: {executable}"]}

    base = f"http://127.0.0.1:{port}"
    process = subprocess.Popen(
        [str(executable), "--test", str(port)],
        cwd=str(executable.parent),
    )
    started_at = time.monotonic()
    deadline = started_at + startup_timeout
    project = None
    try:
        while time.monotonic() < deadline:
            if process.poll() is not None:
                return {
                    "success": False,
                    "errors": [f"game exited during startup with code {process.returncode}"],
                }
            try:
                project = _request(base, "/api/project/info")
                break
            except Exception:
                time.sleep(0.2)

        if project is None:
            return {
                "success": False,
                "errors": [f"test API did not start within {startup_timeout:.1f}s"],
            }

        startup_seconds = time.monotonic() - started_at
        screen = _request(base, "/api/screen/state")
        render = _request(base, "/api/render/stats")
        errors = evaluate_probe(project, screen, render)

        interaction = {}
        # Standalones may open on an intro/splash or directly on a menu. The
        # semantic Start action must make either path reach controllable play.
        if not errors and screen.get("screen") != "playing":
            interaction["start"] = _request(
                base, "/api/screen/action", {"action": "start"}
            )
            time.sleep(0.2)
            playing = _request(base, "/api/screen/state")
            interaction["playing"] = playing
            if playing.get("screen") != "playing":
                errors.append("semantic Start action did not enter gameplay")

        if not errors:
            before = _request(base, "/api/state")
            interaction["move"] = _request(
                base, "/api/input/inject", {"keys": ["W"], "hold": 0.5}
            )
            time.sleep(0.8)
            after = _request(base, "/api/state")
            p0, p1 = _camera_position(before), _camera_position(after)
            interaction["camera_before"] = p0
            interaction["camera_after"] = p1
            if p0 is None or p1 is None:
                errors.append("camera position unavailable for movement proof")
            elif sum((b - a) ** 2 for a, b in zip(p0, p1)) ** 0.5 <= 0.05:
                errors.append("injected forward input did not move the gameplay camera")

        return {
            "success": not errors,
            "errors": errors,
            "startup_seconds": round(startup_seconds, 3),
            "project": project,
            "screen": screen,
            "render": render,
            "interaction": interaction,
        }
    finally:
        if process.poll() is None:
            try:
                _request(base, "/api/engine/shutdown", {}, timeout=3.0)
                process.wait(timeout=5.0)
            except Exception:
                process.terminate()
                try:
                    process.wait(timeout=3.0)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=3.0)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Launch a Phyxel standalone and verify its real test API/render state."
    )
    parser.add_argument("executable", type=Path)
    parser.add_argument("--port", type=int, default=18090)
    parser.add_argument("--startup-timeout", type=float, default=180.0)
    args = parser.parse_args()

    result = smoke_test(args.executable, args.port, args.startup_timeout)
    print(json.dumps(result, indent=2))
    return 0 if result["success"] else 1


if __name__ == "__main__":
    sys.exit(main())
