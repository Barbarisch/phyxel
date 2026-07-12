# Recipe: farming / survival loop

**Satisfies milestones:** `gathering_crafting`, `needs_model` (partial), `core_loop` (survival),
plus content toward `resource_economy`. **Genre:** survival.

The task-level "how" for a Stardew-ish gather→craft→consume loop. Recipes are the layer beneath the
`phyxel-gamedev:*` domain skills — read `phyxel-mechanics` and `phyxel-world` for the tool details;
this is the ordered playbook. Confirm exact MCP tool names/params against the live list
(`list_items`, `list_templates`, etc.) — they evolve.

## 1. Design-first (GAMEPLAN.md)
In **Mechanics**, specify precisely: the resource nodes (what's harvested, tool required, respawn
time), the recipes (inputs→output), and the consumption sink (what `needs_model` drains and what
restores it). In **Systems-Interaction Matrix**, add the rows this loop needs, e.g.
`hunger=0 × time → health drains`, `crop × water → grows`, `crop × harvest-tool → yields`.

## 2. Assets as game objects (not raw art)
Crops are a **stateful prop-sequence**: seed → sprout → grown → harvested. The milestone is done only
when **every state exists and is wired to its transition** (§4c). Author the states (templates /
BlockSmith) and confirm each has collision/anchor metadata, not just a mesh.

## 3. Build the loop
- **Harvest:** placeable resource nodes; on interact, `give_item` the yield and swap the node to its
  depleted state (start a respawn timer via a trigger).
- **Craft:** wire the `CraftingSystem` recipes (data-driven — add to the project's recipe data);
  surface a crafting menu (`create_menu` / `add_menu_element`) or a hotbar-driven craft.
- **Consume:** hook item-use to `needs_model` (restore hunger/thirst); drain over time.
- Track loop progress with `add_objective` for the tutorial beat ("harvest 3, craft 1, eat").

## 4. Resource-loop-closure check (survival softlock guard)
For **every** consumable the player *needs*, confirm a **renewable, reachable source** exists. A sink
without a source is a survival softlock. List the resource graph in GAMEPLAN and verify no dead-ends —
this is the `resource_economy` L3 check (§10.3).

## 5. Validate (red-before-green)
- L2: recipes resolve (inputs/outputs reference real items); crop states all present.
- L3: drive the engine — harvest yields, craft consumes inputs & produces output, eating restores a
  need; a crop advances through its states on the intended trigger. Capture evidence
  (`screenshot` / `get_inventory` / `get_npc_needs` or the player-needs equivalent).
- Record via the tracker: set `gathering_crafting` → `validated:L3`, bump `resource_economy`, update
  `content.recipes.current`.

## 6. Feel pass (Axis C)
Sound on harvest/craft/eat; a pickup pop + particle; easing on the crop-grow transition. Set
`core_loop.feel = passed` only after this.
