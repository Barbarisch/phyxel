#pragma once

#include <glm/glm.hpp>
#include <random>

namespace Phyxel {

// Forward declarations
struct CubeFaces;

namespace Math {

// Random number generation
float randomFloat(float strength = 2.0f);

// Position/index conversion utilities
int positionToIndex(int x, int y, int z, int size = 32);
glm::ivec3 indexToPosition(int index, int size = 32);

// Face mask utilities
uint32_t facesToBitmask(const CubeFaces& faces);

// Ray-box intersection
bool rayBoxIntersection(const glm::vec3& rayOrigin, const glm::vec3& rayDir, 
                       const glm::vec3& boxMin, const glm::vec3& boxMax, 
                       float& tNear, float& tFar);

// Screen to world ray conversion
glm::vec3 screenToWorldRay(double mouseX, double mouseY, int screenWidth, int screenHeight,
                          const glm::mat4& view, const glm::mat4& proj);

} // namespace Math
} // namespace Phyxel
