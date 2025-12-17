#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

namespace VulkanCube {
namespace Graphics {

enum class CameraMode {
    FirstPerson,
    ThirdPerson,
    Free
};

class Camera {
public:
    // Constructor with vectors
    Camera(glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f), float yaw = -90.0f, float pitch = 0.0f);

    // Returns the view matrix calculated using Euler Angles and the LookAt Matrix
    glm::mat4 getViewMatrix() const;

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

    // Setters
    void setPosition(const glm::vec3& newPosition) { position = newPosition; }
    void setFront(const glm::vec3& newFront) { front = newFront; updateCameraVectors(); } // Recalculate right/up
    void setMode(CameraMode newMode) { mode = newMode; }
    void setYaw(float newYaw) { yaw = newYaw; updateCameraVectors(); }
    void setPitch(float newPitch) { pitch = newPitch; updateCameraVectors(); }

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
};

} // namespace Graphics
} // namespace VulkanCube
