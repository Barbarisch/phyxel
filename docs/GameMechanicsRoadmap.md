# Phyxel — Game Mechanics Infrastructure Roadmap

*Created: March 11, 2026*
*Status: Planning complete, Phase 1-4 implemented*

## Overview

Four-phase plan to build core engine infrastructure so that adding game mechanics (quests, cutscenes, multi-NPC scenes) becomes trivial. Bottom-up approach: build the infrastructure layer first, then wire it into existing systems.

### Dependency Graph

```
Phase 1 (Lights) ──┐
                    ├──► Phase 3 (NPCs) ──► Phase 4 (Dialogue)
Phase 2 (Cameras) ─┘
```

- Phases 1 & 2 are **fully parallel** — no dependencies between them
- Phase 3 uses entity-follow cameras (Phase 2) and NPC-attached lights (Phase 1)
- Phase 4 builds on NPC interaction triggers (Phase 3)

### Key Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Light GPU buffer | SSBO (binding 3) | Variable-length arrays, current UBO is only 256 bytes |
| NPC architecture | Scripted state machines, Goose AI optional | Scripted NPCs work offline; Goose plugs into same interface |
| Dialogue UI | RPG box + speech bubbles | Box for focused conversation, bubbles for ambient chatter |
| Camera features | Full toolkit | Named slots, cinematic paths, entity-follow, smooth transitions |
| Dialogue authoring | JSON files in `resources/dialogues/` | Simplest path; visual editor can come later |

### Scope Boundaries

**In scope**: Light structs/manager/shaders, camera slots/transitions/paths, NPC spawning/behaviors/interaction, dialogue box/bubbles/trees

**Explicitly excluded**: Per-light shadow maps, pathfinding/navmesh, combat/health/damage system, inventory, VR cameras, networking/multiplayer

---

## Phase 1: Multi-Light System

**Status**: ✅ Complete (430 tests passing)
**Goal**: Support N point lights + M spot lights alongside the existing directional sun, with runtime add/remove.

### Current State

- One directional sun with color/direction controls
- Ambient light global multiplier (0.0–2.0)
- Glow material is visual-only (emissive multiplier, no actual light emission)
- Shadow mapping: PCF with 2048×2048 depth texture
- UBO struct ~256 bytes (well below 64KB limit)
- No SSBOs currently in use
- Descriptor set: binding 0 (UBO), binding 1 (texture atlas), binding 2 (shadow map)

### Steps

1. **Define light structs** — new `engine/include/graphics/Light.h`
   - `PointLight { vec3 position, vec3 color, float intensity, float radius, bool enabled }`
   - `SpotLight { vec3 position, vec3 direction, vec3 color, float intensity, float radius, float innerCone, float outerCone, bool enabled }`
   - Keep existing sun as-is (already in UBO)

2. **Create `LightManager`** — `engine/include/graphics/LightManager.h` + `engine/src/graphics/LightManager.cpp`
   - Owns vectors of `PointLight` (max 32) and `SpotLight` (max 16)
   - `addPointLight(params) → lightId`
   - `addSpotLight(params) → lightId`
   - `removeLight(id)`, `updateLight(id, params)`
   - `getGPUData()` returns packed struct for SSBO upload
   - Optional: `setGlowEmitsLight(bool)` — auto-register glow voxels as point lights

3. **Add SSBO for lights** — descriptor binding 3
   - Modify `VulkanDevice` descriptor set layout: add binding 3 as `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER`
   - SSBO struct layout:
     ```glsl
     layout(std430, set=0, binding=3) buffer LightBuffer {
         uint numPointLights;
         uint numSpotLights;
         PointLightGPU pointLights[32];
         SpotLightGPU spotLights[16];
     };
     ```
   - Upload from `LightManager` each frame in `RenderCoordinator::render()`
   - Relevant code: `VulkanDevice::createDescriptorSetLayout()`, `VulkanDevice::createDescriptorSets()`

