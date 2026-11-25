#include "scene/VoxelRaycaster.h"
#include "core/ChunkManager.h"
#include "ui/WindowManager.h"
#include "utils/Logger.h"
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <iostream>

namespace Phyxel {

bool VoxelRaycaster::rayAABBIntersect(
    const glm::vec3& rayOrigin,
    const glm::vec3& rayDir,
    const glm::vec3& aabbMin,
    const glm::vec3& aabbMax,
    float& distance
) const {
    // Standard ray-AABB intersection test
    glm::vec3 invDir = 1.0f / rayDir;
    glm::vec3 t1 = (aabbMin - rayOrigin) * invDir;
    glm::vec3 t2 = (aabbMax - rayOrigin) * invDir;
    
    glm::vec3 tMin = glm::min(t1, t2);
    glm::vec3 tMax = glm::max(t1, t2);
    
    float tNear = glm::max(glm::max(tMin.x, tMin.y), tMin.z);
    float tFar = glm::min(glm::min(tMax.x, tMax.y), tMax.z);
    
    if (tNear > tFar || tFar < 0.0f) {
        return false; // No intersection
    }
    
    distance = tNear > 0.0f ? tNear : tFar;
    return true;
}

glm::vec3 VoxelRaycaster::screenToWorldRay(
    double mouseX,
    double mouseY,
    const glm::vec3& cameraPos,
    const glm::vec3& cameraFront,
    const glm::vec3& cameraUp,
    WindowManagerAccessFunc getWindowManager
) const {
    UI::WindowManager* windowManager = getWindowManager();
    if (!windowManager) {
        return glm::vec3(0); // Invalid ray
    }
    
    // Convert mouse coordinates to normalized device coordinates
    float x = (2.0f * mouseX) / windowManager->getWidth() - 1.0f;
    float y = 1.0f - (2.0f * mouseY) / windowManager->getHeight(); // Flip Y coordinate
    
    // Create clip space coordinates - flip Y again for Vulkan coordinate system
    glm::vec4 rayClip = glm::vec4(x, -y, -1.0f, 1.0f);
    
    // Convert to eye space.
    //
    // DELIBERATELY NOT the reverse-Z projection (graphics/DepthConvention.h), and this is the one
    // place in the engine where that is fine: the result is used only for a ray DIRECTION — the
    // next line overwrites z and w outright — so nothing here depends on the depth mapping. The
    // only terms that survive are m[0][0] and m[1][1], which are identical in both conventions.
    // The near/far values below are therefore inert.
    // If this is ever changed to reconstruct a DEPTH or a world-space POSITION, it must switch to
    // Camera::getProjectionMatrix or it will be silently wrong.
    glm::mat4 proj = glm::perspective(
        glm::radians(45.0f),
        (float)windowManager->getWidth() / (float)windowManager->getHeight(),
        0.1f,
        200.0f
    );
    proj[1][1] *= -1; // Flip Y for Vulkan
    
    glm::vec4 rayEye = glm::inverse(proj) * rayClip;
    rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);
    
    // Convert to world space
    glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
    glm::vec3 rayWorld = glm::vec3(glm::inverse(view) * rayEye);
    
    return glm::normalize(rayWorld);
}

} // namespace Phyxel
