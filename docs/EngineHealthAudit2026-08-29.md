# Phyxel Engine Health Audit — 2026-08-29

This is a current, evidence-based triage of feature completeness, architecture, dead code,
efficiency, tests, and documentation. It complements the deeper domain documents; it does not
replace their design history.

## Executive assessment

Phyxel has unusually broad subsystem coverage, but breadth is ahead of integration discipline.
The dominant failure mode is not “class does not exist”; it is “the generated/runtime object is
not registered with the system that makes it functional.” The second-order problem is that the
manual wiring lives in a few files too large to review safely. Performance work is sophisticated
at runtime, but development-time feedback has accumulated avoidable multi-minute costs.

The highest-return sequence is:

1. Make builds/tests trustworthy and hermetic.
2. Add functional-wiring assertions at subsystem boundaries.
3. Stop growth of the four hub files, then extract one command domain at a time.
4. Remove only dead code proven by build/reference evidence; classify unwired public systems as
   feature gaps instead of deleting them.
5. Measure runtime hot paths before further optimization; existing plans already cover world and
   GPU scale, while the immediate proven waste is in tests and tooling.

## Baseline evidence

- 664 first-party engine/editor C++ source/header files: 293 engine `.cpp` files and 341 public
  engine headers, plus editor files.
- Largest implementation files by physical line count:
  - `editor/src/Application.cpp`: 20,516
  - `engine/src/scene/AnimatedVoxelCharacter.cpp`: 4,713
  - `engine/src/core/EngineAPIServer.cpp`: 4,646
  - `engine/src/graphics/RenderCoordinator.cpp`: 4,239
  - `engine/src/vulkan/VulkanDevice.cpp`: 2,764
- The documented build could not configure because `cmake` is absent from PATH. Before this audit,
  `build_and_test.ps1` continued after that failure and ran an old `phyxel_tests.exe`.
- That binary discovered 3,695 tests in 431 suites, contradicting the build script's “276” and the
  README's “~2,300.” Two live LLM tests failed due to network access.
- Each of four `AtlasManagerTest` cases rebuilt both full texture arrays. The first two each took
  about 50 seconds. Sharing the immutable build fixture removes roughly 150 seconds from this one
  suite on this machine.
- The shader manifest passes for 81 compiled shaders but reports `solver_apply.comp` as an orphaned
  source with no build rule or SPIR-V artifact.

## P0 — Trustworthy engineering feedback

### Build harness correctness

The harness used `$LASTEXITCODE` after unresolved commands and later reset `$BuildSuccess = $true`
unconditionally. This allowed stale executables to masquerade as current validation. Fixed in this
pass: required tools are checked before work begins, and the real build result is retained.

Still needed:

- CI should install/pin CMake and dependencies explicitly and prove a clean clone, not rely on
  developer-machine residue.
- Add a test that invokes the harness with a deliberately missing tool and asserts nonzero exit.
- Do not report missing test executables as warnings; a requested suite missing its executable is
  a failure.

### Test taxonomy and hermeticity

Live LLM tests were in the unit binary and activated by any ambient API key. They are now gated by
`PHYXEL_RUN_LIVE_AI_TESTS=1` as well as the key. Longer term, move them to a separately labelled
integration target so `ctest -L unit` can be strictly local and deterministic.

Introduce CTest labels (`unit`, `integration`, `live`, `gpu`, `slow`, `benchmark`, `stress`, `e2e`)
and make the default PR job run `unit` plus a curated hermetic integration set. Exact counts should
be generated, never hand-maintained in prose.

## P1 — Feature completeness: close the wiring gap

The detailed inventory is in `FunctionalWiringBacklog.md`. The architectural finding is broader:
geometry/data existence is routinely mistaken for shipped behavior. A feature is complete only if
its authoritative runtime manager can be queried after generation/load and an end user or NPC can
exercise it.

Confirmed high-value gaps:

- Generated doors carve openings but are not registered with `DoorManager`.
- Micro-placed furniture loses interaction/kinematic metadata, so generated seats and animated
  parts cannot be discovered reliably.
- Packaged-game navigation omits the editor-only obstacle-provider wiring.
- Interior destination anchors are outside buildings because exterior-to-interior navigation is
  unresolved.
- Loot tables are implemented and tested but have no production consumers; containers/trade are
  not implemented. This is dormant capability, not dead code.
- Static windows, signs, markets, wall walks, road preference, and subterranean connectivity have
  visible/data representations ahead of functional ownership.

Required policy: every generator acceptance ladder needs a **W (wired)** assertion beside visual,
structural, traversal, and live-render checks. Query the owning manager after build/load, then add a
negative control proving the test fails when registration is removed.

## P1 — Architecture

### Contain hub-file growth

The May architecture audit called for a size guard, but `Application.cpp` has grown from 15,275 to
20,516 lines (about 34%). Add a CI ratchet that permits files to shrink but fails growth above the
recorded baseline. This is a containment rule, not an arbitrary style limit.

Avoid a new generic module framework until extraction proves the needed interface. The existing
`CommandRegistry` and grouped `registerXCommands()` methods provide a practical seam:

