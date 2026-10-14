# Phyxel agent workflow

Phyxel is a C++17/Vulkan engine with a standalone-game SDK and a local MCP
bridge. The production outcome is a playable packaged executable, not merely a
valid `game.json` or a successful editor preview.

## Build and test

- Use the Phyxel MCP `build_project` tool for engine builds. From a shell-only
  agent, `python tools/call_phyxel_mcp.py build_project` invokes that same tool.
- Run focused Python contracts with `python -m pytest tests/<test>.py -q`.
- Never claim a game is finished until its standalone executable passes
  `tools/smoke_test_standalone.py`: boot, rendered chunks/faces, menu-to-gameplay,
  injected movement, and orderly shutdown.
- Preserve unrelated work in this repository. Do not reset or overwrite a dirty
  worktree.

## End-to-end game production

```powershell
python tools/produce_game.py MyGame `
  --project-dir path/to/MyGame `
  --definition path/to/game.json `
  --output path/to/dist/MyGame
```

The definition is required only when scaffolding a new project. The command
configures, builds Release by default, packages runtime dependencies, launches
the real game with its localhost-only test API, and fails on a broken stage.

## Runtime/package contract

- Generated games subclass `GameShell` and expose test hooks through it.
- `--test [port]` is automation-only and must not be used for player launches.
- A package includes the linked Python runtime, compiled shaders, registries,
  authoritative texture sources, fonts/audio, referenced animation/template
  assets, configs, and any pre-baked world DB.
- Missing referenced or boot-critical assets are packaging errors, not warnings.
- Use Release for distributables. Keep MCP/editor tooling out unless explicitly
  requested with a development packaging option.

See `CLAUDE.md` for architecture and the MCP catalog, and
`docs/GameCreationGuide.md` for the authoring model.
