#include "utils/Math.h"
#include "core/Types.h"
#include <random>
#include <cmath>
#include <algorithm>

namespace Phyxel {
namespace Math {

float randomFloat(float strength) {
    static std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> dist(-strength, strength);
    return dist(rng);
}

int positionToIndex(int x, int y, int z, int size) {
    return x + y * size + z * size * size;
}

glm::ivec3 indexToPosition(int index, int size) {
    int z = index / (size * size);
    int remainder = index % (size * size);
    int y = remainder / size;
    int x = remainder % size;
    return glm::ivec3(x, y, z);
}

uint32_t facesToBitmask(const CubeFaces& faces) {
    uint32_t mask = 0;
    if (faces.front)  mask |= (1 << 0);
    if (faces.back)   mask |= (1 << 1);
    if (faces.right)  mask |= (1 << 2);
    if (faces.left)   mask |= (1 << 3);
    if (faces.top)    mask |= (1 << 4);
    if (faces.bottom) mask |= (1 << 5);
    return mask;
}

bool rayBoxIntersection(const glm::vec3& rayOrigin, const glm::vec3& rayDir, 
                       const glm::vec3& boxMin, const glm::vec3& boxMax, 
                       float& tNear, float& tFar) {
    tNear = 0.0f;
    tFar = 1000.0f;
    
    for (int i = 0; i < 3; ++i) {
        if (std::abs(rayDir[i]) < 1e-6f) {
            // Ray is parallel to this axis
            if (rayOrigin[i] < boxMin[i] || rayOrigin[i] > boxMax[i]) {
                return false;
            }
        } else {
            float t1 = (boxMin[i] - rayOrigin[i]) / rayDir[i];
            float t2 = (boxMax[i] - rayOrigin[i]) / rayDir[i];
            
            if (t1 > t2) std::swap(t1, t2);
            
            tNear = std::max(tNear, t1);
            tFar = std::min(tFar, t2);
            
            if (tNear > tFar) return false;
        }
    }
    
    return tNear < tFar;
}

glm::vec3 screenToWorldRay(double mouseX, double mouseY, int screenWidth, int screenHeight,
                          const glm::mat4& view, const glm::mat4& proj) {
    // Convert screen space to NDC
    float x = (2.0f * mouseX) / screenWidth - 1.0f;
    float y = 1.0f - (2.0f * mouseY) / screenHeight;
    glm::vec4 rayClip = glm::vec4(x, -y, -1.0f, 1.0f);

    // Convert to eye space
    glm::vec4 rayEye = glm::inverse(proj) * rayClip;
    rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);

    // Convert to world space
    glm::vec3 rayWorld = glm::normalize(glm::vec3(glm::inverse(view) * rayEye));
    return rayWorld;
}

} // namespace Math
} // namespace Phyxel
