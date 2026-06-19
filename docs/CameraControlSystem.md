# Camera & Control System — Design

> Status: **design pass** (no code yet). Target home: the roadmapped engine-side
> **`GameShell`** (see `docs/AgentContext.md` "engine-side game-shell base classes").
> Author intent: default cameras (first-person, third-person, overhead, isometric) and
> default character control schemes that dev sessions can pick from and **override**.

## 1. Motivation — why this exists

Camera framing and input-to-character mapping are currently **fused into per-host update
loops and duplicated** across every host:

- `editor/src/Application.cpp` — `handleInput()` (~3441) maps WASD/mouse → character, and the
  update loop (~3060) pushes `inputManager` yaw/pitch into the camera, then
  `camera->updatePositionFromTarget(...)`.
- `MazeRunner.cpp` (scaffolded game) — re-implements that whole loop by hand.

Because the loop is copied, the copies rot independently. The standalone copy shipped with
**inverted movement** (W drove the character backward — the engine convention is *negative
`forward` = move forward*) and **no mouse-look** (it never pushed yaw/pitch into the camera and
left look RMB-gated). Both were real, shipped bugs traced back to this duplication.

The current building blocks are partial:

| Piece | State |
|-------|-------|
| `Graphics::Camera` (`engine/include/graphics/Camera.h`) | Modes `{FirstPerson, ThirdPerson, Free}`. Positioning baked into `updatePositionFromTarget`. No overhead/isometric. |
| Projection | Hard-coded `glm::perspective` in `RenderCoordinator.cpp:892`. No orthographic path. |
| `Graphics::CameraManager` | Has `CameraSlot`, `CameraTransition`, `CameraPath` (cinematic). Good for *named slots + transitions*; nothing about control or per-mode rigs. |
| `Input::InputManager` | Mouse-look (gated on `mouseCaptured`, set only while RMB held), WASD camera move, yaw/pitch, `setMouseCaptured()` (added 2026-06-08). |
| `Scene::AnimatedVoxelCharacter` | `setControlInput(forward, turn, strafe)` (neg forward = forward), `setFacingYaw()`, `getCameraTrackPosition()`. |
| `Core::GameCallbacks` | The host interface games subclass (`onUpdate`, `onHandleInput`, …). |

There is **no shared, overridable abstraction** for "how the camera frames the player" or "how
input drives the character." This document proposes one.

## 2. Core idea — two orthogonal strategies + one shared driver

Camera framing and control mapping are **independent and separately swappable**. FPS controls
with a third-person camera = an over-the-shoulder shooter; tank controls with an overhead camera
= a classic dungeon crawler. The author asked for both independently, so both are first-class.

```
            ┌─────────────────────── GameplayCameraController ───────────────────────┐
  input ───▶│  ControlScheme::sample()  ─▶ {move intent, look intent}                │
            │        │                                                               │
            │        ├─▶ character.setControlInput(fwd, turn, strafe)                │
            │        │   (+ setFacingYaw when the scheme couples body to view)       │
            │        ▼                                                               │
            │  CameraRig::update(camera, target, yaw, pitch, dt)  ─▶ camera pose     │
            │  CameraRig::projection(aspect)  ─▶ perspective | ortho                 │
            └────────────────────────────────────────────────────────────────────────┘
```

### 2.1 `CameraRig` (abstract) — how the camera frames a target, incl. projection

```cpp
// engine/include/graphics/CameraRig.h
class CameraRig {
public:
    virtual ~CameraRig() = default;

    // Position + orient `cam` to frame `target`, given the desired look angles.
    virtual void update(Camera& cam, const glm::vec3& target,
                        float yaw, float pitch, float dt) = 0;

    // Projection for this rig. Base returns perspective(fov); ortho rigs override.
    virtual glm::mat4 projection(float aspect, float nearP, float farP) const;

    // Data knobs — the common tweaks, no subclass required:
    float eyeHeight  = 0.5f;   // first-person eye / track offset
    float distance   = 5.0f;   // third-person / iso boom length
    float fov        = 45.0f;  // perspective rigs
    float orthoScale = 20.0f;  // ortho rigs (half-height in world units)
    float pitchClampMin = -89.0f, pitchClampMax = 89.0f;
};
```

Shipped defaults (each a small struct with public data + one `update` override):

