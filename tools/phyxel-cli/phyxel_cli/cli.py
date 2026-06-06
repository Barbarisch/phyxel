"""`phyxel` command-line entry.

Phase 0 implements `init` (record the engine location for this machine) and `where`
(diagnostics). `up`/`new`/`link` are stubbed until their phases — see docs/GameDevWorkflow.md.
"""
from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

from . import engine, paths, scaffold


def _cmd_link(args: argparse.Namespace) -> int:
    target = Path(args.path).expanduser() if args.path else Path.cwd()
    try:
        r = scaffold.link(target, port=args.port)
    except (NotADirectoryError, OSError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    print(f"phyxel: linked {r['project']} (api port {r['port']})")
    print(f"        wrote: {', '.join(r['wrote'])}")
    return 0


def _cmd_new(args: argparse.Namespace) -> int:
    out = Path(args.output).expanduser() if args.output else (Path.cwd() / args.name)
    try:
        r = scaffold.new(args.name, out, port=args.port)
    except (FileExistsError, OSError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    print(f"phyxel: created project {r['project']} (api port {r['port']})")
    print(f"        wrote: game.json, worlds/, {', '.join(r['wrote'])}")
    return 0


def _cmd_up(args: argparse.Namespace) -> int:
    # Project = explicit arg, else the session's project dir (CLAUDE_PROJECT_DIR set for hooks),
    # else cwd.
    if args.path:
        target = Path(args.path)
    elif os.environ.get("CLAUDE_PROJECT_DIR"):
        target = Path(os.environ["CLAUDE_PROJECT_DIR"])
    else:
        target = Path.cwd()
    r = engine.ensure_up(target.expanduser())
    if r["status"] == "error":
        print(f"phyxel up: {r['error']}", file=sys.stderr)
        return 1
    if r["status"] == "already-running":
        print(f"phyxel: engine already running for {r['project']} on port {r['port']}")
    else:
        print(f"phyxel: launched engine for {r['project']} on port {r['port']} (pid {r['pid']})")
    return 0


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


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="phyxel", description="Phyxel game-dev workflow CLI")
    sub = p.add_subparsers(dest="command", required=True)

    pi = sub.add_parser("init", help="record the engine repo location for this machine")
    pi.add_argument("--home", help="path to the Phyxel engine repo (default: current directory)")
    pi.set_defaults(func=_cmd_init)

    pw = sub.add_parser("where", help="show resolved paths/config (diagnostics)")
    pw.set_defaults(func=_cmd_where)

    pl = sub.add_parser("link", help="add the Claude workflow files to an existing project")
    pl.add_argument("path", nargs="?", help="project directory (default: current directory)")
    pl.add_argument("--port", type=int, help="force a specific API port (else reuse/allocate)")
    pl.set_defaults(func=_cmd_link)

    pn = sub.add_parser("new", help="create a minimal dev project, then link it")
    pn.add_argument("name", help="project name")
    pn.add_argument("--output", help="output directory (default: ./<name>)")
    pn.add_argument("--port", type=int, help="force a specific API port (else allocate)")
    pn.set_defaults(func=_cmd_new)

    pu = sub.add_parser("up", help="ensure this project's engine instance is running")
    pu.add_argument("path", nargs="?", help="project directory (default: session/current dir)")
    pu.set_defaults(func=_cmd_up)
    return p


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
