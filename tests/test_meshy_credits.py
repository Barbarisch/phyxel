"""Roadmap R2: the Meshy credit watcher warns below 10 % of the allotment (owner rule 2026-10-01)."""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import meshy_credits as mc  # noqa: E402


def run(tmp_path, monkeypatch, balance, *args):
    monkeypatch.setenv("MESHY_CREDITS_FAKE_BALANCE", str(balance))
    return mc.main(["--state", str(tmp_path / "s.json"), *args])


def test_threshold_is_exactly_ten_percent():
    assert mc.evaluate(100, 1000) == (0.1, False)       # at 10 % — not yet below
    assert mc.evaluate(99, 1000)[1] is True             # below 10 % — warn
    assert mc.evaluate(500, None) == (None, False)      # no allotment — cannot judge


def test_first_reading_defines_the_allotment_and_later_readings_warn(tmp_path, monkeypatch, capsys):
    assert run(tmp_path, monkeypatch, 4000) == 0
    assert run(tmp_path, monkeypatch, 450, "--label", "batch") == 0      # 11.25 %
    assert run(tmp_path, monkeypatch, 390) == 2                          # 9.75 % -> WARN exit
    out = capsys.readouterr().out
    assert "WARNING" in out and "-60" in out                              # delta since last reading


def test_explicit_allotment_overrides(tmp_path, monkeypatch):
    assert run(tmp_path, monkeypatch, 500, "--set-allotment", "10000") == 2   # 5 %
    assert run(tmp_path, monkeypatch, 500, "--set-allotment", "1000") == 0    # 50 %


def test_missing_key_is_an_error_not_a_silent_pass(tmp_path, monkeypatch):
    monkeypatch.delenv("MESHY_CREDITS_FAKE_BALANCE", raising=False)
    monkeypatch.delenv("MESHY_API_KEY", raising=False)
    monkeypatch.setattr(mc, "CONFIG", tmp_path / "absent.json")   # a real key file must not leak in
    assert mc.main(["--state", str(tmp_path / "s.json")]) == 1


def test_key_comes_from_env_first_then_the_ignored_config(tmp_path, monkeypatch):
    import json
    cfg = tmp_path / "meshy.local.json"
    cfg.write_text(json.dumps({"api_key": "cfg-key"}), encoding="utf-8")
    monkeypatch.delenv("MESHY_API_KEY", raising=False)
    assert mc.api_key(cfg) == "cfg-key"
    monkeypatch.setenv("MESHY_API_KEY", "env-key")
    assert mc.api_key(cfg) == "env-key"


def test_the_key_file_is_git_ignored():
    import subprocess
    root = Path(__file__).resolve().parents[1]
    r = subprocess.run(["git", "check-ignore", "-q", "tools/meshy.local.json"], cwd=root)
    assert r.returncode == 0, "tools/meshy.local.json must be git-ignored"
