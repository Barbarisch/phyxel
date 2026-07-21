# Phyxel Keybindings

> Verified against `editor/src/input/InputController.cpp` (`setupKeyboardBindings`/
> `setupMouseBindings`), `engine/include/input/ControlScheme.h`, and `engine/src/core/
> GameSettings.cpp`. Two behaviors changed significantly from earlier versions of this doc:
> **Left Click no longer breaks voxels** (breaking moved to **B**; plain LMB is now the
> attack/spell-cast/furniture-grab-throw action — "Voxel BREAKING on plain left-click is
> retired" per the source comment), and **T/Shift+T are not bound to any key** (template
> spawning is reachable only via the in-engine Template Spawner ImGui panel or the
> `spawn_template` MCP/HTTP tool).

## General
- **ESC**: Toggle Pause Menu (freeze world, show resume/settings/quit)
- **F1**: Toggle Performance Overlay
- **F2**: Save World (Not implemented)
- **F3**: Toggle Force Debug Visualization
- **F4**: Toggle Debug Rendering
- **Ctrl + F4**: Cycle Debug Visualization Mode
- **F5**: Toggle Raycast Visualization (also shows NPC FOV cones when perception is active — green
  cone/edge when no threat detected, red when a threat is sensed; gated on `PerceptionComponent::
  debugConeDraw`, wired in `PatrolBehavior`/`BehaviorTreeBehavior`)
- **Shift + F5**: Cycle Raycast Target Mode
- **F6**: Toggle Lighting Controls
- **F7**: Toggle Profiler
- **F8**: Spawn AI NPC (Goose/AI system test spawn)
- **F9**: Toggle AI System (start/stop the Goose bridge)
- **F10**: Toggle Game Menu (custom data-driven UI, `"game_menu"`)
- **F11**: Toggle Character Customizer
- **F12**: Toggle Interaction Point Tuner
- **` (Grave Accent)**: Toggle Scripting Console

## Camera
- **V**: Toggle Camera Mode (First/Third/Free)
- **Tab**: Cycle Camera Slot (next)
- **Shift + Tab**: Cycle Camera Slot (previous)

## World Interaction
- **C**: Place Cube
- **Ctrl + C**: Place Subcube
- **Alt + C**: Place Microcube
- **B**: Break Voxel (cube/subcube/microcube — dispatches by hovered voxel's actual size). Also
  double-bound to Previous Animation when controlling the animated character outside Free camera
  mode (see Character Control below) — the two behaviors both fire on a B press in that state.
- **Ctrl + Left Click**: Subdivide Cube
- **Alt + Left Click**: Subdivide Subcube
- **Middle Click**: Subdivide Cube
- **G**: Spawn Dynamic Subcube (Placeholder — logs "not yet implemented", does not spawn anything)
- **P**: Toggle Template Preview
- **[**: Decrease Spawn Speed
- **]**: Increase Spawn Speed
- **-**: Decrease Ambient Light
- **=**: Increase Ambient Light
- **O**: Toggle Breaking Forces
- **Up Arrow**: Cycle voxel target mode toward larger (Micro→Sub→Cube)
- **Down Arrow**: Cycle voxel target mode toward smaller (Cube→Sub→Micro)

> Static/dynamic template spawning (previously **T** / **Shift+T**) is not bound to a key in the
> current input map — use the Template Spawner ImGui panel or the `spawn_template` tool.

## Character Control
- **K**: only one control target exists (`AnimatedCharacter`) — pressing K just re-asserts it;
  it no longer cycles Physics/Spider/Animated (those ragdoll types were removed with Bullet).
- **W/A/S/D**: Movement
- **Shift**: Sprint
- **Space**: Jump (Animated Character)
- **Left Click**: Attack (Animated Character) — spell-cast / combat-click / furniture
  grab-throw depending on mode; this is also where voxel-breaking used to live (moved to B)
- **Ctrl**: Crouch (Animated Character)
- **E**: Interact / Grab Furniture (talk to NPC, or grab/release active furniture at the crosshair)
- **X**: Derez Character (Explode into physics objects)
- **N**: Next Animation (only when controlling the animated character, camera not in Free mode,
  and not in anim-editor mode)
- **B**: Previous Animation (same conditions as N — see the World Interaction note above about
  the overlap with Break Voxel)

## Dialogue
- **Enter**: Advance Dialogue (when a non-AI dialogue is active)
- **1–4**: Select dialogue choice 1–4 (when a choice-selection prompt is active)

## Asset Editor Mode (`--asset-editor <file.txt>`)
- **C / Ctrl+C / Alt+C**: Place cube / subcube / microcube
- **Left Click**: Break voxel (floor at Y=15 is protected)
- **H**: Toggle humanoid reference character
- **Ctrl+S**: Save template back to file
- **Right Mouse**: Hold to enter free-look camera mode
- *(ImGui panel hover blocks all voxel interaction)*

## Anim Editor Mode (`--anim-editor <file.anim>`)
- **Ctrl+S**: Save modified bone sizes back to `.anim` file
- **Right Mouse**: Hold to enter free-look camera mode
- *(All voxel interaction disabled in this mode)*
- *(Use the ImGui panel for animation preview and bone scale sliders)*