| Rig | Behavior | Projection |
|-----|----------|------------|
| `FirstPersonRig` | eye at `target + eyeHeight`, look along yaw/pitch. The rig's static `eyeHeight` default is 0.5, but `GameShell` overrides it per-character to `getControllerHalfHeight() × 1.8` (≈ eye level / 90% of full height — `target` is the capsule **feet**), so first-person isn't stuck at knee height; an explicit `game.json camera.eyeHeight` still wins. | perspective |
| `ThirdPersonRig` | orbit `distance` behind target at yaw/pitch (today's `updatePositionFromTarget` logic, extracted). *Optional* wall-collision shortening in a subclass. | perspective |
| `OverheadRig` | directly above target looking straight down; yaw rotates the top-down view | **orthographic** |
| `IsometricRig` | fixed pitch (~35.264°) + 45° yaw offset, follows target XZ at `distance` | **orthographic** |

**Engine change with teeth:** `RenderCoordinator` must stop hard-coding `glm::perspective`
(`:892`) and instead call `rig->projection(aspect, near, far)`. Without this, overhead/isometric
look wrong (perspective foreshortening). The reflection pass (`:444`) keeps its own perspective —
mirrors are perspective regardless of the main rig.

### 2.2 `ControlScheme` (abstract) — input → movement + look intent

```cpp
// engine/include/input/ControlScheme.h
struct ControlIntent {
    float forward = 0, strafe = 0, turn = 0;   // character control inputs
    float yawDelta = 0, pitchDelta = 0;        // look deltas (degrees)
    bool  coupleFacingToYaw = false;           // lock body heading to camera yaw?
    bool  jump = false, sprint = false, crouch = false, attack = false;
};

class ControlScheme {
public:
    virtual ~ControlScheme() = default;
    virtual ControlIntent sample(Input::InputManager&, float dt) = 0;
    float moveSpeed = 1.0f, mouseSensitivity = 0.1f;
};
```

Shipped defaults:

| Scheme | Mapping |
|--------|---------|
| `FpsScheme` | mouse → yaw/pitch (always-on look), W/S forward (neg=fwd), A/D strafe, `coupleFacingToYaw=true`. *(This is exactly the loop we hand-coded into MazeRunner — now defined once, correctly.)* |
| `TankScheme` | A/D → `turn`, W/S forward, mouse orbits only while RMB held. The editor's classic feel. |
| `TopDownScheme` | WASD moves along world/screen axes (independent of facing), mouse aims; pairs with overhead/iso rigs. |

The **negative-forward convention** and the **`90 - camYaw` facing offset** (derived + runtime-
confirmed 2026-06-08) live in `FpsScheme` only — never re-derived per game again.

### 2.3 `GameplayCameraController` (engine) — the single shared driver

```cpp
// engine/include/core/GameplayCameraController.h
class GameplayCameraController {
public:
    void setRig(std::unique_ptr<Graphics::CameraRig>);       // hot-swappable any frame
    void setScheme(std::unique_ptr<Input::ControlScheme>);
    Graphics::CameraRig*   rig()    const;
    Input::ControlScheme*  scheme() const;

    // Called once per frame by the host (editor OR standalone).
    void update(float dt, Input::InputManager&, Scene::AnimatedVoxelCharacter*,
                Graphics::Camera&);

private:
    std::unique_ptr<Graphics::CameraRig>  rig_;
    std::unique_ptr<Input::ControlScheme> scheme_;
    float yaw_ = 0, pitch_ = 0;   // authoritative look state, owned here
};
```

`update()` body (the loop that replaces both hand-written copies):

```cpp
ControlIntent in = scheme_->sample(input, dt);
yaw_   += in.yawDelta;
pitch_  = clamp(pitch_ + in.pitchDelta, rig_->pitchClampMin, rig_->pitchClampMax);
if (character) {
    character->setControlInput(in.forward, in.turn, in.strafe);
    character->setSprint(in.sprint); character->setCrouch(in.crouch);
    if (in.jump) character->jump();
    if (in.attack) character->attack();
    if (in.coupleFacingToYaw) character->setFacingYaw(glm::radians(90.0f - yaw_));
    character->update(dt);
}
glm::vec3 target = character ? character->getCameraTrackPosition() : camera.getPosition();
rig_->update(camera, target, yaw_, pitch_, dt);
```

`yaw_/pitch_` are owned here (single source of truth) and seeded from the camera on scene load.
`InputManager` keeps its own yaw/pitch for the editor's free-fly/RMB path; the controller reads
*deltas* from the scheme rather than depending on InputManager's absolute yaw, so the two don't
fight.

## 3. Where it lives — `GameShell`

Per the AgentContext roadmap, the scaffold currently embeds ~29KB of shell logic (screen-state
machine, menu renderer wiring, trigger executor, **camera follow**) in every generated game.
`GameShell` is the engine-side base (subclass of `Core::GameCallbacks`) that implements those
defaults once; the scaffold emits a **thin** subclass.

`GameShell` **owns a `GameplayCameraController`** and drives it in its default `onUpdate`:

```cpp
class GameShell : public Core::GameCallbacks {
protected:
    GameplayCameraController cameraCtl_;
    // Defaults chosen from game.json camera.mode / camera.controlScheme:
    virtual void onUpdate(EngineRuntime& e, float dt) override {
        // ...scene pump, triggers...
        if (isPlaying())
            cameraCtl_.update(dt, *e.getInputManager(), playerCharacter(), *e.getCamera());
    }
    // Override points: configureCamera(), createRig(), createScheme(), ...
};
```

The **editor** uses the *same* `GameplayCameraController` in its update loop (replacing the
hand-wired `Application.cpp` block), so editor and standalone can never drift again — the bug
class is closed at the root, not patched per host.

## 4. Customization — three escalating levels

Designed so the common cases need **no code** (per the author's "avoid over-abstraction /
single-source-of-truth" preference, the deep path is opt-in):

1. **Data** — tweak public fields on the default rig/scheme: `distance`, `fov`, `eyeHeight`,
   `orthoScale`, `moveSpeed`, `mouseSensitivity`, pitch clamps.
2. **Subclass** — override one virtual: e.g. a `CollisionThirdPersonRig : ThirdPersonRig` that
   raycasts behind the target and shortens `distance` near walls; or a `ControlScheme::sample`
   for bespoke key maps / gamepad.
3. **Registry + manifest** — `CameraRigRegistry::create("isometric")` /
   `ControlSchemeRegistry::create("fps")` (thin `map<string, factory>`), driven by `game.json`.
   A custom subclass registers under a name and becomes selectable identically.

### 4.1 `game.json` schema additions

```jsonc
"camera": {
    "mode": "first_person | third_person | overhead | isometric",  // existing key, extended
    "controlScheme": "fps | tank | topdown",                       // NEW
    "distance": 6.0, "fov": 60.0, "eyeHeight": 0.6,                // optional rig knobs (eyeHeight overrides the per-character default)
    "mouseSensitivity": 0.12                                        // optional scheme knob
}
```

`mode` already exists and is honored (feedback-3, `loadCamera` / `cameraModeSet`); this extends
its value set and adds `controlScheme` + optional knobs. `GameDefinitionLoader` resolves the
strings via the registries and configures `GameShell`'s controller.

### 4.2 Dev-session ergonomics

- **MCP:** extend the existing `set_camera` tool (already has `mode`) with `control_scheme` and
  the rig knobs. Swapping the strategy pointer is safe on any frame, so it is live.
- **Editor:** a Camera panel dropdown (mode + scheme + sliders) that calls the same controller
  setters — flip first→third→overhead→iso while building a level, no rebuild.
- Defaults if unauthored: `third_person` rig + `tank` scheme (matches today's editor feel);
  a first-person game opts in via `mode: first_person, controlScheme: fps`.

## 5. Relationship to `CameraManager`

`CameraManager` (slots, transitions, cinematic paths) is **complementary, not replaced**. A
`CameraSlot` gains an optional `rig`/`scheme` name; `CameraTransition` blends between two rigs'
*output poses*; cinematic `CameraPath` playback temporarily detaches the controller (sets a
`Free`/scripted rig) and restores it when done. No rewrite of CameraManager.

## 6. Phasing

1. **Consolidate (no new behavior).** Add `CameraRig` (First/Third), `ControlScheme` (FPS/Tank),
   `GameplayCameraController`. Move MazeRunner *and* the editor onto it. Verify parity against the
   2026-06-08 control fixes (CamDbg-trace method: yaw tracks mouse, player advances along look).
2. **New modes.** `OverheadRig` + `IsometricRig` and the `rig->projection()` plumbing into
   `RenderCoordinator` (the orthographic change). Verify visually from the editor.
3. **Authoring.** Registries, `game.json controlScheme` + knobs, `set_camera` MCP extension,
   editor Camera panel, subclass-override docs.
4. **Fold into `GameShell`.** As the broader GameShell base lands, the controller becomes a
   `GameShell` member and the scaffold emits a thin subclass; delete the scaffold's copied
   camera-follow code.

## 7. Non-goals / guardrails

- **Not** a plugin framework. Registries are a `map<string, std::function>`; two interfaces with
  ~2 virtuals each; rigs are plain structs with public data. The justification for *any*
  abstraction here is that it **deletes two divergent copies of a loop that already shipped a
  bug** — if a change would add abstraction without removing duplication, it's out of scope.
- Gamepad, multiple simultaneous cameras (split-screen), and per-camera post-process are future
  work, not part of this pass.

## 8. Open decisions

- **Look-state ownership:** controller-owned `yaw_/pitch_` (proposed) vs continue routing through
  `InputManager`. Proposed because the InputManager path is editor-shaped (RMB, free-fly) and
  fighting it caused the standalone bug.
- **Ortho near/far + scale defaults** for overhead/iso that keep the 32³ maze fully in frame.
- **Whether `CameraMode` enum is extended** (add `Overhead`, `Isometric`) or the rig becomes the
  source of truth and `CameraMode` is demoted to a label. Leaning: extend the enum for
  compatibility, but the *rig* drives behavior.
```
