#pragma once

#include "graphics/Camera.h"
#include "graphics/CameraRig.h"
#include "input/InputManager.h"
#include "input/ControlScheme.h"
#include "scene/AnimatedVoxelCharacter.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <memory>

namespace Phyxel {
namespace Core {

// The single shared driver for gameplay camera + character control. A host (the
// editor OR a standalone game) picks a CameraRig + ControlScheme and calls
// update() once per frame; this replaces the hand-wired input->character->camera
// loops that used to be copied (and diverge) between hosts. See
// docs/CameraControlSystem.md.
//
// Rig and scheme are hot-swappable on any frame (safe — they hold no per-frame
// state that must persist). Look angles flow from the mouse-driven InputManager
// through the scheme, so look stays single-source.
class GameplayCameraController {
public:
    void setRig(std::unique_ptr<Graphics::CameraRig> rig)        { rig_ = std::move(rig); }
    void setScheme(std::unique_ptr<Input::ControlScheme> scheme) { scheme_ = std::move(scheme); }
    Graphics::CameraRig*  rig()    const { return rig_.get(); }
    Input::ControlScheme* scheme() const { return scheme_.get(); }
    bool ready() const { return rig_ && scheme_; }

    // Sample input -> drive character -> frame the camera. `character` may be null
    // (camera-only); `dt` is the frame delta in seconds.
    void update(float dt, Input::InputManager& input,
                Scene::AnimatedVoxelCharacter* character, Graphics::Camera& camera) {
        if (!rig_ || !scheme_) return;

        if (scheme_->wantsAlwaysOnLook()) input.setMouseCaptured(true);

        const Input::ControlIntent in = scheme_->sample(input, dt);

        if (character) {
            character->setControlInput(in.forward, in.turn, in.strafe);
            character->setSprint(in.sprint);
            character->setCrouch(in.crouch);

            // Edge-trigger jump so holding the key doesn't re-fire every frame
            // (matches the editor's spaceWasPressed guard).
            if (in.jump) { if (!jumpHeld_) character->jump(); jumpHeld_ = true; }
            else         { jumpHeld_ = false; }

            if (in.attack) character->attack();

            if (in.coupleFacingToYaw)
                character->setFacingYaw(glm::radians(90.0f - in.yaw));

            character->update(dt);
        }

        const glm::vec3 target = character ? character->getCameraTrackPosition()
                                           : camera.getPosition();
        const float pitch = std::clamp(in.pitch, rig_->pitchClampMin, rig_->pitchClampMax);
        rig_->update(camera, target, in.yaw, pitch, dt);
    }

private:
    std::unique_ptr<Graphics::CameraRig>  rig_;
    std::unique_ptr<Input::ControlScheme> scheme_;
    bool jumpHeld_ = false;
};

} // namespace Core
} // namespace Phyxel
