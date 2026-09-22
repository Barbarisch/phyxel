#!/usr/bin/env python3
"""Regression test: a shader that does not compile must FAIL the build.

    python tools/test_shader_build_fails_loudly.py

Why this exists. On 2026-09-22 `voxel.frag` failed to compile with a real GLSL error and
`build_shaders.bat` printed "All shaders compiled successfully!" and exited 0; running
`shader_manifest.py --check` immediately afterwards also reported everything current. Both
guards passed over a broken shader, and the only thing that caught it was comparing an .spv
mtime by hand.

That matters because `shaders/*.spv` are COMMITTED artifacts and glslc does not track
`#include` deps: commit the .glsl with a stale .spv and the author's machine renders correctly
(nothing rebuilt) while every other checkout renders the OLD shader, with CI green. That is
how the transposed-AgX fix shipped a pink world to everyone but its author for five days
(commit 20341333) -- this was that failure mode plus a source that did not even compile.

Three separate cmd traps were involved, and each masked the next:
  1. `if %errorlevel%` inside a block is expanded at PARSE time, so every check inside the big
     `if defined USE_GLSLC ( ... )` block read one stale value from before any compile ran.
  2. `setlocal enabledelayedexpansion` fixes that, but then `exit /b` triggers an implicit
     `endlocal` that restores the PREVIOUS errorlevel.
  3. `exit /b 1` inside a `||` block nested inside the `if defined` block halts the script but
     still returns 0.
The shipped shape is `|| goto :shader_error` with the only `exit /b 1` at top level.

This test asserts the OUTCOME, not the mechanism, so a future rewrite of the script in any
style still has to satisfy it.
"""

import os
import shutil
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# crack.glsl is an #include with no entry point of its own, so breaking it exercises the
# include path -- the case glslc does not track and the manifest cannot see.
VICTIM = os.path.join(REPO, "shaders", "crack.glsl")
BAT = os.path.join(REPO, "build_shaders.bat")


def run_build():
    env = dict(os.environ, SHADERS_NONINTERACTIVE="1")
    return subprocess.run(["cmd", "/c", BAT], cwd=REPO, env=env,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)


def main():
    if os.name != "nt":
        print("SKIP: build_shaders.bat is Windows-only")
        return 0
    for path in (VICTIM, BAT):
        if not os.path.exists(path):
            print("FAIL: missing %s" % path)
            return 1

    backup = os.path.join(tempfile.gettempdir(), "crack.glsl.shaderguard.bak")
    shutil.copy2(VICTIM, backup)
    failures = []
    try:
        # --- ARM 1: the good path must still succeed -------------------------------------
        r = run_build()
        if r.returncode != 0:
            failures.append("clean build returned %d, expected 0 (the guard must not cry "
                            "wolf)" % r.returncode)
        if b"All shaders compiled successfully" not in r.stdout:
            failures.append("clean build did not print the success banner")

        # --- ARM 2: a broken shader must fail the build ----------------------------------
        with open(VICTIM, "a", encoding="utf-8") as f:
            f.write("\nthis_is_not_valid_glsl @@@ ;\n")
        r = run_build()
        if r.returncode == 0:
            failures.append("BROKEN shader returned exit code 0 -- a failed compile is "
                            "invisible to callers, and a stale .spv can be committed with CI "
                            "green")
        if b"All shaders compiled successfully" in r.stdout:
            failures.append("BROKEN shader still printed the success banner")
    finally:
        shutil.copy2(backup, VICTIM)
        os.remove(backup)
        # Leave the tree with correct .spv on disk regardless of outcome.
        run_build()

    if failures:
        print("FAIL: shader build does not fail loudly")
        for f in failures:
            print("  - " + f)
        return 1
    print("PASS: a broken shader fails the build (non-zero exit, no success banner), "
          "and a clean build still succeeds")
    return 0


if __name__ == "__main__":
    sys.exit(main())
