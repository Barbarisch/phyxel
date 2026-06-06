# phyxel-gamedev (Claude Code plugin)

Game-development workflow skills for the Phyxel voxel engine. Install once per machine; they
then load automatically (when relevant) in any game-project session — paired with the
per-project `phyxel` MCP server that `phyxel link` / `phyxel new` wire up.

## Skills

| Skill | Loads when you're… |
|-------|--------------------|
| `phyxel-playtest`   | building/launching/iterating — engine lifecycle + the verify loop |
| `phyxel-world`      | building terrain, structures, or scenes |
| `phyxel-characters` | adding the player, NPCs, dialogue, or story |
| `phyxel-assets`     | spawning templates or generating models (BlockSmith) |
| `phyxel-mechanics`  | adding health, objectives, music, day/night, combat, menus, … |
| `phyxel-package`    | saving, exporting, or building a standalone game |

## Install (one-time, per machine)

The engine repo doubles as a plugin **marketplace** (`<repo>/.claude-plugin/marketplace.json`).
In Claude Code:

```
/plugin marketplace add <path-to-phyxel-repo>
/plugin install phyxel-gamedev@phyxel
```

Then restart the session (or `/plugin`) so the skills register. Verify with `/help` or by
checking that the `phyxel-*` skills appear available.

> Schema/commands follow the current Claude Code plugin system; if the install flow differs in
> your version, point `/plugin marketplace add` at this repo and install `phyxel-gamedev`.
