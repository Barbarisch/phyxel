#pragma once

#include "Camera.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <string>

namespace Phyxel {
namespace Graphics {

// A CameraRig decides how the camera frames a target (the player) for a given
// look orientation, and which projection that framing implies. It is one half of
// the gameplay camera system (see docs/CameraControlSystem.md); the other half is
// Input::ControlScheme. A single GameplayCameraController drives both, so the
// editor and standalone games share one input->character->camera path instead of
// each hand-wiring (and diverging from) it.
//
// Phase 1 ships FirstPersonRig + ThirdPersonRig (perspective). Overhead/Isometric
// (orthographic) land in Phase 2 along with RenderCoordinator calling projection().
class CameraRig {
public:
    virtual ~CameraRig() = default;

    // Position + orient `cam` to frame `target`, given the desired look angles
    // (degrees, same convention as Camera/InputManager yaw/pitch).
    virtual void update(Camera& cam, const glm::vec3& target,
                        float yaw, float pitch, float dt) = 0;

    // Projection this rig implies. Base = perspective(fov); ortho rigs override.
    // REVERSE-Z (graphics/DepthConvention.h): must match the scene pipelines' GREATER depth test,
    // and the projection is infinite, so `farP` is ignored here.
    virtual glm::mat4 projection(float aspect, float nearP, float farP) const {
        (void)farP;
        return DepthConvention::infiniteReverseZPerspective(glm::radians(fov), aspect, nearP);
    }

    // Common tweaks, no subclass required:
    float eyeHeight     = 0.5f;    // eye / track-point vertical offset
    float distance      = 5.0f;    // third-person / iso boom length
    float fov           = 45.0f;   // perspective rigs
    float orthoScale    = 20.0f;   // ortho rigs (half-height in world units)
    float pitchClampMin = -89.0f;
    float pitchClampMax = 89.0f;
};

// Eye at the target + eyeHeight, looking along yaw/pitch.
class FirstPersonRig : public CameraRig {
public:
    void update(Camera& cam, const glm::vec3& target,
                float yaw, float pitch, float /*dt*/) override {
        // The host owns CameraMode (so e.g. the editor's V toggle isn't fought);
        // the rig only positions/orients and selects the projection.
        cam.setProjectionMode(ProjectionMode::Perspective);
        cam.setYaw(yaw);
        cam.setPitch(pitch);
        cam.setPosition(target + glm::vec3(0.0f, eyeHeight, 0.0f));
    }
};

// Orbit `distance` behind the target at yaw/pitch (the classic over-the-shoulder
// / chase camera; extracts Camera::updatePositionFromTarget's ThirdPerson math).
class ThirdPersonRig : public CameraRig {
public:
    void update(Camera& cam, const glm::vec3& target,
                float yaw, float pitch, float /*dt*/) override {
        // Host owns CameraMode; the rig only positions/orients and selects projection.
        cam.setProjectionMode(ProjectionMode::Perspective);
        cam.setYaw(yaw);
        cam.setPitch(pitch);   // updates cam.getFront()
        const glm::vec3 center = target + glm::vec3(0.0f, eyeHeight, 0.0f);
        cam.setPosition(center - cam.getFront() * distance);
    }
};

// Top-down orthographic view, `distance` above the target. The look is forced
// near-vertical (pitch is clamped just shy of -90deg to avoid the look-at
// gimbal singularity); the input yaw rotates the top-down view. orthoScale
// controls how much of the world is visible.
class OverheadRig : public CameraRig {
public:
    OverheadRig() { distance = 30.0f; orthoScale = 25.0f; }
    void update(Camera& cam, const glm::vec3& target,
                float yaw, float /*pitch*/, float /*dt*/) override {
        cam.setProjectionMode(ProjectionMode::Orthographic);
        cam.setOrthoHalfHeight(orthoScale);
        cam.setYaw(yaw);
        cam.setPitch(-89.0f);   // ~straight down; -90 would make right/up degenerate
        cam.setPosition(target + glm::vec3(0.0f, distance, 0.0f));
    }
};

// Fixed-angle isometric orthographic view (classic ~35.264deg elevation). The
// input yaw rotates the view around the target; pitch is fixed. orthoScale
// controls zoom; `distance` only sets the depth offset (ortho size is scale-only).
class IsometricRig : public CameraRig {
public:
    IsometricRig() { distance = 40.0f; orthoScale = 20.0f; eyeHeight = 0.5f; }
    void update(Camera& cam, const glm::vec3& target,
                float yaw, float /*pitch*/, float /*dt*/) override {
        cam.setProjectionMode(ProjectionMode::Orthographic);
        cam.setOrthoHalfHeight(orthoScale);
        cam.setYaw(yaw);
        cam.setPitch(-35.264f);   // atan(1/sqrt(2)) — true isometric elevation
        const glm::vec3 center = target + glm::vec3(0.0f, eyeHeight, 0.0f);
        cam.setPosition(center - cam.getFront() * distance);
    }
};

// BG3-style TACTICAL camera: a close, angled, PERSPECTIVE over-the-battle view.
//
// Why not OverheadRig: straight-down orthographic (pitch -89, no perspective)
// flattens the scene — characters read as blobs, height and facing vanish, and
// it is genuinely hard to follow a fight in ("hard to follow", user, 2026-08-31).
// This rig keeps the battle legible:
//   * PERSPECTIVE, so depth/height/facing read normally
//   * an ANGLED elevation (default 52deg) instead of vertical
//   * CLOSE (16 units), so combatants are big enough to identify
//   * yaw orbits freely; pitch stays inside a tactical band via the rig's
//     clamps, so the view can be raised/lowered a little but never flips to
//     straight-down or down to ground level
class TacticalRig : public CameraRig {
public:
    TacticalRig() {
        distance      = 14.0f;   // close enough to read faces/gear
        eyeHeight     = 1.2f;    // frame the torso, not the feet
        fov           = 50.0f;   // a touch wide for surrounding context
        pitchClampMin = -68.0f;  // steepest: near-overhead but still angled
        pitchClampMax = -32.0f;  // shallowest: still looking down on the field
    }

