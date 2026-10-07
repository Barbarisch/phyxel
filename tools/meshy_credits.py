#!/usr/bin/env python3
"""meshy_credits.py — watch Meshy API credit usage (CharacterAnimationRoadmap.md R2).

Owner rule (2026-10-01): warn the owner when Meshy credits drop below 10 % of the allotment.
Run this BEFORE and AFTER every Meshy job; it logs each reading so per-job cost is visible.

    python tools/meshy_credits.py                      # balance, % of allotment, WARN if < 10 %
    python tools/meshy_credits.py --label "wolf v2"    # same, and tag the log entry
    python tools/meshy_credits.py --set-allotment 4000 # define 100 % (e.g. the monthly grant)
    python tools/meshy_credits.py --history            # recent readings with deltas

Key: env MESHY_API_KEY, else tools/meshy.local.json {"api_key": "..."} (git-ignored).
Never printed or logged by these tools; never commit it.
State: tools/meshy_credits.local.json (git-ignored): allotment + reading log.
Exit codes: 0 ok · 2 below threshold (WARN) · 1 error (no key, HTTP failure).
Endpoint: GET https://api.meshy.ai/openapi/v1/balance -> {"balance": N}.
Test hook: env MESHY_CREDITS_FAKE_BALANCE=N skips the network call.
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import sys
import urllib.request
from pathlib import Path

API_URL = "https://api.meshy.ai/openapi/v1/balance"
STATE = Path(__file__).resolve().parent / "meshy_credits.local.json"
CONFIG = Path(__file__).resolve().parent / "meshy.local.json"   # git-ignored key file
WARN_FRACTION = 0.10


def load_state(path: Path = STATE) -> dict:
    if path.exists():
        return json.loads(path.read_text(encoding="utf-8"))
    return {"allotment": None, "log": []}


def save_state(state: dict, path: Path = STATE) -> None:
    path.write_text(json.dumps(state, indent=1), encoding="utf-8")


def api_key(config: Path | None = None) -> str:
    """The Meshy key: env MESHY_API_KEY first, else the git-ignored config file. Raises if neither."""
    key = os.environ.get("MESHY_API_KEY", "").strip()
    if key:
        return key
    cfg = Path(config) if config is not None else CONFIG
    if cfg.exists():
        key = str(json.loads(cfg.read_text(encoding="utf-8")).get("api_key", "")).strip()
        if key:
            return key
    raise RuntimeError(f"no Meshy key: set MESHY_API_KEY or put {{\"api_key\": \"...\"}} in {cfg.name} "
                       "(git-ignored; never commit it)")


def fetch_balance() -> int:
    fake = os.environ.get("MESHY_CREDITS_FAKE_BALANCE")
    if fake is not None:
        return int(fake)
    key = api_key()
    req = urllib.request.Request(API_URL, headers={"Authorization": f"Bearer {key}"})
    with urllib.request.urlopen(req, timeout=20) as r:
        return int(json.load(r)["balance"])


def evaluate(balance: int, allotment: int | None) -> tuple[float | None, bool]:
    """Fraction of the allotment left, and whether it is below the warning threshold."""
    if not allotment or allotment <= 0:
        return None, False
    frac = balance / allotment
    return frac, frac < WARN_FRACTION


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--set-allotment", type=int, help="credits that count as 100 %%")
    ap.add_argument("--label", default="", help="tag for this reading (e.g. the job about to run)")
    ap.add_argument("--history", action="store_true")
    ap.add_argument("--state", type=Path, default=STATE, help=argparse.SUPPRESS)
    a = ap.parse_args(argv)
    state = load_state(a.state)

    if a.history:
        prev = None
        for e in state["log"][-20:]:
            d = "" if prev is None else f"  ({e['balance'] - prev:+d})"
            print(f"{e['time']}  {e['balance']:>8}{d}  {e.get('label', '')}")
            prev = e["balance"]
        return 0

    if a.set_allotment is not None:
        state["allotment"] = a.set_allotment
    try:
        bal = fetch_balance()
    except Exception as e:  # noqa: BLE001
        print(f"meshy_credits: ERROR {e}", file=sys.stderr)
        return 1
    if state["allotment"] is None:
        state["allotment"] = bal          # first reading defines 100 % until set explicitly
    prev = state["log"][-1]["balance"] if state["log"] else None
    state["log"].append({"time": dt.datetime.now().isoformat(timespec="seconds"), "balance": bal, "label": a.label})
    save_state(state, a.state)

    frac, low = evaluate(bal, state["allotment"])
    used = "" if prev is None else f" | since last reading: {bal - prev:+d}"
    pct = "?" if frac is None else f"{100 * frac:.1f}%"
    print(f"Meshy credits: {bal} of {state['allotment']} ({pct}){used}")
    if low:
        print(f"WARNING: Meshy credits below {int(WARN_FRACTION * 100)}% of the allotment — tell the owner before spending more.")
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
