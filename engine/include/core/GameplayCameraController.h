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
#include <string>

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
    void setRig(std::unique_ptr<Graphics::CameraRig> rig)        { rig_ = std::move(rig); rigName_.clear(); }
    void setScheme(std::unique_ptr<Input::ControlScheme> scheme) { scheme_ = std::move(scheme); schemeName_.clear(); }
    Graphics::CameraRig*  rig()    const { return rig_.get(); }
    Input::ControlScheme* scheme() const { return scheme_.get(); }
    bool ready() const { return rig_ && scheme_; }

    // Name-based switching (live-safe on any frame). Returns false and leaves the
    // current rig/scheme in place if the name is unknown. The name is remembered
    // so hosts (editor panel, MCP) can show and diff the active selection.
    bool setRigByName(const std::string& name) {
        auto r = Graphics::makeCameraRig(name);
        if (!r) return false;
        rig_ = std::move(r);
        rigName_ = name;
        return true;
    }
    bool setSchemeByName(const std::string& name) {
        auto s = Input::makeControlScheme(name);
        if (!s) return false;
        scheme_ = std::move(s);
        schemeName_ = name;
        return true;
    }
    const std::string& rigName() const { return rigName_; }
    const std::string& schemeName() const { return schemeName_; }

    // Sample input -> drive character -> frame the camera. `character` may be null
    // (camera-only); `dt` is the frame delta in seconds.
    //
    // advanceCharacter: when true (default) the controller calls character->update(dt)
    // itself. Hosts that already advance the character elsewhere (e.g. the editor's
    // entity loop) pass false so it isn't double-updated; the control inputs set here
    // then apply on that host's next character update.
    void update(float dt, Input::InputManager& input,
                Scene::AnimatedVoxelCharacter* character, Graphics::Camera& camera,
                bool advanceCharacter = true) {
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

            // Light/heavy attacks are edge-triggered (a held button is one
            // press); the FSM buffers mid-swing presses for chain links.
            if (in.attack) { if (!attackHeld_) character->lightAttack(); attackHeld_ = true; }
            else           { attackHeld_ = false; }
            if (in.heavy)  { if (!heavyHeld_) character->heavyAttack(); heavyHeld_ = true; }
            else           { heavyHeld_ = false; }

            // Guard is a held stance.
            character->setBlocking(in.block);

            if (in.coupleFacingToYaw)
                character->setFacingYaw(glm::radians(90.0f - in.yaw));

            if (advanceCharacter) character->update(dt);
        }

        const glm::vec3 target = character ? character->getCameraTrackPosition()
                                           : camera.getPosition();
        const float pitch = std::clamp(in.pitch, rig_->pitchClampMin, rig_->pitchClampMax);
        rig_->update(camera, target, in.yaw, pitch, dt);
    }

private:
    std::unique_ptr<Graphics::CameraRig>  rig_;
    std::unique_ptr<Input::ControlScheme> scheme_;
    std::string rigName_;     // last name passed to setRigByName (empty if setRig used)
    std::string schemeName_;  // last name passed to setSchemeByName
    bool jumpHeld_   = false;
    bool attackHeld_ = false;
    bool heavyHeld_  = false;
};

} // namespace Core
} // namespace Phyxel
