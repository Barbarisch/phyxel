#!/usr/bin/env python3
"""PostToolUse hook: enforce the sub-voxel-detail rule on .voxel edits.

Wired in .claude/settings.json to run after Write/Edit. Reads the tool-call
JSON on stdin, and if the edited file is a .voxel asset, runs
tools/lint_voxel_detail.py on it. On a violation it exits 2 with the message on
stderr, which Claude Code feeds back to the model — so a full-cube handtool can
never be written unnoticed. This is the machinery that makes "always use
microcubes for handtools" impossible to forget (CLAUDE.md is the reminder; this
is the enforcer).
"""
import json
import subprocess
import sys
from pathlib import Path


def main() -> int:
    try:
        payload = json.load(sys.stdin)
    except Exception:
        return 0  # no/invalid input — don't block anything

    tool_input = payload.get("tool_input", {}) or {}
    fpath = tool_input.get("file_path") or tool_input.get("path")
    if not fpath or not str(fpath).endswith(".voxel"):
        return 0
    if not Path(fpath).exists():
        return 0

    here = Path(__file__).resolve().parents[1]  # tools/
    lint = here / "lint_voxel_detail.py"
    result = subprocess.run(
        [sys.executable, str(lint), str(fpath)],
        capture_output=True, text=True,
    )
    if result.returncode != 0:
        sys.stderr.write(result.stderr or result.stdout)
        sys.stderr.write(
            "\nFix: author this handtool in microcubes (M lines), skinny and at "
            "true scale, like resources/templates/weapons/sword_fine.voxel.\n"
        )
        return 2  # blocking: surfaces stderr back to the model
    return 0


if __name__ == "__main__":
    sys.exit(main())
