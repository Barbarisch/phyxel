# Physics Character Documentation

## Overview

The `PhysicsCharacter` class represents a significant departure from the traditional `btKinematicCharacterController` approach. Instead of a kinematic object that pushes other objects around, the `PhysicsCharacter` is a fully physical **Active Ragdoll**. It uses rigid bodies, constraints, and motors to balance, walk, and interact with the world.

This approach allows for:
- Realistic interaction with other physics objects (can be knocked over, can push things with weight).
- Procedural animation (legs move based on physics motors).
- "Emergent" gameplay behaviors.

## Architecture

The character is composed of multiple `btRigidBody` parts connected by constraints.

### Body Parts
1.  **Main Body (Capsule)**: The central mass of the character. It floats above the ground.
2.  **Legs (Boxes)**: Connected to the main body via Hinge Constraints.

### Constraints
-   **Hinge Constraints**: Connect the legs to the body.
    -   **Motors**: The hinges have motors enabled. By oscillating the target velocity of these motors, we create a walking motion.
    -   **Limits**: The hinges have angular limits to prevent the legs from spinning 360 degrees.
    -   **Axis**: The hinge axis is aligned with the global X-axis (Pitch) to allow forward/backward leg movement.

## Movement Logic

### 1. Upright Balance (PID Controller)
Since the character is a rigid body, it would naturally fall over. We use a **PID Controller** (Proportional-Integral-Derivative) to keep the capsule upright.

-   **Mechanism**: Every frame, we calculate the angle deviation from the "Up" vector (0, 1, 0).
-   **Correction**: We apply a torque to the main body to correct this deviation.
-   **Code**: `PhysicsCharacter::keepUpright()`

### 2. Locomotion (Walk Cycle)
Movement is achieved by driving the leg motors.

-   **State Machine**:
    -   `Idle`: Motors lock legs in place. Braking friction is applied.
    -   `Walking`: Motors oscillate based on a sine wave (`m_walkCycleTime`).
    -   `Jumping`: An upward impulse is applied.
-   **Ground Detection**: A raycast (`checkGroundStatus`) determines if the character is touching the ground. This enables/disables the walk cycle and jump ability.

### 3. Friction & Braking
To prevent the "ice skating" effect common in physics characters:
-   **High Friction**: The leg materials have high friction (1.5f).
-   **Active Braking**: When in `Idle` state and grounded, we apply a counter-force to the velocity to bring the character to a stop quickly.

## Tuning Parameters

These values are critical for the "feel" of the character. They are defined in `src/scene/PhysicsCharacter.cpp`.

| Parameter | Value | Description |
| :--- | :--- | :--- |
| **Mass** | 50.0kg | Mass of the main body. |
| **Leg Mass** | 10.0kg | Mass of each leg. |
| **Friction** | 1.5f | High friction to grip the ground. |
| **Jump Impulse** | 10.0f | Multiplier for the jump force. |
| **PID P-Gain** | 1000.0f | Strength of the upright correction. |
| **PID D-Gain** | 100.0f | Damping of the upright correction (prevents oscillation). |
| **Motor Target Vel** | 3.0f | Max speed of the leg swing. |
| **Motor Max Impulse** | 50.0f | Strength of the leg motor. |

## Current Status (as of Dec 2025)

### Implemented
-   [x] **Refactor**: Renamed `VoxelCharacter` to `PhysicsCharacter`.
-   [x] **PID Balance**: Character stays upright.
-   [x] **Leg Animation**: Legs swing back and forth using motors.
-   [x] **Jumping**: Impulse-based jumping with ground check.
-   [x] **Tuning**: Fixed sliding issues and low jump height.
-   [x] **Axis Fix**: Rotated hinge constraints so legs swing forward/backward correctly.

### Known Issues / Todo
-   **Knees**: Currently the legs are single rigid bodies. Adding knees would improve realism.
-   **Arms**: No arms are currently simulated.
-   **Turning**: Turning is currently handled by rotating the rigid body directly or applying torque. It needs smoothing.
-   **Stairs**: The capsule collider handles small bumps, but dedicated step-up logic might be needed for steep stairs.

## How to Resume Work

1.  **Open** `src/scene/PhysicsCharacter.cpp`.
2.  **Tweak** the values in the constructor or `update` method to adjust the feel.
3.  **Debug**: Use `build_and_test.ps1` to compile and run.
4.  **Visuals**: The rendering is currently just drawing the collision shapes. See `RenderCoordinator::renderPhysicsCharacter`.
