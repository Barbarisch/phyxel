# Sitting System — Work Log & Current Status

## What We Were Trying to Fix

Two separate bugs in the chair-sitting interaction for `chair_wood` in the Phyxel engine:

1. **Clip-through during sit_to_stand** — The character's feet stayed at a constant Z position throughout the entire `sit_to_stand` (stand-up) animation, which kept them inside the front edge of the chair geometry at `t=1.0`.

2. **Character hovering above the seat in game mode** — When sitting in a real game project (CharacterTestbed), the character's body floated at the seat anchor height instead of being offset correctly into a sitting pose.

---

## Changes Made (code is in the repo, built successfully)

### 1. `engine/src/scene/AnimatedVoxelCharacter.cpp` — Sit-to-stand interpolation

**Problem:** During `SitStandUp` state, the snap position used a constant `sitStandUpOffset`, so feet were frozen at the same world Z for the entire stand-up clip. At `t=1.0` this placed the character inside the chair front geometry.

**Fix:** The snap now lerps from `sittingIdleOffset` → `sitStandUpOffset` over the clip duration:

```cpp
// SitStandUp: interpolate feet from seated position to standing-clear position
float clipDur = (currentClipIndex >= 0 && currentClipIndex < (int)clips.size())
                ? clips[currentClipIndex].duration : 0.001f;
float t = (clipDur > 0.0f) ? glm::clamp(animTime / clipDur, 0.0f, 1.0f) : 0.0f;
snapPos = m_seatSurfacePos + glm::mix(m_sittingIdleOffset, m_sitStandUpOffset, t);
```

**Verified:** 15-frame sweep in Interaction Editor mode (5 time points × 3 clips). Distinct poses captured at each frame, no clip-through detected visually.

---

### 2. `resources/interactions/humanoid_normal.json` — sitStandUpOffset Z value

Changed `chair_wood / seat_0 / sitStandUpOffset[2]` from `0.20` → `0.60`:

```json
"sitStandUpOffset": [0.0, -0.4740000069141388, 0.60]
```

**Why:** With the lerp in place, `t=1.0` now lands at `sitStandUpOffset`. The old value `z=0.20` (same as `sittingIdleOffset`) left the character at `z=13.533`, which is still `0.134` units inside the chair front edge (`z=13.667`). The new value `z=0.60` matches `sitDownOffset` and places feet at `z=13.933`, clearly in front of the chair.

**Verified:** Numerically confirmed via sweep. Visual screenshots show distinct poses at all 15 frames.

---

### 3. `editor/src/Application.cpp` — Interaction profile fallback load

**Problem:** `InteractionProfileManager` was initialized to look for `humanoid_normal.json` only in the game project's own `resources/interactions/` directory. CharacterTestbed (and likely most game projects) don't have this directory. When the file isn't found, all sitting offsets default to `{0, 0, 0}`, which snaps the character to the raw seat anchor position — making them appear to float/hover at seat height rather than sitting on it.

**Fix:** Load engine built-in profiles first, then allow the project to override:

```cpp
// Load engine built-in profiles as base
interactionProfileManager->setBasePath("resources/interactions/");
interactionProfileManager->loadArchetype("humanoid_normal");

// If a project dir exists, set it as save path and allow project-specific override
// (loadArchetype only clears on successful file open, so missing project file
//  leaves engine defaults intact)
if (!projectDir_.empty()) {
    interactionProfileManager->setBasePath(projectDir_ + "/resources/interactions/");
    interactionProfileManager->loadArchetype("humanoid_normal");
}
```

**Verified:** Build succeeded clean. Runtime verification was not completed — the engine was relaunched incorrectly before the test could run.

---

### 4. `editor/src/Application.cpp` — Template surface-snap

**Problem:** `POST /api/world/template` accepted any Y position including ones that would place the template inside solid ground voxels. The user spawned `chair_wood` at `y=16` in a world where the floor occupies `y=16`, embedding the chair halfway underground.

**Fix:** Before placing the template, the handler scans the template's XZ footprint downward from the requested Y. If any column has a solid voxel at or above the requested Y, it raises Y to sit on top:

```cpp
// Surface-snap: prevent placing templates inside the ground
std::set<std::pair<int,int>> footprint;
for (const auto& c : tmpl->cubes)
    footprint.insert({c.relativePos.x, c.relativePos.z});
for (const auto& s : tmpl->subcubes)
    footprint.insert({s.parentRelativePos.x, s.parentRelativePos.z});
for (const auto& m : tmpl->microcubes)
    footprint.insert({m.parentRelativePos.x, m.parentRelativePos.z});

int requestedY = static_cast<int>(y);
int requiredY  = requestedY;
for (const auto& [lx, lz] : footprint) {
    int wx = static_cast<int>(x) + lx;
    int wz = static_cast<int>(z) + lz;
    for (int wy = requestedY; wy >= requestedY - 64; --wy) {
        if (chunkManager->getCubeAt({wx, wy, wz})) {
            requiredY = std::max(requiredY, wy + 1);
            break;
        }
    }
}
if (requiredY > requestedY)
    y = static_cast<float>(requiredY);
```

**Note:** The `spawn_template` endpoint reads position from a nested `"position"` object, not top-level x/y/z keys:
```json
{ "name": "chair_wood", "position": { "x": 16, "y": 16, "z": 14 } }
```

**Verified:** API confirmed — requesting `y=16` returned `y=17` in response. Surface-snap is working.

---

## What Is NOT Verified

- **Game-mode sitting with the profile fallback fix** — The profile fix (change #3) was built but the engine was never successfully relaunched to test it. This is the most important thing to verify. The expected result is: character sits with feet at `seatAnchor + sittingIdleOffset` (~0.47 units below and 0.20 units in front of the seat anchor), instead of floating at the raw seat anchor position.

---

## API Notes for Whoever Tests This

The correct endpoints (wrong ones were used during testing and wasted time):

```
POST /api/world/template          — spawn a template (position must be nested)
GET  /api/placed_objects          — list placed objects with positions
POST /api/placed_object/remove    — remove a placed object (body: {"id": "chair_wood_1"})
POST /api/interaction/sit         — trigger sitting
     body: {"entity_id":"player","object_id":"chair_wood_6","point_id":"seat_0"}
POST /api/orbit-screenshots       — take orbit screenshots
```

The engine runs at `localhost:8090`. The asset editor (for IE validation) runs at `localhost:8091`.

---

## Summary

| Change | File | Status |
|--------|------|--------|
| Lerp sit_to_stand feet over clip | `AnimatedVoxelCharacter.cpp` | Built + verified in IE |
| sitStandUpOffset Z: 0.20 → 0.60 | `humanoid_normal.json` | Built + verified numerically |
| Profile fallback load (hover fix) | `Application.cpp` | Built, NOT runtime tested |
| Surface-snap for template spawn | `Application.cpp` | Built + verified via API |