4. **Update shaders**
   - `shaders/voxel.frag`: Add SSBO read, per-fragment loop over point/spot lights
   - `shaders/character.frag`: Bring lighting in line with voxel.frag (currently hardcoded)
   - Point light attenuation: `1.0 / (1.0 + linear*d + quadratic*d²)` with radius cutoff
   - Spot light falloff: `smoothstep(outerCone, innerCone, dot(lightDir, spotDir))`
   - Rebuild shaders: `.\build_shaders.bat`

5. **Wire into RenderCoordinator**
   - `RenderCoordinator` owns `LightManager` (member variable)
   - `getLightManager()` accessor for external use
   - Existing `setSunDirection()` / `setSunColor()` / `setAmbientLight()` unchanged
   - SSBO buffer created/mapped alongside existing UBO in `createUniformBuffers()`

6. **Extend F6 Lighting Controls** (ImGui panel)
   - List active point lights and spot lights with editable properties
   - Position (x/y/z sliders), color picker, intensity slider, radius slider
   - "Add Point Light at Camera" / "Add Spot Light at Camera" buttons
   - "Remove" button per light
   - File: `engine/src/ui/ImGuiRenderer.cpp` → `renderLightingControls()`

### Files to Modify

| File | Change |
|------|--------|
| **NEW** `engine/include/graphics/Light.h` | Light struct definitions |
| **NEW** `engine/include/graphics/LightManager.h` | LightManager class |
| **NEW** `engine/src/graphics/LightManager.cpp` | LightManager implementation |
| `engine/include/vulkan/VulkanDevice.h` | Descriptor layout, SSBO buffer members |
| `engine/src/vulkan/VulkanDevice.cpp` | Add binding 3, create/map SSBO, update descriptor writes |
| `engine/include/graphics/RenderCoordinator.h` | Own LightManager, SSBO upload method |
| `engine/src/graphics/RenderCoordinator.cpp` | Upload light data each frame |
| `engine/src/ui/ImGuiRenderer.cpp` | Extended F6 lighting panel |
| `engine/include/ui/ImGuiRenderer.h` | LightManager pointer for UI |
| `shaders/voxel.frag` | Multi-light accumulation loop |
| `shaders/character.frag` | Match voxel.frag lighting model |

### Verification

- [ ] Place glow voxels → nearby surfaces visibly illuminate
- [ ] Add point light via F6 UI → illumination sphere visible on nearby geometry
- [ ] Add spot light → cone-shaped illumination visible
- [ ] Remove lights → illumination disappears immediately
- [ ] 32 point lights active simultaneously without performance collapse
- [ ] Unit tests: `LightManager` add/remove/update, capacity limits, GPU data packing

---

## Phase 2: Camera Management System

**Status**: ✅ Complete (454 tests passing)
**Goal**: Multiple named cameras, smooth transitions, entity following, cinematic paths, hotkey cycling.

### Current State

- Single `Camera` instance owned by `Application`
- Three modes: FirstPerson, ThirdPerson, Free (V key cycles)
- Camera follows active character (physics/spider/animated)
- No save/restore, no smooth transitions, no camera paths
- HTTP API: `GET /api/camera`, `POST /api/camera` (single camera)

### Steps

1. **Create `CameraManager`** — `engine/include/graphics/CameraManager.h` + `.cpp`
   - `CameraSlot { string name, vec3 position, float yaw, float pitch, CameraMode mode, optional<string> followEntityId, float followDistance }`
   - Owns the `Camera` instance + vector of `CameraSlot`
   - `createSlot(name, position, yaw, pitch) → slotIndex`
   - `setActiveSlot(name)` — snap or smooth transition
   - `cycleSlot()` / `cycleSlotsReverse()` — next/prev slot
   - `followEntity(slotName, entityId, distance)` — attach slot to entity via EntityRegistry
   - `update(dt)` — handles transitions, entity follow, path playback
   - Default slot "free" created at init (matches current behavior)

2. **Smooth transitions** — `CameraTransition` (internal to CameraManager or separate helper)
   - `startTransition(fromState, toState, durationSec, easeType)`
   - `update(dt) → bool isComplete`
   - Position: lerp. Orientation: slerp via yaw/pitch interpolation
   - Ease types: `Linear`, `EaseInOut` (cubic hermite)