    /// FRAME THE ACTION, not just the player. The host sets this each frame to
    /// whoever is currently acting (or the player's target); the rig anchors
    /// between the player and that point and pulls back far enough to hold
    /// both. Without it the camera stares at the player while an enemy 20
    /// units away takes its turn off-screen — you cannot follow the battle.
    /// weight 0 disables the behavior entirely (plain follow-the-player).
    void setFocus(const glm::vec3& worldPoint, float weight) {
        focusPoint  = worldPoint;
        focusWeight = glm::clamp(weight, 0.0f, 1.0f);
    }
    void clearFocus() { focusWeight = 0.0f; }

    void update(Camera& cam, const glm::vec3& target,
                float yaw, float pitch, float /*dt*/) override {
        cam.setProjectionMode(ProjectionMode::Perspective);
        cam.setYaw(yaw);
        // The controller clamps to [pitchClampMin, pitchClampMax] before this
        // call; default to a good tactical angle when the incoming look is
        // outside the band (e.g. entering combat from a level gaze).
        const float p = (pitch < pitchClampMin || pitch > pitchClampMax) ? -48.0f : pitch;
        cam.setPitch(p);

        glm::vec3 anchor = target;
        float boom = distance;
        if (focusWeight > 0.0f) {
            anchor = glm::mix(target, focusPoint, focusWeight);
            // Pull back so both the player and the focus stay in frame: half
            // the separation, on top of the base boom.
            const glm::vec3 sep = focusPoint - target;
            boom += glm::length(glm::vec2(sep.x, sep.z)) * 0.5f;
        }
        boom = glm::clamp(boom, 8.0f, 40.0f);

        const glm::vec3 center = anchor + glm::vec3(0.0f, eyeHeight, 0.0f);
        cam.setPosition(center - cam.getFront() * boom);
    }

    glm::vec3 focusPoint{0.0f};
    float     focusWeight = 0.0f;
};

// Name -> rig factory for data-driven selection (game.json camera.mode, the
// set_camera MCP tool, the editor camera panel). Returns nullptr for an unknown
// name so callers can fall back to a default. Accepts snake_case / PascalCase /
// short aliases. Custom rigs that subclass CameraRig are used via
// GameplayCameraController::setRig() directly; extend this if one needs a name.
inline std::unique_ptr<CameraRig> makeCameraRig(const std::string& name) {
    if (name == "first_person" || name == "FirstPerson" || name == "first")
        return std::make_unique<FirstPersonRig>();
    if (name == "third_person" || name == "ThirdPerson" || name == "third")
        return std::make_unique<ThirdPersonRig>();
    if (name == "overhead" || name == "Overhead" || name == "top_down")
        return std::make_unique<OverheadRig>();
    if (name == "isometric" || name == "Isometric" || name == "iso")
        return std::make_unique<IsometricRig>();
    if (name == "tactical" || name == "Tactical" || name == "bg3")
        return std::make_unique<TacticalRig>();
    return nullptr;
}

} // namespace Graphics
} // namespace Phyxel
