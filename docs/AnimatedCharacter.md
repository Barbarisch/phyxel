# Animated Character State Machine Documentation

## Overview
The `AnimatedVoxelCharacter` uses a finite state machine (FSM) to manage its behavior, movement, and animation playback. This document describes the available states, transitions, and how input affects the character.

## States

| State | Description | Animation |
|-------|-------------|-----------|
| **Idle** | Character is standing still. | `idle` |
| **StartWalk** | Transition state when starting to move forward. | `start_walking` |
| **Walk** | Standard forward movement. | `walk` |
| **Run** | Fast forward movement (Sprint). | `run` / `fast_run` |
| **Jump** | Upward movement in the air. | `jump` |
| **Fall** | Downward movement in the air. | `jump_down` |
| **Land** | Impact with ground after falling. | `landing` |
| **Crouch** | Transition to crouching posture. | `standing_to_crouched` |
| **CrouchIdle** | Stationary while crouching. | `standing_to_crouched` (held) |
| **CrouchWalk** | Moving while crouching. | `crouched_walking` |
| **StandUp** | Transition from crouch to standing. | `crouch_to_stand` |
| **Attack** | Performing an attack action. | `attack` |
| **TurnLeft/Right** | Rotating in place. | `left_turn` / `right_turn` |
| **StrafeLeft/Right** | Moving sideways (Running/Fast). | `left_strafe` / `right_strafe` |
| **WalkStrafeLeft/Right** | Moving sideways (Walking/Slow). | `left_strafe_walk` / `right_strafe_walk` |
| **Preview** | Debug state for previewing animations. | (Selected Animation) |

## State Transitions

The state machine is updated every frame in `AnimatedVoxelCharacter::updateStateMachine`.

### Priority Actions
1.  **Jump**: Triggered by `jumpRequested`. Transitions to `Jump`.
2.  **Attack**: Triggered by `attackRequested`. Transitions to `Attack`.
3.  **Fall**: Triggered if vertical velocity < -5.0f. Transitions to `Fall`.

### Movement Logic
If no priority action is active, the state is determined by input:

*   **Crouching**:
    *   If `isCrouching` is true:
        *   Moving -> `CrouchWalk`
        *   Stationary -> `CrouchIdle` (via `Crouch` transition)
    *   If `isCrouching` becomes false -> `StandUp`

*   **Standing Movement**:
    *   **Forward Input**:
        *   High input (> 0.6) -> `Run`
        *   Low input -> `Walk` (via `StartWalk` transition from Idle)
    *   **Strafe Input**:
        *   If also moving forward -> `WalkStrafeLeft/Right`
        *   If only strafing -> `StrafeLeft/Right`
    *   **Turn Input**:
        *   If stationary -> `TurnLeft/Right`
    *   **No Input** -> `Idle`

### Common Issues & Debugging

#### "Stuck in Idle" or "Flickering Movement"
If the character seems to ignore input or flicker between moving and stopping, check the `switch` statement in `updateStateMachine`.
**Cause**: If a state (e.g., `StrafeLeft`) is missing from the switch case, the `default` case will likely reset the state to `Idle` on the next frame.
**Fix**: Ensure all enum values in `AnimatedCharacterState` have a corresponding `case` in the state machine logic.

#### Animation Mismatch
If the wrong animation plays for a state:
1.  Check `update` method where `targetAnim` is selected based on `currentState`.
2.  Check `loadModel` to ensure the animation file actually contains a clip with that name.
3.  Check the fuzzy matching logic in `update` (e.g., ensuring "walk" doesn't match "strafe_walk").

## Input Handling
Input is passed from `Application::handleInput` to `AnimatedVoxelCharacter::setControlInput`.
*   `forward`: -1.0 to 1.0
*   `turn`: -1.0 to 1.0
*   `strafe`: -1.0 to 1.0

The character physics controller (`controllerBody`) is moved based on these inputs relative to the camera/character orientation.