3. **Cinematic camera paths** — `engine/include/graphics/CameraPath.h` + `.cpp`
   - `CameraWaypoint { vec3 position, float yaw, float pitch, float dwellTimeSec }`
   - Ordered list of waypoints
   - Catmull-Rom spline interpolation between positions
   - `play()`, `pause()`, `stop()`, `update(dt)`
   - `isPlaying()`, `getProgress() → float 0-1`
   - JSON serialization: `toJson()` / `fromJson()` for MCP/scripting use
   - Looping option

4. **Wire into Application**
   - Replace `std::unique_ptr<Camera> camera` with `CameraManager cameraManager`
   - `cameraManager.getCamera()` replaces direct `camera` usage throughout
   - V-key mode cycling: changes mode of current active slot
   - **Tab** cycles camera slots, **Shift+Tab** reverses
   - Entity-follow cameras: `cameraManager.update(dt)` queries `EntityRegistry` for entity position
   - When character control changes (K key), update the current slot's follow target

5. **Extend camera API endpoints** in `EngineAPIServer`
   - `GET /api/cameras` — list all slots with properties
   - `POST /api/cameras` — create new slot
   - `DELETE /api/cameras/{name}` — remove slot
   - `POST /api/camera/active` — switch active slot (with optional `transitionDuration` param)
   - Existing `GET /api/camera` and `POST /api/camera` still work (operate on active slot)

### Files to Modify

| File | Change |
|------|--------|
| **NEW** `engine/include/graphics/CameraManager.h` | CameraManager + CameraSlot + CameraTransition |
| **NEW** `engine/src/graphics/CameraManager.cpp` | Implementation |
| **NEW** `engine/include/graphics/CameraPath.h` | CameraPath + CameraWaypoint |
| **NEW** `engine/src/graphics/CameraPath.cpp` | Spline interpolation, playback |
| `engine/include/graphics/Camera.h` | Add `setState(pos, yaw, pitch)` for snap-set |
| `editor/src/Application.cpp` | Camera ownership → CameraManager |
| `editor/include/Application.h` | CameraManager member |
| `editor/src/input/InputController.cpp` | Tab/Shift+Tab bindings |
| `engine/src/core/EngineAPIServer.cpp` | New camera endpoints |

### Verification

- [ ] Create 3 named camera slots → Tab cycles correctly
- [ ] Follow-camera on animated character → camera tracks entity smoothly
- [ ] Cinematic path with 4 waypoints → smooth spline travel, pauses at dwell times
- [ ] Transition between slots → visible smooth lerp (not a snap)
- [ ] V key still changes mode within current slot
- [ ] K key updates follow target correctly
- [ ] API: create slot, switch with transition, list slots
- [ ] Unit tests: CameraManager slot CRUD, CameraTransition interpolation, CameraPath spline math

---

## Phase 3: NPC Infrastructure

**Status**: ✅ Complete (475 tests passing)
**Depends on**: Phase 1 (NPC-attached lights), Phase 2 (entity-follow cameras for NPC focus)
**Goal**: Easily spawnable NPCs with scripted behavior, interaction triggers, and a clean interface that Goose AI can optionally drive.

### Current State

- `Entity` base class: `update(dt)`, `render()`, position/rotation/scale
- `AnimatedVoxelCharacter`: 19-state FSM, animation blending, physics ragdoll
- `EntityRegistry`: O(1) lookup, type tags, spatial queries, thread-safe
- Goose AI framework wired up (`AISystem`, `GooseBridge`, `AICharacterComponent`, `StoryDirector`) but no active NPCs
- NPC recipes exist in `resources/recipes/` (merchant.yaml, patrol.yaml, dialog.yaml, combat.yaml)
- No pathfinding, no interaction trigger system, no NPC spawning API

### Steps

1. **Create `NPCBehavior` interface** — `engine/include/scene/NPCBehavior.h`
   ```
   class NPCBehavior {
       virtual void update(float dt, NPCContext& ctx) = 0;
       virtual void onInteract(Entity* interactor) = 0;
       virtual void onEvent(const std::string& type, const nlohmann::json& data) = 0;
       virtual std::string getBehaviorName() const = 0;
   };
   ```
   - `NPCContext` provides: own entity ref, EntityRegistry*, LightManager*, DialogueSystem*, world query helpers
   - Concrete implementations: `IdleBehavior`, `PatrolBehavior`, `ScriptedSequenceBehavior`, `GooseNPCBehavior`

