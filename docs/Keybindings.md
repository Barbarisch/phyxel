# Phyxel Keybindings

## General
- **ESC**: Toggle Pause Menu (freeze world, show resume/settings/quit)
- **F1**: Toggle Performance Overlay
- **F2**: Save World (Not implemented)
- **F3**: Toggle Force Debug Visualization
- **F4**: Toggle Debug Rendering
- **Ctrl + F4**: Cycle Debug Visualization Mode
- **F5**: Toggle Raycast Visualization (also shows NPC FOV cones when perception is active)
- **Shift + F5**: Cycle Raycast Target Mode
- **F6**: Toggle Lighting Controls
- **F7**: Toggle Profiler
- **` (Grave Accent)**: Toggle Scripting Console

## Camera
- **V**: Toggle Camera Mode (First/Third/Free)

## World Interaction
- **C**: Place Cube
- **Ctrl + C**: Place Subcube
- **Alt + C**: Place Microcube
- **Left Click**: Break Voxel
- **Ctrl + Left Click**: Subdivide Cube
- **Alt + Left Click**: Subdivide Subcube
- **Middle Click**: Subdivide Cube
- **G**: Spawn Dynamic Subcube (Placeholder)
- **T**: Spawn Static Template
- **Shift + T**: Spawn Dynamic Template
- **P**: Toggle Template Preview
- **[**: Decrease Spawn Speed
- **]**: Increase Spawn Speed
- **-**: Decrease Ambient Light
- **=**: Increase Ambient Light
- **O**: Toggle Breaking Forces

## Character Control
- **K**: Toggle Character Control (Physics/Spider/Animated)
- **W/A/S/D**: Movement
- **Shift**: Sprint
- **Space**: Jump (Animated Character)
- **Left Click**: Attack (Animated Character)
- **Ctrl**: Crouch (Animated Character)
- **X**: Derez Character (Explode into physics objects)
- **N**: Next Animation (Preview Mode)
- **B**: Previous Animation (Preview Mode)

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
