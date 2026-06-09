#pragma once

#include "Camera.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

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
    virtual glm::mat4 projection(float aspect, float nearP, float farP) const {
        return glm::perspective(glm::radians(fov), aspect, nearP, farP);
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

} // namespace Graphics
} // namespace Phyxel
