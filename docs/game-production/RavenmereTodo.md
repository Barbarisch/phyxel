# Ravenmere — the live TODO (baseline 2026-09-25)

The working list. `RavenmereGapLedger.md` stays the **history**: every gap ever found, its
evidence, and how it closed — commits, evidence files and code comments cite its `G-` ids and
those ids never change. This file is the **open set only**, renumbered `T-` for tracking; each
line points at its `G-` row for the full story. When an item ships, delete its line here and
close the `G-` row in the ledger. New gaps get the next `G-` id in the ledger AND a `T-` line
here.

How the baseline was drawn (2026-09-25): every ledger row whose latest status still read OPEN
was carried unless later work had shipped it; those closed on the way are listed at the bottom
with the evidence. Rows from the 2026-09-08 census that nobody has re-checked since are marked
*(census, unverified)* — some may be moot by now.

## A. Player-visible in the shipped game

| T | G | Sev | Item | Next step |
|---|---|---|---|---|
| T-03 | G-163 | S2 | An empty lot with an orphaned sign bracket | Find which pass places the bracket and what it keys off; find why the lot is empty (a silently failed build is the interesting bug). |
| T-04 | G-167 | S2 | A build clears half a tree | Clearing must operate on the tree as an object, not the cells in a box. Test: build over a tree, assert zero orphaned tree voxels. |
| T-05 | G-159 | S2 | NPC and player colours flicker between palettes | Reproduce first: park the camera, record N frames, diff pixels; then bisect by freezing the 32-light set. |
| T-06 | G-119 | S2 | The transition to Hollin Farm is not signposted | Wayfinding without markers (G-51 is the design rule): a road, a sign, a line of dialogue. |
| T-07 | G-181 | S2 | Load times | Town visit is 2.1-2.4 s load + 1.2-1.7 s hold after four increments; the 2 s load line is met on the first visit, open on revisits. Levers left: faces in the finalize (0.57 s), decode (0.65 s), NavGraph (0.55 s). |
| T-08 | G-126 | S3 | Nameplates draw over HUD panels | Seen again 2026-09-24 (Wynn's nameplate over the combat hint). World labels are outside the HUD lint; either clip them against HUD panels or lint them too. |
| T-09 | G-114 | S2 | No splash while the exe boots | The wait was fixed (atlas cache); the splash itself is not drawn. |
| T-10 | G-160 | S3 | No sprint control | Small; unblocks the sprint-gated strafe clips. |
| T-11 | G-180 | S2 | No music (menu, intro, town, ...) | Needs the asset-source decision first (local generation vs CC0 vs authored). Parked 2026-09-25 as too big for now. |

## B. RPG systems and content (the game itself)

| T | G | Sev | Item | Next step |
|---|---|---|---|---|
| T-12 | G-01 | S1 | Character creation | Class/race/ability pick at game start (`character_creation` milestone). *(census, unverified)* |
| T-13 | G-04 | S1 | Control companions in combat | Select, order or switch to a companion on its turn. *(census)* |
| T-14 | G-51 | S1 | Wayfinding design | User's rule: NO quest markers or arrows; exploration through the world and dialogue. Stays as the constraint for T-06. |
| T-15 | G-22 | S1 | The generated town's residents ship with the town | 14 scheduled residents in the shipped build (`living_town` milestone). *(census)* |
| T-16 | G-37 | S2 | A finite inventory and an inventory screen | The material hotbar is gone (G-104/G-106); the finite inventory model and its screen are not built. Folds the inventory part of G-08. |
| T-17 | G-08 | S2 | Level-up choice screen | The rest of G-08 shipped (sheet G-133, journal G-168, spellbook G-106, party G-161); the level-up choice did not. |
| T-18 | G-09 | S2 | Buy/sell with a merchant, currency checks in dialogue | *(census)* |
| T-19 | G-07 | S2 | Conditions on triggers | Fire only if a story flag holds. *(census)* |
| T-20 | G-24 | S2 | Compound dialogue conditions | "tracks found AND not yet reported". *(census)* |
| T-21 | G-30 | S2 | The pre-made cleric has cleric stats | And G-134: class proficiencies applied on load. *(census)* |
| T-22 | G-43 | S2 | The authored cleric survives a CR-appropriate fight | And G-83: the farm wolves kill a level-1 party 4 deaths / 7 runs, 3x the SRD budget. Balance, not harness. |
| T-23 | G-58 | S2 | Choosing whom to talk to when two NPCs are near | *(census)* |
| T-24 | G-46 | S2 | A 1-voxel step should be walkable without a jump | Altar dais, stair riser. *(census)* |
| T-25 | G-151 | S3 | Items on the action bar cannot fire | Needs an item-use path (consumables at least). |
| T-26 | G-134 | S3 | Class proficiencies on the pre-made | Apply save proficiencies + authored skills on load. |

## C. Settlement and structure generation

| T | G | Sev | Item | Next step |
|---|---|---|---|---|
| T-27 | G-61 | S1 | The walkability gate must RUN in the settlement build | Plan written (`WalkabilityGateAndPlaytestLoop.md` §5). |
| T-28 | G-72 | S1 | Fence gaps at the actual door; spur meets the gate | Mitigated (approach bands cleared); the spur/gate placement itself is still wrong. |
| T-29 | G-15 | S1 | Clearing a strip of generated forest crashes the editor | And G-145 (`clear_region` crash in finalization). Symbolise the dump, guard the path. |
| T-30 | G-17 | S2 | Residents and authored NPCs standing on the ground | *(census)* |
| T-31 | G-19 | S2 | Civic buildings a story needs | Shrine/temple, town hall, jail/stocks typologies. *(census)* |
| T-32 | G-57 | S2 | Walls with shuttered windows must still be walls | Collision through the shutter. *(census)* |
| T-33 | G-59 | S2 | Alleys between adjacent buildings | Or the space behind a house is unreachable. |
| T-34 | G-73 | S2 | A bar needs a service aisle behind it | Tavern furnishing. |
| T-35 | G-74 | S2 | The public room should meet the entrance | Tavern typology. |
| T-36 | G-96 | S2 | Intermittent wedge at the taproom-kitchen door | Log NPC positions on a wedge; doorway-yield rule for NPCs. |
| T-37 | G-162 | S3 | A gap between a house front and its fence | |
| T-38 | G-67 | S3 | A flush trade sign must hang above head height | |
| T-39 | G-85 | S3 | Location marker in front of the street door | Gate mitigated; the forge is still wrong. |
| T-40 | G-16 | S3 | Honest resident count from `build_settlement` | *(census)* |
| T-41 | G-20 | S3 | Front doors face the main street | *(census)* |
| T-42 | G-125 | S3 | One wanderer holds a valid route and never advances | |

## D. Rendering and lighting

| T | G | Sev | Item | Next step |
|---|---|---|---|---|
| T-43 | G-146 | S1 | The ambient model costs 39-52 ms/frame in the town | Grass done; the remaining receivers are not. Measure per pass (`gpu_scopes`). |
| T-44 | G-144 | S2 | Ambient is wrong outside the probe grid | A far probe cascade instead of "assume open sky". |
| T-45 | G-142 | S2 | Grass under a roof reads ~20x brighter than the room | Measure `grass.frag`'s sun term vs shadowFactor under the lab roof. |
| T-46 | G-152 | S3 | Two unit tests fail for unrelated reasons | (a) AtlasManager depends on a disk cache (passes after the merge; re-check); (b) FineFaceMerge cross-cube light key. |

## E. Engine, scenes and pipeline

| T | G | Sev | Item | Next step |
|---|---|---|---|---|
| T-47 | G-84 | S1 | A scene whose DB yields 0 chunks must not spawn the player into the void | And G-66 (an empty DB must not kill the host), G-48 (engine half). One rule: refuse, report, fall back. |
| T-48 | G-80 | S1 | A transition trigger on a natural walking line | Authoring fixed; the engine gap (a way to say "interact to cross") is open; intermittent firing on watch. |
| T-49 | G-13 | S2 | Editor executes the same trigger/dialogue actions as the shipped game | *(census)* |
| T-50 | G-63 | S2 | The shipped game feeds NPC navigation the same non-voxel obstacles as the editor | Verify: `NPCManager::buildNavGrid` carries the obstacle overlay; is it fed in the shell? |
| T-51 | G-27 | S2 | Leaving the pause menu by keyboard; Enter must not re-trigger | Probably closed by G-179 (Escape leaves every menu); verify and close. |
| T-52 | G-175 | S2 | A panel's auto-size disagrees with where its children draw | Repeater rows fixed (G-184); `UIPanel` measure vs render advance still differ. |
| T-53 | G-155 | S2 | Pipeline-statistics queries crash the NVIDIA driver | Dev-only, off by default. Find the misuse or drop the queries. |
| T-54 | G-12 | S3 | Menu scene should be smooth | *(census)* |
| T-55 | G-23 | S3 | Building the game project should be quick | *(census)* |
| T-56 | G-11 | S3 | `phyxel link --genre rpg` seeds the RPG milestones | *(census)* |
| T-57 | G-39 | S3 | `LOG_INFO("{:.1f}")` must not be writable | Root cause in the logger. |
| T-58 | G-14 | S3 | `resources/rpg/` cited in CLAUDE.md / README | Docs. |

## G. Planning

| T | G | Sev | Item | Next step |
|---|---|---|---|---|
| T-71 | G-185 | S3 | A generic scroll container (any content, both axes, keyboard/gamepad) | Planning session first (user, 2026-09-25). The intro card scrolls today via a nested scrollable panel. |

## F. Harness, test API and diagnostics

| T | G | Sev | Item | Next step |
|---|---|---|---|---|
| T-72 | G-187 | S3 | `WorldForgeLiveApplyTest` stalls since 2026-09-25 evening (fails on the previous commit too) | Time one chunk's generation; rerun on a quiet machine. |
| T-59 | G-97 | S3 | `/api/walk/probe` reports `fits_at_start:false` everywhere | Check what it expects for `y`; unit test through the same path. |
| T-60 | G-149 | S3 | The harness cannot scroll the wheel or set the zoom | |
| T-61 | G-174 | S3 | `entity_damage` reports what was asked, not what landed | |
| T-62 | G-156 | S3 | `total_visible_faces` / `shadow_instances_drawn` are not per-view | Make them per-view or rename. |
| T-63 | G-176 | S3 | `detailedTiming.totalFrameTime` is meaningless in a shipped game | |
| T-64 | G-110 | S3 | `combat/screen_of` answers ok with pixels outside the viewport | |
| T-65 | G-28 | S3 | Party membership observable in the shipped build | *(census)* |
| T-66 | G-35 | S3 | The dialogue state machine documented for a harness | *(census)* |
| T-67 | G-40 | S3 | A pure "observe this event" trigger | *(census)* |
| T-68 | G-56 | S3 | Region boxes validated | *(census)* |
| T-69 | G-25 | S3 | `win_condition` static check finds the real terminal | *(census)* |
| T-70 | G-21 | S3 | Find ground height at a spot (authoring tool) | *(census)* |

## Closed in the 2026-09-25 cleanup (ledger rows updated)

| G | Why |
|---|---|
| G-08 | Superseded: sheet (G-133), journal (G-168), spellbook (G-106), party (G-161) shipped; the two unbuilt parts carried as T-16 and T-17. |
| G-10 | Shipped: projects get a unique `api_port` and `phyxel.exe --project` reads it (AgentContext, multi-instance rule). |
| G-49 | Shipped: the loading screen is held until the scene's chunks are meshed (G-115, refined by G-181 increment 4). |
| G-127 | Its own status: FIXED 2026-09-23 with G-177 (the occupancy race). |
| G-154 | Shipped with G-183: the layout gate lints every overlay screen with the alignment-aware rules, 0 defects. |
| G-161, G-171, G-172 | Their own status: FIXED 2026-09-23 (engine); the reports were half read at the time. |
| G-173 | Shipped with G-183: `visibleWhen` takes `&&`/`!` expressions, evaluated identically by the engine and the gate; malformed stays hidden (fail-closed) and the gate reads every term. |
| G-116 (T-01) | Shipped 2026-09-25: the intro story card gates the first scene of every run (button, Enter or Space); L4 `rv_intro_l4.py`. |
| G-164 (T-02) | Shipped 2026-09-25: a sun that crosses the southern sky (noon 50 deg, `sky.latitudeDeg`), a moon that is full on the first night, the `sky` block honoured in shipped games; L4 `rv_sky_l4.py`. |
| G-186 | Shipped 2026-09-25: moonless nights are dark but readable (a night-only ambient fill); L4 `rv_night_l4.py`. |
