# Real-Time Combat AI — cover, chain of command, intelligence

The real-time combat stack (`CombatBehavior` for melee, `RangedCasterBehavior` for
casters) is what drives large battles where there are no turns and no initiative
order — every combatant thinks for itself, every frame, at whatever cadence its
intelligence affords it.

This document covers the **tactical layer** added on top of the melee FSM: line of
sight and cover, a chain of command, and intelligence as the dial that ties them
together. For the turn-based encounter system see
[`TurnBasedCombat.md`](TurnBasedCombat.md).

---

## 1. Why this exists

Without a tactical layer, 200 combatants are 200 independent agents that happen to
share a faction. Every one of them charges the nearest enemy, and the battle is a
uniform grind with no shape to it — no line that holds, no flank that turns, no
squad that breaks. The measurement that proves this is `/api/rpg/tactics`: with the
layer off, **100% of live melee fighters report the `engage` intent, from the first
sample to the last** (see §6).

Three things were added, and they are deliberately separable:

| Piece | Lives in | Answers |
|---|---|---|
| `AI::TacticalSpace` | `engine/{include,src}/ai/TacticalSpace.*` | *Can I see them? Where can I stand so they can't see me?* |
| `AI::CommandStructure` | `engine/include/ai/CommandStructure.h` | *Who gives orders, who takes them, what happens when the officer dies?* |
| Intelligence | `CombatBehavior::setIntelligence` | *How well does this particular soldier use the other two?* |

---

## 2. TacticalSpace — line of sight and cover

Pure functions over `ChunkManager`; no state, no ownership, safe to call from any
behavior. It was extracted from an inline voxel walk that lived inside
`PatrolBehavior`, so LOS is now defined once for the whole engine.

```cpp
bool  hasLineOfSight(cm, from, to, step = 0.5f);
bool  canSee(cm, fromFeet, toFeet);           // adds kEyeHeight = 1.6 to both
bool  isStandable(cm, pos, bodyHeight = 2.0f);
float groundHeight(cm, x, z, fromY, maxDrop = 24.0f);
CoverSpot findCover(cm, origin, threat, searchRadius = 12, rings = 3, samples = 12);
```

`findCover` samples rings of candidate positions around the seeker, discards any
that are not standable, discards any the **threat can still see**, and scores the
survivors by how little running they cost plus a bonus for being on the far side of
the obstacle from the threat. It returns `found = false` when the ground offers
nothing — in the open, taking cover is not an option, and the behavior must
degrade gracefully rather than run to an imaginary rock.

**Cost note.** One `findCover` call is ~36 candidates × (a ground probe + a LOS
walk) ≈ 2k voxel queries. That is why callers gate it behind a cooldown
(`CombatBehavior` uses 2.5 s) rather than searching every frame.

---

## 3. CommandStructure — squads, officers, orders

```cpp
enum class Order { Advance, Hold, Flank, FallBack, Regroup };
```

The engine owns the **structure** (squads, membership, the leader), the
**propagation** (an order applies to every member), and the **degradation**
(`notifyDeath` on the officer sets `leaderless`, and the squad keeps its last
order but stops receiving new ones — a headless squad fights on, badly).

The engine has **no opinion about tactics**. Which order an officer issues is a
host-supplied `Doctrine` callback:

```cpp
using Doctrine = std::function<Order(const SquadSituation&)>;
```

`SquadSituation` is the snapshot the host builds from its own entities: how many
are alive out of the starting strength, the squad's average health fraction, how
many hostiles are near, the distance to the nearest one, and both centres of mass.
Officers re-evaluate on a cadence (`setDecisionInterval`, default 3 s) — real
officers do not re-plan every frame and neither should twenty of them.

This is the seam that keeps the engine/game split honest: **a game changes how its
armies fight by swapping one function, not by editing the engine.** The scaffolded
doctrine in `tools/create_project.py` is a starting point, not a rule:

```
shattered (≤34% alive) or badly bloodied (<30% hp) → FallBack
hurt but holding (≤60% alive or <55% hp)          → Hold
strong and the enemy is close                     → Flank
otherwise                                          → Advance
```

**Authoring.** In `game.json`, an NPC joins a squad with `"squad": "<id>"` and
leads it with `"rank": "officer"`. Squads are created on first mention, so
authoring order does not matter. A squad's **rally point** — where `FallBack`
sends it — is computed as the running mean of its members' spawn positions, i.e.
the ground it formed up on. (Left unset it would be world origin, and every
retreat would march to the corner of the map.)

---

## 4. Intelligence — the dial

`"intelligence": 3..18` (D&D INT). It is **not** a damage stat. It changes *how a
soldier thinks*, along four axes:

| Derived from INT | INT 3 | INT 10 | INT 18 |
|---|---|---|---|
| `reactionDelay()` — seconds between tactical re-evaluations | 1.4 s | 0.82 s | 0.15 s |
| `coverDiscipline()` — chance it actually breaks line of sight when hurt | 0 | 0.33 | 0.95 |
| `obedience()` — chance it follows the squad order rather than free-lancing | 0 | 0.43 | 1.0 |
| target choice | nearest foe | nearest foe | weights wounded enemies (INT ≥ 12) |

The reaction delay is what makes low-INT troops look *committed and stupid*
rather than merely slow: between evaluations the previous intent stands, so a dull
soldier keeps charging for well over a second after the situation has changed.
At INT 3 cover discipline and obedience both clamp to zero, which is precisely why
INT 3 + no squads is the control arm of the A/B in §6.

---

## 5. Intents

`CombatBehavior::intentName()` reports what the fighter is currently doing:

| Intent | Behaviour |
|---|---|
| `engage` | close with the nearest hostile and fight (the only intent without the tactical layer) |
| `cover` | move to a `TacticalSpace` spot the target cannot see; movement owns the frame |
| `hold` | stand the ground held when the order landed — gives up at most 2.5 u of it |
| `flank` | approach on an arc (forward + sideways) instead of straight down the enemy's front |
| `fall_back` | run for the squad rally point instead of fighting |

⚠️ `hold` was initially a **label with no behaviour** — the approach branch still
charged, so the telemetry reported "holding" while the soldier ran. It now anchors
on the position it occupied when the order landed. Any new intent must change what
the behavior *does*, or the telemetry lies.

---

## 6. Verification — the A/B that proves it

Claims like "they take cover now" describe an invisible internal state, so they
need a measurement that **can come back negative**. `POST /api/rpg/tactics`
returns the live distribution of intents across every melee fighter, per faction,
plus `tactical_fraction` (the share not merely engaging).

Two runs of the same battle on the same binary, same terrain
(`scratchpad/tactics_probe.py`, generator `scratchpad/build_battlesim.py`):

- **Control** (`BATTLE_TACTICS=0`) — no squads, no officers, every soldier INT 3.
- **Treatment** (`BATTLE_TACTICS=1`) — squads of 20 under officers, INT 8/11/14.

The control result is the load-bearing half: if the control also showed tactical
intents, a green treatment would prove nothing.

Cover only means something if there is something to stand behind, so the
battlefield carries a ruined wall down the centre (with gaps to fight through),
scattered rock outcrops, and two hillocks worth holding.

**Unit tests** (`tests/ai/TacticalSpaceTest.cpp`, `tests/ai/CommandStructureTest.cpp`,
19 cases) pin the geometry and the command mechanism deterministically. The cover
search was **mutation-checked**: deleting the line-of-sight filter from `findCover`
makes `OpenGroundOffersNoCover` fail. Worth knowing that
`FindsCoverBehindAWallAndItActuallyCovers` does *not* catch that mutation — with a
wall present the scoring picks a covered spot regardless — so the open-ground
control is the load-bearing test, not the one whose name sounds like it.

### Measured, 2026-08-31 (200 combatants, Release, 7 samples over 81 s)

