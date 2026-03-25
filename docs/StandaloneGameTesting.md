# Standalone Game Testing Guide

This guide covers how to manually test standalone (scaffolded) games created with
`tools/create_project.py`. Use it as a checklist to verify consistent behavior
across sessions.

## Prerequisites

1. Phyxel engine built in Debug: `./build_and_test.ps1 -Config Debug`
2. All unit + integration tests passing (baseline)
3. A test game definition JSON (see `samples/game_definitions/testing_baseline.json`)
4. Python 3.10+ with venv activated: `& .venv\Scripts\Activate.ps1`

## Quick-Start: Scaffold → Build → Run

```powershell
# 1. Set CMake on PATH (one-time per terminal)
$env:PATH += ";C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"

# 2. Scaffold a test project from the test definition
python tools/create_project.py TestGame --game-definition samples/game_definitions/testing_baseline.json

# 3. Pre-bake the world (engine must be running with the definition loaded)
#    OR copy a previously baked database:
#    copy worlds\default.db "$env:USERPROFILE\Documents\PhyxelProjects\TestGame\worlds\default.db"

# 4. Build the test game
cd "$env:USERPROFILE\Documents\PhyxelProjects\TestGame"
cmake -B build -S .
cmake --build build --config Debug

# 5. Run
.\build\Debug\TestGame.exe
```

**Shortcut using engine as editor (--project mode):**
```powershell
# Launch the editor pointing at the project
.\phyxel.exe --project "$env:USERPROFILE\Documents\PhyxelProjects\TestGame"
# Then via MCP: build_game, run_game
```

## Test Checklist

Use this checklist after every change to `create_project.py`, `EngineRuntime`,
`GameDefinitionLoader`, `PatrolBehavior`, or related engine subsystems.

---

### 1. Main Menu (Phase 5)

| # | Test | Expected Result | Pass? |
|---|------|-----------------|-------|
| 1.1 | Launch game | Main menu appears with game title, "New Game", "Quit" | |
| 1.2 | Mouse cursor | Cursor is visible and free on main menu | |
| 1.3 | Click "Quit" | Window closes cleanly, no crash | |
| 1.4 | Click "New Game" | Transitions to gameplay, cursor captured | |

---

### 2. Player Character & Camera (Phase 1)

| # | Test | Expected Result | Pass? |
|---|------|-----------------|-------|
| 2.1 | Player exists | After "New Game", a physics character is visible in 3rd person | |
| 2.2 | Camera mode | Camera orbits behind the player (ThirdPerson mode) | |
| 2.3 | WASD movement | Player walks on terrain surface, camera follows | |
| 2.4 | Mouse look | Camera orbits around player when mouse moves | |
| 2.5 | Player falls | If spawned above terrain, player falls to surface (gravity works) | |
| 2.6 | Player on terrain | Player does not fall through terrain — stands on surface | |

---

### 3. NPC Behavior (Phase 2)

| # | Test | Expected Result | Pass? |
|---|------|-----------------|-------|
| 3.1 | NPC visible | NPCs render as animated voxel characters at spawn positions | |
| 3.2 | NPC falls to ground | NPCs spawned above terrain fall to surface (not floating) | |
| 3.3 | Patrol movement | Patrol NPCs walk toward waypoints along the ground | |
| 3.4 | NPC gravity | NPCs descend slopes, don't hover at waypoint Y | |
| 3.5 | Idle NPC | Idle NPCs stay in place (not sliding around) | |
| 3.6 | Speech bubbles | Patrol NPCs occasionally show speech bubbles at waypoints | |

---

### 4. Terrain (Phase 3)

| # | Test | Expected Result | Pass? |
|---|------|-----------------|-------|
| 4.1 | Terrain shape | Gentle rolling hills, ~4 block height variation | |
| 4.2 | Walkable | Player can walk across terrain without getting stuck | |
| 4.3 | Materials | Surface=Default, sub-surface=Cork (brown), deep=Stone | |
| 4.4 | Multi-chunk | Terrain spans multiple chunks seamlessly | |

---

### 5. Input & Controls (Phase 4)

| # | Test | Expected Result | Pass? |
|---|------|-----------------|-------|
| 5.1 | WASD in gameplay | Movement works with real delta time (smooth, not jerky) | |
| 5.2 | Input blocked on menu | WASD does nothing on main menu or pause screen | |
| 5.3 | ESC during gameplay | Opens pause menu, cursor becomes free | |
| 5.4 | ESC during pause | Closes pause menu, cursor re-captured | |
| 5.5 | Pause: "Resume" | Returns to gameplay, cursor captured | |
| 5.6 | Pause: "Main Menu" | Returns to main menu, game paused | |
| 5.7 | Pause: "Quit" | Window closes cleanly | |

---

### 6. Physics Simulation

| # | Test | Expected Result | Pass? |
|---|------|-----------------|-------|
| 6.1 | Physics paused on menu | World doesn't simulate on main menu or pause | |
| 6.2 | Physics runs in gameplay | Objects fall, character walks, NPCs move when Playing | |
| 6.3 | No crash on resume | Resuming from pause resumes physics cleanly | |

---

### 7. Edge Cases