1. Select one cohesive, well-tested domain.
2. Move its registrations and handlers into a domain translation unit.
3. Pass a narrow dependency struct containing only what that domain uses.
4. Preserve routes and responses byte-for-byte with characterization tests.
5. Repeat; keep lifecycle/composition in `Application`.

Apply the same pattern later to `EngineAPIServer` route families and `RenderCoordinator` passes.
Do not decompose hot rendering/chunk paths merely to satisfy file-size aesthetics.

### Clarify ownership

The single-owner `WorldObject` direction in `EngineArchitectureAudit.md` remains the right pilot.
One record should own persistence and state transitions; chunk voxels, render instances, and physics
bodies should be projections. Until migration completes, centralize teardown and add invariant
checks for ghost/orphan/reappearance states.

### Public dependency surface

`phyxel_core` exposes Vulkan, GLFW, ImGui, pybind11, JSON, HTTP, and multiple include directories as
PUBLIC dependencies. This makes standalone consumers inherit editor-adjacent and implementation
dependencies, slows rebuilds, and weakens the claimed engine/editor boundary. Audit every PUBLIC
link/include and make it PRIVATE where public headers do not require it. A clean minimal-game build
is the acceptance test; the currently commented-out minimal-game subdirectory should be restored or
the README claim removed.

## P1/P2 — Efficiency

### Proven development-time waste

- Atlas tests rebuilt hundreds of texture layers four times. Fixed by building once per suite.
- Default test selection mixed fast logic tests with live network and asset-pipeline work. Label and
  split these categories.
- Engine/editor/test sources use recursive globbing. `CONFIGURE_DEPENDS` prevents stale discovery,
  but explicit target source lists give clearer ownership and reviewable build changes. Convert by
  subsystem opportunistically rather than in one noisy rewrite.
- The repository root contains very large runtime logs/CSV/executable artifacts. They are ignored,
  not tracked, but they make workspace scans and accidental tooling expensive. Put runtime output
  under one ignored `artifacts/` or `run/` directory with rotation/retention defaults.

### Runtime work requiring measurement

Do not infer runtime bottlenecks from file size. Use existing profilers/benchmarks to establish frame
budgets for chunk streaming, face rebuilds, upload bytes, draw/dispatch count, physics contacts,
character updates, and persistence. Known code-level candidates include whole-chunk rebuilds for
single-cube face changes and the deprecated all-chunks update path, but optimize only after capture.

Add performance budgets to CI for deterministic CPU kernels. GPU and full-world benchmarks should
publish trends without initially gating heterogeneous runners.

## P2 — Dead-code policy and candidates

Classify before deleting:

- **Proven orphan:** no build rule, runtime load, include, generated binary, or documented future
  owner. Delete source plus manifest/docs in one change.
- **Test-only legacy twin:** production has a replacement but old API exists solely for its own
  tests. Migrate/remove together after confirming no external API promise.
- **Dormant capability:** tested public API awaiting integration. Track as a feature gap; do not call
  it dead.
- **Conditional backend:** compiled only without an optional dependency. Keep and test that build.

Current candidates:

- `shaders/solver_apply.comp`: proven orphan by the shader checker and AVBD audit. Delete with the
  related solver-orphan cleanup after reconciling the already-modified shader manifest.
- `planParcelFence`: references are its implementation and dedicated tests; production uses
  `planParcelFenceRuns`/`fenceGateWindowAt`. Either migrate the useful traversal test to the live
  path and remove the twin, or reinstate it as the production primitive.
- Goose “extension registration” is a compatibility/configuration boundary with a call site, not
  dead merely because the method is operationally a no-op.
- SQLite-disabled `WorldStorage` stubs are conditional-backend code, not dead.
- `LootTable` is dormant public functionality with tests and docs, not dead.

Automate the first pass with compiler/linker warnings, include-what-you-use or clang tooling, and
shader/asset manifest reachability. Text-reference counts are candidate generation only; reflection,
registries, serialized names, and public consumers make blind deletion unsafe.

## Documentation quality

Fixed in this pass:

- Removed stale exact test counts from the README/build banner.
- Replaced the obsolete unit-test coverage list, which claimed tested systems were untested.
- Documented explicit opt-in semantics for live LLM tests.
- Refreshed architecture file metrics and recorded continuing hub growth.
- Fixed the doc-sync tool's non-interactive hang by making stdin-reading an explicit `--pre-push`
  mode and updating the hook.

Still needed:

- Run a Markdown link/anchor checker over all canonical docs in CI.
- Mark every document as canonical, generated, historical, plan, or ledger; do not mix current API
  truth with dated narrative in one unlabelled document.
- Generate API/tool counts and dimension/reference tables from sources.
- Reconcile the README's standalone-game claim with the commented-out build target.
- Repair mojibake where UTF-8 punctuation/diagrams are decoded using a legacy Windows code page.

## Definition of done for the next health milestone

- Clean-clone configure/build succeeds in CI and cannot fall through to stale binaries.
- Default unit selection is hermetic and has a published duration budget.
- Four hub-file baselines are ratcheted in CI and no longer grow.
- One command domain is extracted from `Application.cpp` with characterization tests.
- Generated doors or furniture ship with a manager-level W assertion and reload coverage.
- The two proven shader/fence legacy candidates are resolved, not merely commented as dead.
- Canonical docs pass link checking and contain no hand-maintained volatile counts.
