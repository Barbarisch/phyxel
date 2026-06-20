#pragma once

#include "InputManager.h"
#include <GLFW/glfw3.h>
#include <memory>
#include <string>

namespace Phyxel {
namespace Input {

// What a control scheme extracts from raw input each frame: movement to feed the
// character, the look angles for the camera, and one-shot action flags. The
// GameplayCameraController consumes this; see docs/CameraControlSystem.md.
//
// Movement sign convention matches the engine (AnimatedVoxelCharacter): NEGATIVE
// `forward` = move forward. `turn` rotates the body; `strafe` sidesteps.
struct ControlIntent {
    float forward = 0.0f;
    float strafe  = 0.0f;
    float turn    = 0.0f;

    // Absolute look (degrees, Camera/InputManager convention). Schemes read this
    // from the mouse-driven InputManager so look stays single-source.
    float yaw   = 0.0f;
    float pitch = 0.0f;

    bool coupleFacingToYaw = false;  // lock body heading to camera yaw (FPS)?

    bool sprint = false;
    bool crouch = false;
    bool jump   = false;   // one-shot: pressed this frame
    bool attack = false;   // light attack (LMB) — edge-guarded by the controller
    bool heavy  = false;   // heavy attack (Shift+LMB) — edge-guarded by the controller
    bool block  = false;   // guard held (RMB in FPS; LEFT_ALT in tank, where RMB orbits)
    bool dodge  = false;   // dodge/roll (R) — one-shot, edge-guarded by the controller
};

// Strategy: maps InputManager state to a ControlIntent.
class ControlScheme {
public:
    virtual ~ControlScheme() = default;
    virtual ControlIntent sample(InputManager& input, float dt) = 0;

    // FPS-style schemes want always-on mouse look (no RMB hold); the controller
    // calls input.setMouseCaptured() accordingly. Editor-style schemes leave it
    // RMB-gated.
    virtual bool wantsAlwaysOnLook() const { return false; }

    float mouseSensitivity = 0.1f;  // reserved (InputManager owns sensitivity today)
};

// Classic FPS: mouse turns the view, W/S walk along the look direction, A/D
// strafe, body heading locked to camera yaw. Matches the verified MazeRunner
// controls (2026-06-08).
class FpsScheme : public ControlScheme {
public:
    ControlIntent sample(InputManager& input, float /*dt*/) override {
        ControlIntent in;
        // Movement/jump/sprint/crouch flow through the rebindable action map
        // (InputManager seeds defaults: MoveForward=W, Jump=Space, ...). The
        // esoteric scheme keys (dodge/block) stay raw until they're added to the
        // settings vocabulary.
        if (input.isActionPressed("MoveForward"))  in.forward -= 1.0f;
        if (input.isActionPressed("MoveBackward")) in.forward += 1.0f;
        if (input.isActionPressed("MoveLeft"))     in.strafe  -= 1.0f;
        if (input.isActionPressed("MoveRight"))    in.strafe  += 1.0f;
        in.sprint = input.isActionPressed("Sprint");
        in.crouch = input.isActionPressed("Crouch");
        in.jump   = input.isActionPressed("Jump");
        {
            const bool lmb   = input.isMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT);
            const bool shift = in.sprint;
            in.attack = lmb && !shift;
            in.heavy  = lmb && shift;
        }
        in.block  = input.isMouseButtonPressed(GLFW_MOUSE_BUTTON_RIGHT);
        in.dodge  = input.isKeyPressed(GLFW_KEY_R);
        in.yaw    = input.getYaw();
        in.pitch  = input.getPitch();
        in.coupleFacingToYaw = true;
        return in;
    }
    bool wantsAlwaysOnLook() const override { return true; }
};

// Tank / chase: A/D turn the body, W/S walk forward/back, Q strafes; the camera
// orbits with the mouse only while RMB is held. Matches the editor's animated-
// character control (Application.cpp ~3441) including its 0.5/1.0 walk/sprint
// move magnitude.
class TankScheme : public ControlScheme {
public:
    ControlIntent sample(InputManager& input, float /*dt*/) override {
        ControlIntent in;
        // Rebindable actions via the InputManager action map; A/D become TURN
        // here (vs. strafe in FPS) — same physical "MoveLeft"/"MoveRight" keys,
        // different scheme interpretation. Q strafe stays raw (not yet in the
        // settings vocabulary).
        const bool sprint = input.isActionPressed("Sprint");
        const float mag = sprint ? 1.0f : 0.5f;
        if (input.isActionPressed("MoveForward"))  in.forward -= mag;
        if (input.isActionPressed("MoveBackward")) in.forward += mag;
        if (input.isActionPressed("MoveLeft"))     in.turn    -= 1.0f;
        if (input.isActionPressed("MoveRight"))    in.turn    += 1.0f;
        if (input.isKeyPressed(GLFW_KEY_Q))        in.strafe  -= mag;
        in.sprint = sprint;
        in.crouch = input.isActionPressed("Crouch");
        in.jump   = input.isActionPressed("Jump");
        {
            const bool lmb = input.isMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT);
            in.attack = lmb && !sprint;
            in.heavy  = lmb && sprint;
        }
        // RMB is the camera-orbit hold in tank mode, so guard goes on ALT.
        in.block  = input.isKeyPressed(GLFW_KEY_LEFT_ALT);
        in.dodge  = input.isKeyPressed(GLFW_KEY_R);
        in.yaw    = input.getYaw();
        in.pitch  = input.getPitch();
        in.coupleFacingToYaw = false;
        return in;
    }
};

// Name -> scheme factory for data-driven selection (game.json
// camera.controlScheme, the set_camera MCP tool, the editor panel). Returns
// nullptr for an unknown name so callers can fall back to a default.
inline std::unique_ptr<ControlScheme> makeControlScheme(const std::string& name) {
    if (name == "fps" || name == "FPS" || name == "first_person")
        return std::make_unique<FpsScheme>();
    if (name == "tank" || name == "Tank" || name == "third_person")
        return std::make_unique<TankScheme>();
    return nullptr;
}

} // namespace Input
} // namespace Phyxel