| # | Test | Expected Result | Pass? |
|---|------|-----------------|-------|
| 7.1 | No game.json | Game launches with empty world + default player at (16,25,16) | |
| 7.2 | Empty game.json | `{}` — loads with default player, no NPCs | |
| 7.3 | Missing player in def | Default player spawned as fallback at (16,25,16) | |
| 7.4 | Rapid ESC toggle | Pause/resume 10x quickly — no crash or stuck state | |
| 7.5 | Alt+F4 mid-game | Clean shutdown, no Vulkan validation errors | |

---

## Test Game Definition

Use `samples/game_definitions/testing_baseline.json` for consistent testing. It
provides:

- **Perlin terrain** with gentle hills (heightScale=4, frequency=0.04)
- **3×1×3 chunk world** (9 chunks total)
- **Physics player** at (16, 25, 16) — above terrain, should fall into place
- **Camera** set for third-person overview
- **2 NPCs**: one patrol (Guard, 2 waypoints), one idle (Merchant)
- **Simple structures**: stone platform, wooden hut
- **Dialogue** on both NPCs
- **Basic story arc** with 2 beats

This definition exercises all the systems changed in Phases 1–5.

## Automated Tests (Unit + Integration)

Always run these before and after changes:

```powershell
# Unit tests (should be 1033+)
.\build\tests\Debug\phyxel_tests.exe --gtest_brief=1

# Integration tests (should be 36)
.\build\tests\integration\Debug\phyxel_integration_tests.exe --gtest_brief=1

# PatrolBehavior-specific (velocity-based movement)
.\build\tests\Debug\phyxel_tests.exe --gtest_filter="PatrolBehaviorTest.*"

# EngineRuntime tests (getLastDeltaTime, etc.)
.\build\tests\Debug\phyxel_tests.exe --gtest_filter="EngineRuntimeTest.*"

# GameCallbacks tests
.\build\tests\Debug\phyxel_tests.exe --gtest_filter="GameCallbacksTest.*"
```

## Key Implementation Details (for debugging)

### Player Character (Phase 1)
- Created via `spawnEntity("physics", pos, "")` in generated `onInitialize()`
- `PhysicsCharacter` constructor: `(PhysicsWorld*, InputManager*, Camera*, startPos)`
- Fallback spawn at (16, 25, 16) if no player in `game.json`
- Camera set to `CameraMode::ThirdPerson` after player spawn
- `playerCharacter_->update(dt)` + `updateCamera()` in `onUpdate()`

### NPC Movement (Phase 2)
- `PatrolBehavior::update()` calls `ctx.self->setMoveVelocity(direction * speed)`
- `setMoveVelocity()` on `AnimatedVoxelCharacter`: sets XZ velocity, preserves Y
- Gravity is `btVector3(0, -9.81, 0)` in `PhysicsWorld`
- NPCs zero velocity when waiting at waypoints or after arrival
- `controllerBody->activate(true)` prevents physics body from sleeping

### Terrain (Phase 3)
- Uses `"params": {"heightScale": 4.0, "frequency": 0.04}` in world definition
- WorldGenerator Perlin: `height = perlinNoise3D(...) * heightScale + 16.0`
- With heightScale=4, terrain varies ~4 blocks above/below Y=16

### Input (Phase 4)
- `engine.getLastDeltaTime()` provides real dt from `beginFrame()`
- Input gated: `if (isGameRunning(screen_.getState())) input->processInput(dt)`
- ESC toggles pause via `screen_.togglePause()` / `screen_.resume()`

### Menus (Phase 5)
- `GameScreen screen_` starts in `ScreenState::MainMenu`
- Cursor: `setCursorVisible(!shouldCapture)` toggled on every state change
- `renderMainMenu()` / `renderPauseMenu()` from `ui/GameMenus.h`

## Regression Indicators

If you see any of these, there's a regression:

| Symptom | Likely Cause |
|---------|--------------|
| NPCs float in the air | `PatrolBehavior` using `setPosition()` instead of `setMoveVelocity()` |
| No player character | `entitySpawner` not wired in `loadGameDefinition()` |
| Camera stuck in free mode | `cam->setMode(ThirdPerson)` missing after player spawn |
| WASD doesn't work | Input not gated properly, or `processInput()` never called |
| Game launches straight to 3D view | `screen_.setState(MainMenu)` missing in `onInitialize()` |
| Cursor invisible on menus | `updateCursorMode()` not called on state transitions |
| Jerky movement | Still using hardcoded `0.016f` instead of `getLastDeltaTime()` |
| PatrolBehavior tests fail | `StubEntity::setMoveVelocity()` not overridden, or tests checking position instead of velocity |
| Physics runs on menu | `onUpdate()` missing `isGameRunning()` guard |

## Files Changed (Reference)

### Engine-level (affect all games)
- `engine/include/scene/Entity.h` — added virtual `setMoveVelocity()`
- `engine/include/scene/AnimatedVoxelCharacter.h` + `.cpp` — `setMoveVelocity()` impl
- `engine/include/scene/NPCEntity.h` + `.cpp` — `setMoveVelocity()` override
- `engine/src/scene/behaviors/PatrolBehavior.cpp` — velocity-based movement
- `engine/include/core/EngineRuntime.h` + `.cpp` — `getLastDeltaTime()`

### Scaffolding (affects new game projects only)
- `tools/create_project.py` — complete rewrite of generated game template

### Tests
- `tests/scene/NPCSystemTest.cpp` — updated PatrolBehavior tests for velocity-based movement
