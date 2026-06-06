"""`phyxel` command-line entry.

Phase 0 implements `init` (record the engine location for this machine) and `where`
(diagnostics). `up`/`new`/`link` are stubbed until their phases — see docs/GameDevWorkflow.md.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

from . import paths


def _cmd_init(args: argparse.Namespace) -> int:
    home = Path(args.home).expanduser().resolve() if args.home else Path.cwd().resolve()
    if not paths.looks_like_engine(home):
        print(
            f"error: {home} does not look like the Phyxel engine repo "
            f"(no scripts/mcp/phyxel_mcp_server.py).\n"
            f"       Run from the engine repo, or pass --home <engine repo path>.",
            file=sys.stderr,
        )
        return 1
    cfg = paths.load_config()
    cfg["engine_home"] = str(home)
    written = paths.save_config(cfg)
    print(f"phyxel: engine home set to {home}")
    print(f"phyxel: config written to {written}")
    return 0


def _cmd_where(args: argparse.Namespace) -> int:
    home = paths.engine_home()
    print(f"config:       {paths.config_path()}")
    print(f"engine_home:  {home if home else '(unset - run `phyxel init`)'}")
    if home:
        print(f"  valid:      {paths.looks_like_engine(home)}")
        print(f"  mcp server: {paths.mcp_server_script(home)}")
        binp = paths.engine_binary(home)
        print(f"  engine bin: {binp if binp else '(not built / not found)'}")
    cwd = Path.cwd()
    print(f"cwd:          {cwd}")
    print(f"  api port:   {paths.project_api_port(cwd)}")
    return 0


def _stub(name: str, phase: int):
    def run(_args: argparse.Namespace) -> int:
        print(f"phyxel {name}: not implemented yet (Phase {phase}; see docs/GameDevWorkflow.md)")
        return 2
    return run


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="phyxel", description="Phyxel game-dev workflow CLI")
    sub = p.add_subparsers(dest="command", required=True)

    pi = sub.add_parser("init", help="record the engine repo location for this machine")
    pi.add_argument("--home", help="path to the Phyxel engine repo (default: current directory)")
    pi.set_defaults(func=_cmd_init)

    pw = sub.add_parser("where", help="show resolved paths/config (diagnostics)")
    pw.set_defaults(func=_cmd_where)

    sub.add_parser("up", help="ensure this project's engine is running (Phase 4)").set_defaults(
        func=_stub("up", 4))
    sub.add_parser("new", help="scaffold a new game project (Phase 2)").set_defaults(
        func=_stub("new", 2))
    sub.add_parser("link", help="retrofit an existing project (Phase 2)").set_defaults(
        func=_stub("link", 2))
    return p


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
