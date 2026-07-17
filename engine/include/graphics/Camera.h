#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

namespace Phyxel {
namespace Graphics {

enum class CameraMode {
    FirstPerson,
    ThirdPerson,
    Free
};

// How the camera projects the scene. Perspective for first/third-person; the
// overhead and isometric camera rigs switch this to Orthographic. The renderer
// reads it via getProjectionMatrix(). See docs/CameraControlSystem.md.
enum class ProjectionMode {
    Perspective,
    Orthographic
};

class Camera {
public:
    // Constructor with vectors
    Camera(glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f), float yaw = -90.0f, float pitch = 0.0f);

    // Returns the view matrix calculated using Euler Angles and the LookAt Matrix
    glm::mat4 getViewMatrix() const;

    // Camera-relative rendering (docs/CameraRelativeRendering.md): rotation-only view with
    // the eye at the ORIGIN. All world translations handed to the GPU must then be
    // (worldPos - cameraPos), computed in doubles via relativeTo(). At continental
    // coordinates (~60k units, float ULP ~4 mm) the classic eye-at-world lookAt cancels
    // catastrophically in world->clip; this keeps every GPU-side magnitude within render
    // distance where float is exact to sub-micron.
    glm::mat4 getRelativeViewMatrix() const {
        return glm::lookAt(glm::vec3(0.0f), front, up);
    }
    // (worldPos - cameraPos) with the subtraction in double precision, truncated last.
    glm::vec3 relativeTo(const glm::dvec3& worldPos) const {
        return glm::vec3(worldPos - glm::dvec3(position));
    }
    glm::vec3 relativeTo(const glm::vec3& worldPos) const {
        return relativeTo(glm::dvec3(worldPos));
    }

    // Processes input received from any keyboard-like input system
    void processKeyboard(int direction, float deltaTime);

    // Processes input received from a mouse input system
    void processMouseMovement(float xoffset, float yoffset, bool constrainPitch = true);

    // Processes input received from a mouse scroll-wheel event
    void processMouseScroll(float yoffset);

    // Updates the camera position based on a target (for ThirdPerson/FirstPerson attached to player)
    void updatePositionFromTarget(const glm::vec3& targetPosition, float offsetHeight = 0.0f);

    // Getters
    glm::vec3 getPosition() const { return position; }
    glm::vec3 getFront() const { return front; }
    glm::vec3 getUp() const { return up; }
    glm::vec3 getRight() const { return right; }
    float getYaw() const { return yaw; }
    float getPitch() const { return pitch; }
    CameraMode getMode() const { return mode; }
    float getZoom() const { return zoom; }
    float getDistanceFromTarget() const { return distanceFromTarget; }
    ProjectionMode getProjectionMode() const { return projectionMode; }
    float getOrthoHalfHeight() const { return orthoHalfHeight; }

    // The projection matrix for the current mode, with the Vulkan Y-flip already
    // applied (so it matches the renderer's existing convention). Perspective
    // keeps the engine's fixed 45deg FOV; orthographic uses orthoHalfHeight as the
    // visible half-height in world units (width follows the aspect ratio).
    glm::mat4 getProjectionMatrix(float aspect, float nearP, float farP) const {
        glm::mat4 proj;
        if (projectionMode == ProjectionMode::Orthographic) {
            const float h = orthoHalfHeight;
            const float w = h * aspect;
            // _ZO = [0,1] depth (Vulkan). Plain glm::ortho gives OpenGL [-1,1];
            // ortho depth is LINEAR, so with [-1,1] everything nearer than the
            // mid-plane lands at z<0 and Vulkan clips it (the whole scene goes
            // black). Perspective tolerates [-1,1] only because its depth is
            // non-linear, so we keep glm::perspective for that path.
            proj = glm::orthoRH_ZO(-w, w, -h, h, nearP, farP);
        } else {
            proj = glm::perspective(glm::radians(45.0f), aspect, nearP, farP);
        }
        proj[1][1] *= -1; // Vulkan flips Y vs OpenGL
        return proj;
    }

    // Setters
    void setPosition(const glm::vec3& newPosition) { position = newPosition; }
    void setFront(const glm::vec3& newFront) { front = newFront; updateCameraVectors(); } // Recalculate right/up
    void setMode(CameraMode newMode) { mode = newMode; }
    void setYaw(float newYaw) { yaw = newYaw; updateCameraVectors(); }
    void setPitch(float newPitch) { pitch = newPitch; updateCameraVectors(); }
    void setDistanceFromTarget(float dist) { distanceFromTarget = dist; }
    void setProjectionMode(ProjectionMode m) { projectionMode = m; }
    void setOrthoHalfHeight(float h) { orthoHalfHeight = h; }
    void setZoom(float fov) { zoom = fov; if (zoom < 1.0f) zoom = 1.0f; if (zoom > 120.0f) zoom = 120.0f; }
    void setMouseSensitivity(float s) { mouseSensitivity = s; }

private:
    // Calculates the front vector from the Camera's (updated) Euler Angles
    void updateCameraVectors();

    // Camera Attributes
    glm::vec3 position;
    glm::vec3 front;
    glm::vec3 up;
    glm::vec3 right;
    glm::vec3 worldUp;

    // Euler Angles
    float yaw;
    float pitch;

    // Camera options
    float movementSpeed;
    float mouseSensitivity;
    float zoom;
    
    // Third Person options
    float distanceFromTarget = 5.0f;

    CameraMode mode;
    ProjectionMode projectionMode = ProjectionMode::Perspective;
    float orthoHalfHeight = 20.0f;  // visible half-height (world units) in ortho mode
};

} // namespace Graphics
} // namespace Phyxel
