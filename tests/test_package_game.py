"""Hermetic contract tests for the standalone game packager."""

import json
import sys
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parent.parent / "tools"
sys.path.insert(0, str(TOOLS_DIR))

import package_game  # noqa: E402


def _write(path: Path, contents: bytes = b"fixture") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(contents)


def test_package_project_produces_runnable_layout(tmp_path, monkeypatch):
    """A built project packages without reaching into a real Phyxel checkout."""
    engine_root = tmp_path / "engine"
    project = tmp_path / "project"
    output = tmp_path / "dist"

    monkeypatch.setattr(package_game, "PHYXEL_ROOT", engine_root)
    monkeypatch.setattr(package_game, "REQUIRED_SHADERS", ["world.vert.spv"])
    monkeypatch.setattr(package_game, "REQUIRED_RESOURCE_DIRS", [])
    monkeypatch.setattr(
        package_game,
        "REQUIRED_RESOURCES",
        ["resources/materials.json"],
    )

    _write(engine_root / "shaders" / "world.vert.spv")
    _write(engine_root / "resources" / "materials.json", b"{}")
    _write(engine_root / "resources" / "textures" / "source" / "stone.png")
    _write(project / "build" / "Debug" / "ContractGame.exe", b"binary")
    python_root = tmp_path / "python"
    python_lib = python_root / "libs" / "python312.lib"
    _write(python_lib)
    _write(python_root / "python312.dll", b"python-runtime")
    _write(
        project / "build" / "CMakeCache.txt",
        f"PYTHON_LIBRARIES:INTERNAL={python_lib.as_posix()}\n".encode("utf-8"),
    )
    _write(project / "worlds" / "default.db", b"sqlite")

    game_definition = {
        "name": "Contract Game",
        "world": {"type": "Flat"},
    }
    _write(project / "game.json", json.dumps(game_definition).encode("utf-8"))
    _write(
        project / "engine.json",
        json.dumps({"window": {"title": "Contract Game"}}).encode("utf-8"),
    )

    result = package_game.package_game(
        name="ContractGame",
        output_dir=output,
        project_dir=project,
    )

    assert result["success"], result
    assert (output / "ContractGame.exe").read_bytes() == b"binary"
    assert (output / "python312.dll").read_bytes() == b"python-runtime"
    assert (output / "shaders" / "world.vert.spv").exists()
    assert (output / "resources" / "materials.json").exists()
    assert (output / "resources" / "textures" / "source" / "stone.png").exists()
    assert json.loads((output / "game.json").read_text(encoding="utf-8")) == game_definition
    assert json.loads((output / "engine.json").read_text(encoding="utf-8"))["window"]["title"] == "Contract Game"
    assert (output / "worlds" / "default.db").read_bytes() == b"sqlite"
    assert (output / "Play ContractGame.bat").exists()
    assert (output / "README.md").exists()


def test_package_includes_every_compiled_runtime_shader(tmp_path, monkeypatch):
    engine_root = tmp_path / "engine"
    project = tmp_path / "project"
    monkeypatch.setattr(package_game, "PHYXEL_ROOT", engine_root)
    monkeypatch.setattr(package_game, "REQUIRED_SHADERS", ["base.spv"])
    monkeypatch.setattr(package_game, "REQUIRED_RESOURCE_DIRS", [])
    monkeypatch.setattr(package_game, "REQUIRED_RESOURCES", [])
    _write(engine_root / "shaders" / "base.spv")
    _write(engine_root / "shaders" / "future_pipeline.spv")
    _write(engine_root / "resources" / "textures" / "source" / "stone.png")
    _write(project / "build" / "Debug" / "ShaderGame.exe")
    python_root = tmp_path / "python"
    python_lib = python_root / "libs" / "python312.lib"
    _write(python_lib)
    _write(python_root / "python312.dll")
    _write(project / "build" / "CMakeCache.txt",
           f"PYTHON_LIBRARIES:INTERNAL={python_lib.as_posix()}\n".encode())

    result = package_game.package_game("ShaderGame", tmp_path / "dist", project_dir=project)
    assert result["success"], result
    assert (tmp_path / "dist" / "shaders" / "future_pipeline.spv").exists()


def test_package_fails_when_required_runtime_resource_is_missing(tmp_path, monkeypatch):
    """Strict runtime inputs are errors, not successful-but-broken packages."""
    engine_root = tmp_path / "engine"
    project = tmp_path / "project"

    monkeypatch.setattr(package_game, "PHYXEL_ROOT", engine_root)
    monkeypatch.setattr(package_game, "REQUIRED_SHADERS", [])
    monkeypatch.setattr(package_game, "REQUIRED_RESOURCE_DIRS", [])
    monkeypatch.setattr(
        package_game,
        "REQUIRED_RESOURCES",
        ["resources/required.json"],
    )

    _write(project / "build" / "Debug" / "BrokenGame.exe", b"binary")
    python_root = tmp_path / "python"
    python_lib = python_root / "libs" / "python312.lib"
    _write(python_lib)
    _write(python_root / "python312.dll", b"python-runtime")
    _write(
        project / "build" / "CMakeCache.txt",
        f"PYTHON_LIBRARIES:INTERNAL={python_lib.as_posix()}\n".encode("utf-8"),
    )

    result = package_game.package_game(
        name="BrokenGame",
        output_dir=tmp_path / "dist",
        project_dir=project,
    )

    assert not result["success"]
    assert "Required resource missing: resources/required.json" in result["errors"]
