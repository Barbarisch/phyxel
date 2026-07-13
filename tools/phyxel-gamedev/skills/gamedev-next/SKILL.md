---
name: gamedev-next
description: Use to drive a Phyxel game toward shippable one milestone at a time — the guided production loop backed by the .phyxel/production.json tracker (orient → pick the next milestone → design-first → build → validate → record → checkpoint). Invoke when the user asks "what's next", wants to make progress / continue the game, or a session starts and wants to advance the project toward completion.
---

# Advancing a Phyxel game (the production loop)

This is the *consistent process* on top of the `production` MCP tool + the `.phyxel/production.json`
milestone tracker. It carries a game from empty project to shippable, **one milestone per pass**, and
keeps every session honest. Full design: `docs/game-production/README.md`.

## One pass of the loop

1. **Orient.** The SessionStart digest is already in context; else `production(op="status")`. It shows
   the stage, `done/total (%)`, the recommended **NEXT** milestones, ordering-critical nags, and the
   current focus. **Stale-first:** if anything shows `(!) STALE`, re-validate it before starting new work.

2. **Sweep for regressions.** If recent edits touched a milestone's inputs (game.json / GAMEPLAN.md),
   run `production(op="sweep")` — it flips rotted "done" milestones to `stale` (or self-heals ones still
   valid). Do this before piling on more work, so "done" never silently lies.

3. **Pick.** Take the user's directive; otherwise the recommended `NEXT`. Surface **ordering-critical**
   milestones (`accessibility`, `localization_ready`) early — they're un-retrofittable once strings/UI
   are authored.

4. **Design-first.** If the milestone is under-specified, update `GAMEPLAN.md` *before* building —
   especially mechanics (spec them precisely: "door slides on +X when the lever is pulled", not "make
   the door work"; vague specs burn tokens). Check `docs/game-production/recipes/` for a matching
   playbook (farming-survival-loop, rpg-quest-chain, turn-based-encounter, save-integrity, menus…).

5. **Build.** Use the domain skills — `phyxel-world`, `phyxel-characters`, `phyxel-mechanics`,
   `phyxel-assets` — following the recipe if one applies. Assets are "done" when they're wired game
   *objects* (collision/anchors), not raw art; stateful props need every state wired.

6. **Validate (red-before-green).** `production(op="validate", milestone=<name>)`. It runs the static
   L1/L2 checks (win-trigger wired, world/player present, GAMEPLAN filled) and — when the engine is
   running with this project — a runtime L3/L4 pass (e.g. the world actually renders). It auto-writes
   `validated`+`status`, honestly capping at what it can confirm (L4 milestones stay `in_progress` until
   a runtime validator/playtest confirms them). Never hand-mark "done" what a validator hasn't confirmed.

7. **Record.** Validate already wrote the state; add `evidence`/`note` and update the one-line focus:
   `production(op="set", focus="<current work>")`. For a milestone the tool can't auto-check (a real
   playtest, feel), set it deliberately with evidence — and stamp its feel: `feel="passed"` only after a
   juice pass (sound-on-input, easing, impact, coyote/buffer where there's movement).

8. **Checkpoint.** At stage boundaries — before `vertical_slice`→scale, before `content_complete` —
   surface to the user for a go/no-go; then `production(op="advance_stage")`. Prove one deep slice before
   breadth.

Repeat until required milestones complete → `phyxel-package` (packaging soft-warns on anything incomplete
or stale; `strictPackaging` blocks).

## Creator ≠ certifier ≠ tester
The session that *builds* a milestone should not self-certify L3+. Let `validate` (objective) confirm
what it can; for runtime/feel, verify by an **auditor pass** or a **fresh perspective**, not by the
builder's say-so. Red-before-green on every "works/done" claim.

## Target genres (survival · RPG/D&D · action-RPG)
Most mechanics already exist in the engine — the milestones here are wiring + content + balance, not
implementation. For systemic games, keep the **systems-interaction matrix** in GAMEPLAN.md current and
confirm **resource-loop-closure** (no needed consumable without a reachable source = a survival softlock).

## Log engine gaps
When a milestone needs an engine capability that doesn't exist yet (e.g. a specific screen, an item
type), log it with `/feedback` (or `phyxel feedback`) instead of hand-working around it — engine-dev
`/triage-feedback` folds it into the roadmap.

Pointers: `docs/game-production/README.md` (design) · `recipes/` (playbooks) · `milestone-schema.md`
(tracker fields) · `production-stages.md` (the stage gates).
