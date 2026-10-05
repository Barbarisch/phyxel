"""DebrisInteractionPlan 1a: a shader that does not compile must FAIL build_shaders.bat.

Inside a parenthesised cmd block (an if/else) `%errorlevel%` is expanded once, when the whole
block is PARSED, so a compile followed by `if %errorlevel% neq 0 ( ... exit /b 1 )` always
tested the stale pre-block value: a broken solver shader printed its error, the script carried
on, and the old .spv stayed on disk. The working pattern is `... || goto :shader_error` (see the
header of build_shaders.bat).

Each test copies build_shaders.bat + shaders/ into a temp dir, breaks ONE shader, and runs the
script there (SHADERS_NONINTERACTIVE=1 skips `pause`). The script has ONE compiler,
glslangValidator, found either via VULKAN_SDK ("sdk") or via PATH ("path" — exercises the
`where` detection, which had the same parse-time %errorlevel% bug). A machine with neither
must fail with a clear message. Windows-only: it is a .bat.

BUILD_SHADERS_BAT=<path> runs the suite against another copy of the script (used to show the
pre-fix script fails these tests).
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[1]
BAT = Path(os.environ.get("BUILD_SHADERS_BAT", REPO / "build_shaders.bat"))
SDK_BIN = Path(os.environ.get("VULKAN_SDK", "")) / "Bin"

pytestmark = pytest.mark.skipif(sys.platform != "win32", reason="build_shaders.bat is Windows-only")


def _stage(tmp_path: Path) -> Path:
    work = tmp_path / "repo"
    work.mkdir()
    shutil.copy2(BAT, work / "build_shaders.bat")
    shutil.copytree(REPO / "shaders", work / "shaders",
                    ignore=shutil.ignore_patterns("*.spv"))
    return work


def _env(tmp_path: Path, compiler: str) -> dict:
    """compiler: "sdk" (VULKAN_SDK set), "path" (only PATH has glslangValidator), "none"."""
    env = dict(os.environ, SHADERS_NONINTERACTIVE="1")
    if compiler in ("path", "none"):
        if not (SDK_BIN / "glslangValidator.exe").exists():
            pytest.skip("glslangValidator.exe not found under VULKAN_SDK")
        env.pop("VULKAN_SDK", None)
        sysroot = os.environ.get("SystemRoot", r"C:\Windows")
        dirs = [sysroot + r"\System32", sysroot]
        if compiler == "path":
            only = tmp_path / "bin"
            only.mkdir()
            shutil.copy2(SDK_BIN / "glslangValidator.exe", only / "glslangValidator.exe")
            dirs.insert(0, str(only))
        env["PATH"] = os.pathsep.join(dirs)
    return env


def _run(work: Path, env: dict) -> subprocess.CompletedProcess:
    # The script uses relative shaders\ paths, so it must run with the staged copy as cwd.
    # Absolute path: with NoDefaultCurrentDirectoryInExePath set, cmd never searches cwd.
    return subprocess.run(["cmd.exe", "/c", str(work / "build_shaders.bat")], cwd=work,
                          capture_output=True, text=True, env=env, timeout=900)


def _break(work: Path, shader: str) -> None:
    p = work / "shaders" / shader
    p.write_text(p.read_text(encoding="utf-8") + "\nthis is not glsl;\n", encoding="utf-8")


# One shader per rule family that used the broken `if %errorlevel%` form or had no check:
# a solver pass, a broadphase scan pass, and post_process.frag (no check at all).
@pytest.mark.parametrize("compiler", ["sdk", "path"])
@pytest.mark.parametrize("shader", ["solver_voxel.comp", "particle_scan_block.comp",
                                    "post_process.frag"])
def test_a_broken_shader_fails_the_build(tmp_path, shader, compiler):
    env = _env(tmp_path, compiler)
    work = _stage(tmp_path)
    _break(work, shader)
    r = _run(work, env)
    out = r.stdout + r.stderr
    assert r.returncode != 0, f"[{compiler}] {shader} did not compile but the script exited 0:\n{out[-3000:]}"
    assert "BUILD FAILED" in out, out[-3000:]
    assert not (work / "shaders" / (shader + ".spv")).exists(), "a .spv was written for the broken shader"


@pytest.mark.parametrize("compiler", ["sdk", "path"])
def test_control_the_untouched_tree_builds(tmp_path, compiler):
    """Control: the same staging with nothing broken exits 0 (the manifest step only warns
    here because tools/ is not copied)."""
    env = _env(tmp_path, compiler)
    work = _stage(tmp_path)
    r = _run(work, env)
    out = r.stdout + r.stderr
    assert r.returncode == 0, f"[{compiler}]\n{out[-3000:]}"
    assert "All shaders compiled successfully!" in out, out[-3000:]
    assert (work / "shaders" / "solver_voxel.comp.spv").exists()


def test_no_compiler_fails_with_a_clear_message(tmp_path):
    """Neither VULKAN_SDK nor PATH has glslangValidator: exit 1 BEFORE compiling anything."""
    env = _env(tmp_path, "none")
    work = _stage(tmp_path)
    r = _run(work, env)
    out = r.stdout + r.stderr
    assert r.returncode != 0, out[-3000:]
    assert "Could not find glslangValidator" in out, out[-3000:]
    assert "Compiling" not in out, "it started compiling with no compiler"
