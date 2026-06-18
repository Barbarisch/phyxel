---
description: Reconcile the forwarding surface (docs, skills, MCP tool descriptions, templates) with recent engine changes so future Claude sessions get accurate info.
---

Reconcile Phyxel's **forwarding surface** with recent engine changes. This is an **engine-dev**
task (run from the engine repo). The mechanical pre-push/CI gate (`tools/check_doc_sync.py`) only
checks that *something* in the surface was touched; this command does the **semantic** sync.

Steps:
1. Determine the change range — default `origin/main..HEAD` (or ask the user for a range / feature).
   `git diff --stat <range>` to see what engine code changed.
2. Open **`docs/ForwardingSurface.md`** — the map of *change type → downstream files to update*.
3. For each engine change in range, find the matching downstream surface and check it's current:
   - MCP commands/params → tool descriptions (`scripts/mcp/...` + handlers) + the relevant
     `phyxel-gamedev` skill + `docs/GameCreationGuide.md`.
   - UI/HUD/menus/dialogue → `docs/HudSystem.md` + `phyxel-mechanics` skill + the `scaffold.py`
     CLAUDE.md template.
   - new subsystem → `docs/<it>.md` + `AgentContext.md` roadmap + the matching skill.
   - build/flags/ports → `AgentContext.md` + `docs/GameDevWorkflow.md` + `phyxel-playtest`.
   - CLI/scaffold/packaging → `docs/GameDevWorkflow.md` + `scaffold.py` / `create_project.py` /
     `package_game.py` + `phyxel-package`.
   Watch for **stale specifics** (moved UI positions, renamed tools, "not yet implemented"
   headers, removed features) — those are the silent drift this exists to catch.
4. Apply the updates (concise, matching the surrounding style). Update `docs/AgentContext.md`'s
   roadmap if a workstream advanced.
5. Summarize what you changed and why. Don't commit unless the user asks. (If the gate flagged a
   push and a change genuinely needs no surface update, the author may instead add `[skip-docs]`
   to a commit message — call that out rather than inventing doc changes.)
