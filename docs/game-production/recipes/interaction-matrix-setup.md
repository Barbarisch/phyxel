# Recipe: systems-interaction matrix setup

**Satisfies:** the `interaction_matrix` validation (§10.3) — the systemic-game centerpiece. **Genre:**
survival / rpg / any game where systems interact. This is where these games ship broken: each system
works alone, but the *combinations* are never checked.

## 1. Seed from the genre template
Your `GAMEPLAN.md` "Systems-Interaction Matrix" was pre-seeded from the genre template(s)
(`interaction_matrix_seed`). Start there — e.g. `rain x campfire -> extinguishes`,
`hunger=0 x time -> health drains`, `reputation(faction) x merchant -> prices change`.

## 2. Grow it as systems land
Every time you add a system, add a row for **each existing system it should touch**. The matrix is a
grid: N systems -> up to N x N interactions, most of which nobody remembers to wire. Prioritize the
pairs the player will actually create. Keep each row concrete: `A x B -> observable effect`.

## 3. Turn each row into a scripted L3 check
For a row `A x B -> effect`: script the scenario in-engine (set up A, apply B, assert the effect),
capture evidence. This is `TraversalProbe`-style scripted validation generalized from geometry to
system pairs. A row without a passing scenario is an unverified interaction.

## 4. Survival: also prove resource-loop-closure
Separately from pairwise rows, verify the **resource graph has no dead-end**: every consumable the
player *needs* has a renewable, reachable source. A sink without a source is a survival softlock. List
the graph in GAMEPLAN; walk each need back to a reachable source.

## 5. Validate + record
- L3: every matrix row has a passing scripted scenario; resource-loop-closure holds.
- Record `interaction_matrix -> validated:L3` (survival: bump `resource_economy` too).
- Re-run in the regression sweep — adding a system can silently break an existing interaction.

> Why this matters: functional per-milestone validation confirms each system in isolation and is
> structurally blind to emergent combinations. The matrix is the only place they get checked.
