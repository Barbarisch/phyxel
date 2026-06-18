#!/usr/bin/env python3
"""Doc-sync gate: fail if engine code changed without touching the forwarding surface.

The engine has a "forwarding surface" (docs, the phyxel-gamedev skills, the MCP/CLI
templates, AgentContext) that downstream Claude sessions read. When engine code changes
but none of that surface is updated, the surface silently drifts. This is a MECHANICAL
forget-gate — it can't tell whether the RIGHT doc changed, only that *something* in the
surface was touched (or that the author opted out). Semantic reconciliation is the
`/sync-docs` command. See docs/ForwardingSurface.md.

Usage:
    python tools/check_doc_sync.py [<base>..<head>]   # explicit range
    python tools/check_doc_sync.py                     # default origin/main..HEAD;
                                                       #   pre-push: reads refs on stdin
Exit 0 = OK (no engine change, surface also updated, or opt-out tag). Exit 1 = drift.
Opt out for a genuinely doc-irrelevant change with [skip-docs] (or [docs-ok]) in a commit
message in the range.
"""
from __future__ import annotations

import subprocess
import sys

# Code whose changes usually need the forwarding surface updated.
ENGINE_PREFIXES = ("engine/", "editor/", "scripts/mcp/", "shaders/")
# The forwarding surface itself (touch SOMETHING here when the engine changes).
SURFACE_PREFIXES = (
    "docs/",
    "tools/phyxel-gamedev/",                 # skills + commands
    "tools/phyxel-cli/phyxel_cli/scaffold.py",  # project CLAUDE.md / .mcp.json template
    "tools/create_project.py",
    ".claude/",
    "CLAUDE.md",
    "README",
)
BYPASS_TAGS = ("[skip-docs]", "[docs-ok]")


def _git(args: list[str]) -> str:
    return subprocess.run(["git", *args], capture_output=True, text=True).stdout


def _resolve_range() -> str | None:
    """Range from argv, else pre-push stdin, else origin/main..HEAD."""
    if len(sys.argv) > 1:
        return sys.argv[1]
    # Pre-push passes "<local ref> <local sha> <remote ref> <remote sha>" lines on stdin.
    if not sys.stdin.isatty():
        data = sys.stdin.read().strip()
        if data:
            ranges = []
            for line in data.splitlines():
                parts = line.split()
                if len(parts) >= 4:
                    local_sha, remote_sha = parts[1], parts[3]
                    if set(local_sha) == {"0"}:  # branch deletion — nothing to check
                        continue
                    base = remote_sha if set(remote_sha) != {"0"} else _merge_base_default(local_sha)
                    ranges.append(f"{base}..{local_sha}")
            if ranges:
                return ranges[0]  # first (typically only) ref being pushed
    # Default: what's ahead of the remote main.
    if _git(["rev-parse", "--verify", "-q", "origin/main"]).strip():
        return "origin/main..HEAD"
    return None


def _merge_base_default(sha: str) -> str:
    mb = _git(["merge-base", sha, "origin/main"]).strip() if \
        _git(["rev-parse", "--verify", "-q", "origin/main"]).strip() else ""
    return mb or _git(["rev-list", "--max-parents=0", sha]).strip().splitlines()[0]


def check(rng: str) -> int:
    changed = [f for f in _git(["diff", "--name-only", rng]).splitlines() if f]
    if not changed:
        return 0
    eng = [f for f in changed if f.startswith(ENGINE_PREFIXES)]
    surf = [f for f in changed if f.startswith(SURFACE_PREFIXES)]
    if not eng or surf:
        return 0  # no engine change, or the surface was also touched
    msgs = _git(["log", "--format=%B", rng])
    if any(tag in msgs for tag in BYPASS_TAGS):
        return 0
    print("DOC-SYNC GATE FAILED: engine code changed but the forwarding surface was NOT updated.")
    print(f"  range: {rng}")
    print("  engine files changed (sample):")
    for f in eng[:12]:
        print(f"    {f}")
    print("\n  Update the relevant downstream surface (see docs/ForwardingSurface.md):")
    print("    docs/*.md, tools/phyxel-gamedev/skills/, the CLAUDE.md / .mcp.json templates, ...")
    print("  Run  /sync-docs  to reconcile, or add [skip-docs] to a commit message if this")
    print("  change genuinely needs no doc/skill/API-description update.")
    return 1


def main() -> int:
    rng = _resolve_range()
    if not rng:
        return 0  # nothing to compare against (e.g. no origin/main yet)
    return check(rng)


if __name__ == "__main__":
    raise SystemExit(main())
