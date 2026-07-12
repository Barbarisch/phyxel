"""`phyxel` command-line entry.

Phase 0 implements `init` (record the engine location for this machine) and `where`
(diagnostics). `up`/`new`/`link` are stubbed until their phases — see docs/GameDevWorkflow.md.
"""
from __future__ import annotations

import argparse
import os
import sys
from datetime import date
from pathlib import Path

from . import engine, paths, scaffold, status


def _split_genres(raw) -> list:
    """Flatten repeatable + comma-separated --genre into a clean list (order preserved, deduped)."""
    if not raw:
        return []
    out = []
    for item in raw:
        for g in str(item).split(","):
            g = g.strip()
            if g and g not in out:
                out.append(g)
    return out


def _cmd_link(args: argparse.Namespace) -> int:
    target = Path(args.path).expanduser() if args.path else Path.cwd()
    try:
        r = scaffold.link(target, port=args.port, genres=_split_genres(args.genre))
    except (NotADirectoryError, OSError, ValueError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    print(f"phyxel: linked {r['project']} (api port {r['port']})")
    if r.get("genres"):
        print(f"        genres: {', '.join(r['genres'])} (+ core)")
    print(f"        wrote: {', '.join(r['wrote'])}")
    return 0


def _cmd_new(args: argparse.Namespace) -> int:
    out = Path(args.output).expanduser() if args.output else (Path.cwd() / args.name)
    try:
        r = scaffold.new(args.name, out, port=args.port, genres=_split_genres(args.genre))
    except (FileExistsError, OSError, ValueError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    print(f"phyxel: created project {r['project']} (api port {r['port']})")
    if r.get("genres"):
        print(f"        genres: {', '.join(r['genres'])} (+ core)")
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
    if r["status"] == "skipped":
        # Normal for non-project dirs (the SessionStart hook runs everywhere) — quiet no-op.
        print(f"phyxel up: skipped — {r['reason']}")
        return 0
    if r["status"] == "already-running":
        print(f"phyxel: engine already running for {r['project']} on port {r['port']}")
    else:
        print(f"phyxel: launched engine for {r['project']} on port {r['port']} (pid {r['pid']})")
    return 0


def _cmd_status(args: argparse.Namespace) -> int:
    # Resolve project like `up`: explicit arg > session dir (hook) > cwd.
    if args.path:
        target = Path(args.path)
    elif os.environ.get("CLAUDE_PROJECT_DIR"):
        target = Path(os.environ["CLAUDE_PROJECT_DIR"])
    else:
        target = Path.cwd()
    out = status.digest(target.expanduser())
    if out is None:
        # No tracker here (non-project dir, or a project not yet given one). Stay quiet on stdout
        # so the SessionStart hook injects nothing; a hint on stderr for manual use.
        print("phyxel status: no production tracker here "
              "(link with `phyxel link --genre <g>`)", file=sys.stderr)
        return 0
    print(out)
    return 0


def _cmd_feedback(args: argparse.Namespace) -> int:
    home = paths.engine_home()
    if home is None or not paths.looks_like_engine(home):
        print("phyxel feedback: engine home not set/invalid - run `phyxel init`", file=sys.stderr)
        return 1
    text = args.text.strip()
    if not text:
        print("phyxel feedback: empty feedback text", file=sys.stderr)
        return 1
    if args.project:
        project = args.project
    elif os.environ.get("CLAUDE_PROJECT_DIR"):
        project = Path(os.environ["CLAUDE_PROJECT_DIR"]).name
    else:
        project = Path.cwd().name

    inbox = paths.feedback_inbox(home)
    inbox.parent.mkdir(parents=True, exist_ok=True)
    entry = f"## {date.today().isoformat()} — {project} — {args.type}\n{text}\n\n"
    with inbox.open("a", encoding="utf-8") as f:
        f.write(entry)
    print(f"phyxel: logged {args.type} from '{project}' -> {inbox}")
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
    pl.add_argument("--genre", action="append",
                    help="genre template(s) to seed the production tracker (repeatable or "
                         "comma-separated: survival, rpg, action-rpg). Core is always included.")
    pl.set_defaults(func=_cmd_link)

    pn = sub.add_parser("new", help="create a minimal dev project, then link it")
    pn.add_argument("name", help="project name")
    pn.add_argument("--output", help="output directory (default: ./<name>)")
    pn.add_argument("--port", type=int, help="force a specific API port (else allocate)")
    pn.add_argument("--genre", action="append",
                    help="genre template(s) to seed the production tracker (repeatable or "
                         "comma-separated: survival, rpg, action-rpg). Core is always included.")
    pn.set_defaults(func=_cmd_new)

    pu = sub.add_parser("up", help="ensure this project's engine instance is running")
    pu.add_argument("path", nargs="?", help="project directory (default: session/current dir)")
    pu.set_defaults(func=_cmd_up)

    ps = sub.add_parser("status", help="print the production digest (what's done / next)")
    ps.add_argument("path", nargs="?", help="project directory (default: session/current dir)")
    ps.set_defaults(func=_cmd_status)

    pf = sub.add_parser("feedback", help="log a lesson/feature-request to the engine feedback inbox")
    pf.add_argument("text", help="the feedback (one concise paragraph)")
    pf.add_argument("--type", choices=["bug", "gotcha", "feature-request"],
                    default="feature-request")
    pf.add_argument("--project", help="project name (default: session/cwd name)")
    pf.set_defaults(func=_cmd_feedback)
    return p


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
