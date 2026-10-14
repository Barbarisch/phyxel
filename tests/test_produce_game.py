"""Contract tests for the one-command production orchestrator."""

import json
import sys
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import produce_game  # noqa: E402


def test_missing_project_requires_definition(tmp_path):
    result = produce_game.produce(
        "MissingGame",
        tmp_path / "project",
        tmp_path / "dist",
        run_smoke=False,
    )
    assert not result["success"]
    assert not result["stages"]["scaffold"]["success"]


def test_pipeline_stops_when_configure_fails(tmp_path, monkeypatch):
    project = tmp_path / "project"
    project.mkdir()
    (project / "CMakeLists.txt").write_text("fixture", encoding="utf-8")

    monkeypatch.setattr(
        produce_game,
        "find_cmake",
        lambda: "cmake",
    )
    monkeypatch.setattr(
        produce_game,
        "_run",
        lambda command, cwd: {
            "success": False,
            "returncode": 1,
            "command": command,
            "output_tail": "configure failed",
        },
    )
    result = produce_game.produce(
        "BrokenGame", project, tmp_path / "dist", run_smoke=False
    )
    assert not result["success"]
    assert result["stages"]["configure"]["output_tail"] == "configure failed"
    assert "build" not in result["stages"]


def test_new_project_scaffold_receives_definition(tmp_path, monkeypatch):
    definition = tmp_path / "game.json"
    definition.write_text(json.dumps({"name": "NewGame"}), encoding="utf-8")
    observed = {}

    def fake_create(name, project, root, game_definition):
        observed.update(name=name, definition=game_definition)
        project.mkdir(parents=True)
        (project / "CMakeLists.txt").write_text("fixture", encoding="utf-8")

    monkeypatch.setattr(produce_game.create_project, "create_project", fake_create)
    monkeypatch.setattr(produce_game, "find_cmake", lambda: "cmake")
    monkeypatch.setattr(
        produce_game,
        "_run",
        lambda command, cwd: {
            "success": False,
            "returncode": 1,
            "command": command,
            "output_tail": "expected stop",
        },
    )
    result = produce_game.produce(
        "NewGame",
        tmp_path / "project",
        tmp_path / "dist",
        definition_path=definition,
        run_smoke=False,
    )
    assert observed == {"name": "NewGame", "definition": {"name": "NewGame"}}
    assert result["stages"]["scaffold"]["created"]