2. **Create `NPCEntity`** — `engine/include/scene/NPCEntity.h` + `engine/src/scene/NPCEntity.cpp`
   - Extends `Entity`, wraps an `AnimatedVoxelCharacter` (reuses existing animation system)
   - Members: `string name`, `unique_ptr<NPCBehavior> behavior`, `float interactionRadius` (default 3.0)
   - Optional: `DialogueProvider*` for conversation content
   - Optional: `int attachedLightId` (e.g., NPC carrying a lantern — uses LightManager)
   - `update(dt)`: delegates to behavior, updates attached light position
   - `isInteractable()` override returns true
   - `setBehavior(unique_ptr<NPCBehavior>)` for hot-swapping

3. **Implement behaviors** — `engine/src/scene/behaviors/`
   - `IdleBehavior`: Play idle animation, face nearest player occasionally
   - `PatrolBehavior`: List of `vec3` waypoints, walk speed, wait time per waypoint. Move toward next waypoint (direct line), pause on arrival, advance to next. No pathfinding — stop on collision, retry after delay
   - `GooseNPCBehavior`: Wraps existing `AICharacterComponent` / `AIController`. `update()` forwards to Goose session. `onInteract()` sends player message to agent → feeds response to dialogue system

4. **Create `InteractionManager`** — `engine/include/core/InteractionManager.h` + `.cpp`
   - Each frame: get player position, query EntityRegistry for nearby entities tagged "npc"
   - Check distance < `npc.interactionRadius` for each
   - Track `nearestInteractableNPC` — null if none in range
   - When non-null: signal UI to show "Press E to interact" prompt
   - On E key press: call `nearestInteractableNPC->getBehavior()->onInteract(playerEntity)`
   - Debounce: ignore rapid re-triggers (0.5s cooldown)

5. **Create `NPCManager`** — `engine/include/core/NPCManager.h` + `.cpp`
   - `spawnNPC(name, animFile, position, behaviorType, behaviorParams) → NPCEntity*`
   - Registers in EntityRegistry with type tag "npc"
   - Manages NPC lifecycle (owns the entities)
   - `removeNPC(name)`, `getNPC(name)`, `getAllNPCs()`
   - `behaviorType` enum: `Idle`, `Patrol`, `Goose`

6. **Wire into Application**
   - `Application` owns `NPCManager` and `InteractionManager`
   - Update loop: `interactionManager->update(dt, playerPosition)`
   - E key binding in `InputController`

### Files to Modify

| File | Change |
|------|--------|
| **NEW** `engine/include/scene/NPCBehavior.h` | Behavior interface + NPCContext |
| **NEW** `engine/include/scene/NPCEntity.h` | NPCEntity class |
| **NEW** `engine/src/scene/NPCEntity.cpp` | NPCEntity implementation |
| **NEW** `engine/src/scene/behaviors/IdleBehavior.h/.cpp` | Idle behavior |
| **NEW** `engine/src/scene/behaviors/PatrolBehavior.h/.cpp` | Patrol behavior |
| **NEW** `engine/src/scene/behaviors/GooseNPCBehavior.h/.cpp` | Goose AI adapter |
| **NEW** `engine/include/core/InteractionManager.h` | Interaction proximity system |
| **NEW** `engine/src/core/InteractionManager.cpp` | Implementation |
| **NEW** `engine/include/core/NPCManager.h` | NPC spawning/lifecycle |
| **NEW** `engine/src/core/NPCManager.cpp` | Implementation |
| `engine/include/scene/Entity.h` | Add `virtual bool isInteractable() { return false; }` |
| `editor/src/Application.cpp` | Integrate NPCManager + InteractionManager |
| `editor/include/Application.h` | NPCManager + InteractionManager members |
| `editor/src/input/InputController.cpp` | E key binding |
| `engine/src/ui/ImGuiRenderer.cpp` | "Press E" interaction prompt |

### Verification