| | Control | Treatment |
|---|---|---|
| intents observed | `engage` only | `engage`, `cover`, `flank`, `hold` |
| tactical fraction | **0.000 at every sample** | 0.28 – 0.75 |
| cover decisions (cumulative) | **0** | **185** |
| orders obeyed (cumulative) | **0** | **1798** |
| squad order changes | 0 | 13 — `flank` ×6, `advance` ×5, `hold` ×2 |

Every control counter is exactly zero, on every sample, for both factions. The
control was run three times across three builds and came back zero each time.

**Intelligence shows up as behaviour, not as a stat.** Crimson's line is drilled to
INT 11, azure's to INT 8; nothing else differs between them.

| | crimson (INT 11) | azure (INT 8) |
|---|---|---|
| cover taken | 157 | 28 |
| cover denied | 0 | 3 |
| orders obeyed / ignored | 1614 / 1499 | 184 / 359 |
| **measured obedience** | **51.8%** | **33.9%** |
| predicted obedience (incl. the 3 INT-14 officers per side) | 51.1% | 30.7% |

Measured obedience lands within ~3 points of what `obedience()` predicts, which is
a real validation of the model rather than a restatement of it. The cover gap is
*larger* than `coverDiscipline()` alone predicts (5.6× measured vs 2.5× derived)
because intelligence compounds: a sharper soldier also re-evaluates more often, so
it gets more chances to notice it should move, and it survives longer to take them.

`cover_denied` was 0 for crimson and 3 for azure — this battlefield has ample
cover, so a low cover count would have meant the layer wasn't firing, not that the
ground was bare.

⚠️ Crimson also *won* that battle (the only survivors at t06). That is **one
sample** and is not evidence that better-led troops win — only the behavioural
difference is measured here. A win-rate claim needs many seeded runs.

**`fall_back` was never issued, and the reason is the design working.** All 15
orders landed in the first 47 s; the last one fired 34 s before the battle ended.
By the time squads were shattered enough to meet the FallBack threshold their
officers were already dead, so the squads were `leaderless` and no longer
receiving orders at all. The armies ground each other down under stale `hold` and
`advance` orders because there was nobody left to call the retreat.

That is coherent — and it raises a real design question rather than settling one:
a leaderless squad currently keeps its last order forever. Whether it should
instead revert to individual instinct, or promote a surviving member, is an open
call (see §7).

---

### Reading the telemetry honestly

`tactical_fraction` is an **instantaneous census** and it undercounts cover
badly, because moving to cover is a *transit* state that ends the moment the
fighter arrives. A poll every 10 s catches almost none of them. `/api/rpg/tactics`
therefore also returns cumulative per-faction tallies — `cover_taken`,
`cover_denied`, `orders_obeyed`, `orders_ignored` — which count **decisions**, and
that is what the question "does the tactical layer fire?" actually asks. The
tallies include the dead: a soldier who took cover and then fell still took cover,
and dropping them would bias the count toward whichever side is winning.

`cover_denied` is worth watching on its own. It separates *the layer never fires*
from *the layer fires and this ground has nothing to hide behind* — two very
different bugs that look identical in the intent census.

## 7. Open items

- Casters (`RangedCasterBehavior`) do not use `TacticalSpace` yet — they hold
  range and back off, but do not seek line-of-sight breaks. They are the obvious
  next consumer.
- **Succession.** A `leaderless` squad keeps its last order indefinitely, which in
  the measured battle meant nobody ever called a retreat (see §6). The options are
  promoting a survivor, degrading to individual instinct after a delay, or leaving
  it as is because a headless unit *should* be paralysed. Not yet decided.
- `Regroup` is defined in the `Order` enum but no intent implements it.
- `FallBack` has never been observed firing in a live battle for the reason above,
  so its behaviour (running to the rally point) is unproven at L4 — only the
  rally-point *derivation* is pinned.
- Squad orders are visible only in the game log (`Command: squad '<id>' (<faction>)
  -> <order>`); there is no API for the live order per squad.
- `findCover` searches a flat ring pattern; it has no notion of holding the high
  ground the hillocks provide.
