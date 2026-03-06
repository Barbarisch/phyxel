#include "graphics/Camera.h"

namespace Phyxel {
namespace Graphics {

Camera::Camera(glm::vec3 position, glm::vec3 up, float yaw, float pitch)
    : position(position)
    , worldUp(up)
    , yaw(yaw)
    , pitch(pitch)
    , front(glm::vec3(0.0f, 0.0f, -1.0f))
    , movementSpeed(5.0f)
    , mouseSensitivity(0.1f)
    , zoom(45.0f)
    , mode(CameraMode::FirstPerson)
{
    updateCameraVectors();
}

glm::mat4 Camera::getViewMatrix() const {
    return glm::lookAt(position, position + front, up);
}

void Camera::processKeyboard(int direction, float deltaTime) {
    // Direction: 0 = Forward, 1 = Backward, 2 = Left, 3 = Right, 4 = Up, 5 = Down
    float velocity = movementSpeed * deltaTime;
    if (direction == 0) position += front * velocity;
    if (direction == 1) position -= front * velocity;
    if (direction == 2) position -= right * velocity;
    if (direction == 3) position += right * velocity;
    if (direction == 4) position += worldUp * velocity;
    if (direction == 5) position -= worldUp * velocity;
}

void Camera::processMouseMovement(float xoffset, float yoffset, bool constrainPitch) {
    xoffset *= mouseSensitivity;
    yoffset *= mouseSensitivity;

    yaw += xoffset;
    pitch += yoffset;

    if (constrainPitch) {
        if (pitch > 89.0f) pitch = 89.0f;
        if (pitch < -89.0f) pitch = -89.0f;
    }

    updateCameraVectors();
}

void Camera::processMouseScroll(float yoffset) {
    zoom -= (float)yoffset;
    if (zoom < 1.0f) zoom = 1.0f;
    if (zoom > 45.0f) zoom = 45.0f;
    
    // Also adjust distance for third person
    if (mode == CameraMode::ThirdPerson) {
        distanceFromTarget -= yoffset;
        if (distanceFromTarget < 2.0f) distanceFromTarget = 2.0f;
        if (distanceFromTarget > 15.0f) distanceFromTarget = 15.0f;
    }
}

void Camera::updatePositionFromTarget(const glm::vec3& targetPosition, float offsetHeight) {
    if (mode == CameraMode::FirstPerson) {
        position = targetPosition + glm::vec3(0.0f, offsetHeight, 0.0f);
    } else if (mode == CameraMode::ThirdPerson) {
        // Calculate position based on yaw/pitch and distance
        // We want the camera to be behind the target based on the view direction
        // front vector points FROM camera TO target direction (roughly)
        // Actually front points where we are looking.
        // So camera position = target - front * distance
        
        glm::vec3 targetCenter = targetPosition + glm::vec3(0.0f, offsetHeight, 0.0f);
        position = targetCenter - (front * distanceFromTarget);
    }
}

void Camera::updateCameraVectors() {
    // Calculate the new Front vector
    glm::vec3 newFront;
    newFront.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    newFront.y = sin(glm::radians(pitch));
    newFront.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    front = glm::normalize(newFront);

    // Also re-calculate the Right and Up vector
    right = glm::normalize(glm::cross(front, worldUp));  // Normalize the vectors, because their length gets closer to 0 the more you look up or down which results in slower movement.
    up = glm::normalize(glm::cross(right, front));
}

} // namespace Graphics
} // namespace Phyxel