- [ ] Spawn NPC with idle behavior → stands in place, plays idle animation
- [ ] Spawn NPC with patrol → walks between 3+ waypoints, pauses at each
- [ ] Walk near NPC → "Press E" prompt appears
- [ ] Press E → `onInteract` fires (logs event, triggers dialogue if Phase 4 done)
- [ ] Swap behavior from scripted to Goose at runtime → NPC now AI-driven
- [ ] NPC with attached point light → light follows NPC position
- [ ] Camera follow slot attached to NPC → camera tracks NPC
- [ ] Unit tests: PatrolBehavior waypoint progression, InteractionManager proximity, NPCManager spawn/remove

---

## Phase 4: Dialogue & Conversation System

**Status**: ✅ Complete (511 tests passing)
**Depends on**: Phase 3 (NPCs trigger dialogue via onInteract)
**Goal**: RPG-style dialogue box for focused conversation + floating speech bubbles for ambient NPC chatter.

### Current State

- ImGui rendering works (debug panels, scripting console, lighting controls)
- Goose AI has `DialogHandler` callback (`say_dialog` with text + emotion)
- `StoryDirector` has quest tracking and NPC mood system (framework only)
- No dialogue tree data structure, no conversation UI, no speech bubbles

### Steps

1. **Dialogue data model** — `engine/include/ui/DialogueData.h`
   - `DialogueNode { string id, string speaker, string text, string emotion, vector<DialogueChoice> choices, string nextNodeId }`
   - `DialogueChoice { string text, string targetNodeId, optional<function<bool()>> condition }`
   - `DialogueTree { string id, string startNodeId, unordered_map<string, DialogueNode> nodes }`
   - JSON serialization: `DialogueTree::fromJson(json)` / `toJson()`
   - `DialogueProvider` interface: `getDialogueTree(context) → DialogueTree*` — scripted trees or AI-generated

2. **Create `DialogueSystem`** — `engine/include/ui/DialogueSystem.h` + `.cpp`
   - State machine: `Inactive`, `Typing`, `WaitingForInput`, `ChoiceSelection`
   - `startConversation(NPCEntity*, DialogueTree*)` → opens UI, pauses player movement
   - `advanceDialogue()` → show next node or choices (Enter key)
   - `selectChoice(int index)` → pick choice, navigate tree (1-4 keys)
   - `endConversation()` → close UI, resume player control (Esc key)
   - `isActive() → bool` — for input routing (dialogue input vs. game input)
   - Typewriter effect: reveal characters over time (configurable speed)

3. **RPG dialogue box** (ImGui rendering in `ImGuiRenderer`)
   - Bottom 25% of screen, semi-transparent dark background
   - Speaker name (bold, colored per-NPC)
   - Text area with typewriter reveal
   - Choice buttons: `[1] Choice text` / `[2] Choice text` / etc.
   - Continue indicator: `▼` blink when waiting for Enter
   - Input: Enter (advance/skip typewriter), 1-4 (select choice), Esc (end conversation)

