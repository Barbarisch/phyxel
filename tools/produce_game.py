#!/usr/bin/env python3
"""Create, build, package, and runtime-verify a Phyxel game in one command."""

from __future__ import annotations

import argparse
import contextlib
import io
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

import create_project
import package_game
import smoke_test_standalone


def find_cmake() -> str | None:
    """Find CMake on PATH or in standard Visual Studio installations."""
    found = shutil.which("cmake")
    if found:
        return found
    program_files = Path(os.environ.get("ProgramFiles", r"C:\Program Files"))
    candidates = []
    for edition in ("Community", "Professional", "Enterprise", "BuildTools"):
        candidates.append(
            program_files
            / "Microsoft Visual Studio"
            / "2022"
            / edition
            / "Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
        )
    return str(next((path for path in candidates if path.is_file()), "")) or None


def _run(command: list[str], cwd: Path) -> dict:
    try:
        completed = subprocess.run(
            command,
            cwd=str(cwd),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
    except OSError as exc:
        return {
            "success": False,
            "returncode": None,
            "command": command,
            "output_tail": str(exc),
        }
    return {
        "success": completed.returncode == 0,
        "returncode": completed.returncode,
        "command": command,
        "output_tail": completed.stdout[-6000:],
    }


def produce(
    name: str,
    project_dir: Path,
    output_dir: Path,
    definition_path: Path | None = None,
    config: str = "Release",
    strict: bool = False,
    run_smoke: bool = True,
    smoke_port: int = 18090,
    startup_timeout: float = 180.0,
) -> dict:
    root = Path(__file__).resolve().parent.parent
    project_dir = project_dir.resolve()
    output_dir = output_dir.resolve()
    result = {"success": False, "name": name, "stages": {}}

    if not (project_dir / "CMakeLists.txt").exists():
        if definition_path is None:
            result["stages"]["scaffold"] = {
                "success": False,
                "error": "project does not exist and no --definition was supplied",
            }
            return result
        definition = json.loads(definition_path.read_text(encoding="utf-8-sig"))
        captured = io.StringIO()
        with contextlib.redirect_stdout(captured):
            create_project.create_project(name, project_dir, root, definition)
        result["stages"]["scaffold"] = {
            "success": True,
            "created": True,
            "output_tail": captured.getvalue()[-3000:],
        }
    else:
        result["stages"]["scaffold"] = {"success": True, "created": False}

    cmake = find_cmake()
    if not cmake:
        result["stages"]["configure"] = {
            "success": False,
            "error": "CMake was not found on PATH or in Visual Studio 2022",
        }
        return result

    configure = _run([cmake, "-B", "build", "-S", "."], project_dir)
    result["stages"]["configure"] = configure
    if not configure["success"]:
        return result

    build = _run(
        [cmake, "--build", "build", "--config", config, "--target", name],
        project_dir,
    )
    result["stages"]["build"] = build
    if not build["success"]:
        return result

    packaged = package_game.package_game(
        name=name,
        output_dir=output_dir,
        definition_path=project_dir / "game.json",
        config=config,
        project_dir=project_dir,
        strict=strict,
    )
    result["stages"]["package"] = packaged
    if not packaged["success"]:
        return result

    if run_smoke:
        smoke = smoke_test_standalone.smoke_test(
            output_dir / f"{name}.exe", smoke_port, startup_timeout
        )
        result["stages"]["smoke"] = smoke
        if not smoke["success"]:
            return result
    else:
        result["stages"]["smoke"] = {"success": True, "skipped": True}

    result["success"] = True
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("name")
    parser.add_argument("--project-dir", type=Path, required=True)
    parser.add_argument("--definition", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--config", choices=("Debug", "Release"), default="Release")
    parser.add_argument("--strict", action="store_true")
    parser.add_argument("--skip-smoke", action="store_true")
    parser.add_argument("--smoke-port", type=int, default=18090)
    parser.add_argument("--startup-timeout", type=float, default=180.0)
    args = parser.parse_args()

    output = args.output or (args.project_dir.parent / "dist" / args.name)
    result = produce(
        args.name,
        args.project_dir,
        output,
        definition_path=args.definition,
        config=args.config,
        strict=args.strict,
        run_smoke=not args.skip_smoke,
        smoke_port=args.smoke_port,
        startup_timeout=args.startup_timeout,
    )
    print(json.dumps(result, indent=2))
    return 0 if result["success"] else 1


if __name__ == "__main__":
    sys.exit(main())
