# Recipe: RPG quest chain (with dialogue + skill checks)

**Satisfies milestones:** `quests`, `dialogue` (partial), content toward `campaign_structure`.
**Genre:** rpg. Phyxel already ships the quest/dialogue/skill-check systems — this recipe **wires**
them into a coherent, completable chain, it doesn't implement them.

Read `phyxel-characters` (dialogue → story-state, arcs/beats) for tool details; this is the ordered
playbook. Confirm tool names against the live list (`add_objective`, `complete_objective`,
`start_dialogue`, `load_dialogue_file`, `add_trigger`, `story_*`, `check_dc`, …).

## 1. Design-first (GAMEPLAN.md)
In **Story & Setting** / **Milestone Notes**, write the chain as a graph: quests, steps, the NPC(s)
per step, branch points, skill-check gates (which ability/DC), and rewards. Note the **failure
branches** too — an RPG quest without a "you failed / walked away" path softlocks.

## 2. Author the quest chain
- One arc via `story_add_arc`; beats per quest (`story` beats / variables track progress).
- Per step: an objective (`add_objective`) that a trigger `complete_objective`s on the step's
  condition (talk to X, bring item Y, reach Z).
- Rewards on completion: `give_item` / XP / currency / `set_npc_opinion` / reputation.

## 3. Dialogue + skill checks
- Author dialogue trees (`load_dialogue_file` / `set_npc_dialogue`); branch on story variables so
  earlier choices show/hide options (consequence, not cosmetic branching).
- Skill-check nodes: resolve a `check_dc` against the right ability modifier (DialogueSkillCheck);
  **both** outcomes must lead somewhere (success path AND failure path) — never a dead node.

## 4. Completability check (the RPG softlock guard)
Prove the chain is finishable from every reachable state (§9): a required NPC that can be killed, a
quest item that can be sold/dropped, or a one-way area that strands a needed target are the classic
softlocks. Either guard them (essential NPCs, re-obtainable items) or add a recovery path. Add matrix
row `quest-item × lost/consumed → still completable (or re-obtainable)`.

## 5. Validate (red-before-green)
- L2: every objective/trigger references a real target; every dialogue branch resolves; no dead skill
  nodes.
- L3: drive the chain end-to-end in-engine — advance each step, take a skill-check **both** ways,
  confirm rewards land and the arc closes. Capture `get_objectives` / `get_dialogue_state` /
  `story_get_state` evidence.
- Record: `quests` → `validated:L3`; bump `dialogue`; update `content.quests.current` /
  `content.dialogue_nodes.current`.

## 6. Content bar
One completable quest proves the *system*; `content_complete` needs the **count** (see the genre
template's `content_targets`, e.g. 20 quests). Log the running count as you author more.