4. **Floating speech bubbles** — `engine/include/ui/SpeechBubbleManager.h` + `.cpp`
   - `SpeechBubble { string text, string speakerEntityId, float lifetime, float elapsed, float fadeStartTime }`
   - `SpeechBubbleManager::say(entityId, text, durationSec)` — creates bubble
   - `update(dt)` — age bubbles, remove expired
   - Rendering: world-to-screen projection of entity position + height offset → ImGui window at screen coords
   - Clamp to screen edges, handles off-screen (don't render)
   - Max 8 simultaneous bubbles (oldest evicted)

5. **Connect to NPCEntity / InteractionManager**
   - `NPCEntity::onInteract()` → if has `DialogueProvider`, call `dialogueSystem->startConversation(this, tree)`
   - `PatrolBehavior` can call `speechBubbleManager->say(myId, "Nice weather today", 3.0f)` at waypoints
   - Input routing in `Application`: if `dialogueSystem->isActive()`, route Enter/1-4/Esc to dialogue instead of game

6. **Connect to Goose AI**
   - `GooseNPCBehavior::onInteract()` → sends message to Goose agent → agent responds with text/emotion
   - Response feeds into `DialogueSystem` as a dynamic single-node dialogue (no tree — free-form conversation)
   - `say_dialog` command handler → `speechBubbleManager->say()` for ambient AI chatter

7. **Connect to StoryDirector**
   - `DialogueSystem::endConversation()` emits event: `gameEventLog->emit("conversation_ended", {npcId, nodeId, choicesMade})`
   - StoryDirector can listen for these to advance quests
   - Quest state changes can swap an NPC's `DialogueProvider` (different dialogue after quest progress)

8. **Dialogue files** — `resources/dialogues/`
   - JSON format, one file per NPC or conversation
   - Example: `merchant_intro.json`, `guard_quest.json`
   - Loaded at NPC spawn time or on-demand

### Files to Modify

| File | Change |
|------|--------|
| **NEW** `engine/include/ui/DialogueData.h` | DialogueNode, DialogueChoice, DialogueTree, DialogueProvider |
| **NEW** `engine/include/ui/DialogueSystem.h` | Conversation state machine |
| **NEW** `engine/src/ui/DialogueSystem.cpp` | Implementation |
| **NEW** `engine/include/ui/SpeechBubbleManager.h` | Floating bubble manager |
| **NEW** `engine/src/ui/SpeechBubbleManager.cpp` | Implementation (world-to-screen projection) |
| **NEW** `resources/dialogues/` | Directory for dialogue JSON files |
| `engine/src/ui/ImGuiRenderer.h` | DialogueSystem + SpeechBubbleManager pointers |
| `engine/src/ui/ImGuiRenderer.cpp` | Render dialogue box + speech bubbles |
| `editor/src/Application.cpp` | Own DialogueSystem + SpeechBubbleManager, input routing |
| `editor/include/Application.h` | Members |
| `editor/src/input/InputController.cpp` | Dialogue-mode input (Enter, 1-4, Esc) |

### Verification

- [ ] Start conversation with NPC → dialogue box appears, player movement paused
- [ ] Typewriter text effect visible → Enter skips to full text
- [ ] Choices displayed → press 1-4 to select → dialogue branches correctly
- [ ] Press Esc → conversation ends, movement resumes
- [ ] NPC ambient `say()` → speech bubble above head, fades over duration
- [ ] Multiple bubbles from different NPCs → all visible, oldest evicted at cap
- [ ] Goose NPC interaction → dynamic dialogue appears in box
- [ ] Load dialogue tree from JSON → plays correctly through all branches
- [ ] Conversation end emits event → visible in `poll_events`
- [ ] Unit tests: DialogueTree traversal, SpeechBubbleManager lifetime/eviction, DialogueSystem state machine

---

## Implementation Order Recommendation

```
Week 1-2:  Phase 1 (Lights) + Phase 2 (Cameras) in parallel
Week 3:    Phase 3 (NPCs) — depends on 1 & 2
Week 4:    Phase 4 (Dialogue) — depends on 3
```

Each phase should end with passing unit tests and manual verification before moving to the next.

## References

- Entity base class: `engine/include/scene/Entity.h`
- AnimatedVoxelCharacter: `engine/include/scene/AnimatedVoxelCharacter.h`
- EntityRegistry: `engine/include/core/EntityRegistry.h`
- RenderCoordinator: `engine/include/graphics/RenderCoordinator.h`
- VulkanDevice: `engine/include/vulkan/VulkanDevice.h`
- Camera: `engine/include/graphics/Camera.h`
- ImGuiRenderer: `engine/include/ui/ImGuiRenderer.h`
- EngineAPIServer: `engine/src/core/EngineAPIServer.cpp`
- AI system: `editor/include/ai/AISystem.h`, `engine/include/ai/GooseBridge.h`, `engine/include/ai/AICharacterComponent.h`
- StoryDirector: `editor/include/ai/StoryDirector.h`
- Existing recipes: `resources/recipes/`
- Shader pipeline: `build_shaders.bat` → glslangValidator
- UBO struct: `UniformBufferObject` in `VulkanDevice.h`
- Voxel fragment shader: `shaders/voxel.frag`
- Character fragment shader: `shaders/character.frag`
