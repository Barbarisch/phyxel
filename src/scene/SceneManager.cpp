#include "scene/SceneManager.h"
#include "utils/Math.h"
#include <iostream>
#include <random>
#include <algorithm>
#include <limits>

namespace VulkanCube {
namespace Scene {

SceneManager::SceneManager() : gridSize(32), hoveredCubeIndex(-1), instanceDataNeedsUpdate(false) {
    // Constructor
}

SceneManager::~SceneManager() {
    cleanup();
}

bool SceneManager::initialize(int gridSize) {
    this->gridSize = gridSize;
    
    // Reserve space for reasonable number of cubes
    cubes.reserve(32768); // 32K cubes max
    instanceData.reserve(32768);
    visibilityBuffer.reserve(32768);
    
    // Initialize UBO with default values
    ubo.view = glm::mat4(1.0f);
    ubo.proj = glm::mat4(1.0f);
    ubo.numInstances = 0;
    
    std::cout << "Scene manager initialized with grid size: " << gridSize << "x" << gridSize << "x" << gridSize << std::endl;
    
    return true;
}

void SceneManager::cleanup() {
    cubes.clear();
    instanceData.clear();
    visibilityBuffer.clear();
    positionToIndex.clear();
    
    // Reset UBO
    ubo.numInstances = 0;
}

void SceneManager::addCube(int x, int y, int z) {
    if (!isPositionValid(x, y, z)) {
        return;
    }
    
    glm::ivec3 position(x, y, z);
    
    // Check if cube already exists at this position
    if (positionToIndex.find(position) != positionToIndex.end()) {
        return;
    }
    
    // Create new cube
    Cube newCube(position, getRandomColor());
    
    // Add to cubes vector
    size_t index = cubes.size();
    cubes.push_back(newCube);
    
    // Update position mapping
    positionToIndex[position] = index;
    
    // Create instance data using new packed format
    CubeFaces faces;
    faces.front = faces.back = faces.left = faces.right = faces.top = faces.bottom = true; // All faces visible initially
    InstanceData instanceData = InstanceDataUtils::createInstanceData(position, faces, newCube.color);
    this->instanceData.push_back(instanceData);
    
    // Add to visibility buffer
    visibilityBuffer.push_back(1); // Initially visible
    
    // Update UBO with current cube count
    ubo.numInstances = static_cast<uint32_t>(cubes.size());
}

void SceneManager::removeCube(int x, int y, int z) {
    glm::ivec3 position(x, y, z);
    
    auto it = positionToIndex.find(position);
    if (it == positionToIndex.end()) {
        return; // Cube doesn't exist
    }
    
    size_t index = it->second;
    
    // Remove from vectors (swap with last element for O(1) removal)
    if (index < cubes.size() - 1) {
        std::swap(cubes[index], cubes.back());
        std::swap(instanceData[index], instanceData.back());
        std::swap(visibilityBuffer[index], visibilityBuffer.back());
        
        // Update position mapping for the swapped element
        positionToIndex[cubes[index].position] = index;
    }
    
    cubes.pop_back();
    instanceData.pop_back();
    visibilityBuffer.pop_back();
    
    // Remove from position mapping
    positionToIndex.erase(it);
    
    // Update UBO with current cube count
    ubo.numInstances = static_cast<uint32_t>(cubes.size());
}

bool SceneManager::hasCube(int x, int y, int z) const {
    glm::ivec3 position(x, y, z);
    return positionToIndex.find(position) != positionToIndex.end();
}

void SceneManager::clearCubes() {
    cubes.clear();
    instanceData.clear();
    visibilityBuffer.clear();
    positionToIndex.clear();
    
    // Update UBO with current cube count (0)
    ubo.numInstances = 0;
}

void SceneManager::generateRandomCubes(int count, float strength) {
    clearCubes();
    
    for (int i = 0; i < count; ++i) {
        int x = Math::randomFloat(strength) * gridSize;
        int y = Math::randomFloat(strength) * gridSize;
        int z = Math::randomFloat(strength) * gridSize;
        
        // Clamp to valid range
        x = std::max(0, std::min(x, gridSize - 1));
        y = std::max(0, std::min(y, gridSize - 1));
        z = std::max(0, std::min(z, gridSize - 1));
        
        addCube(x, y, z);
    }
    
    std::cout << "Generated " << cubes.size() << " random cubes" << std::endl;
}

void SceneManager::generateTestScene() {
    clearCubes();
    
    // Create a simple test pattern
    for (int x = 0; x < 4; ++x) {
        for (int y = 0; y < 4; ++y) {
            for (int z = 0; z < 4; ++z) {
                addCube(x + gridSize/2 - 2, y, z + gridSize/2 - 2);
            }
        }
    }
    
    std::cout << "Generated test scene with " << cubes.size() << " cubes" << std::endl;
}

bool SceneManager::updateInstanceData() {
    // OPTIMIZATION: For static cubes, only update colors when needed
    // The position and face masks are static and don't need repacking every frame
    
    bool dataChanged = instanceDataNeedsUpdate;  // Check if forced update is needed
    
    // Clear the update flag after processing
    instanceDataNeedsUpdate = false;
    
    // REMOVED: Expensive per-frame unpacking/repacking for 32K static cubes
    // The position and face data are set once when cubes are added and never change
    // Hover color changes are now handled directly in setHoveredCube()/clearHoveredCube()
    
    return dataChanged;
}

void SceneManager::recalculateFaceMasks() {
    // Calculate face masks for all cubes - do this once after scene generation, not every frame!
    for (size_t i = 0; i < cubes.size(); ++i) {
        uint32_t newFaceMask = calculateFaceMask(cubes[i].position);
        
        // Extract current position and future data, update face mask
        uint32_t x, y, z;
        InstanceDataUtils::unpackRelativePos(instanceData[i].packedData, x, y, z);
        uint32_t currentFutureData = InstanceDataUtils::getFutureData(instanceData[i].packedData);
        
        // Repack with new face mask
        instanceData[i].packedData = InstanceDataUtils::packInstanceData(x, y, z, newFaceMask, currentFutureData);
    }
    std::cout << "Recalculated face masks for " << cubes.size() << " cubes" << std::endl;
}

uint32_t SceneManager::calculateFaceMask(const glm::ivec3& position) {
    CubeFaces faces; // All faces start as visible (true)
    
    int x = position.x;
    int y = position.y; 
    int z = position.z;
    
    // Check each direction for adjacent cubes to cull faces
    // NOTE: Face directions must match vertex shader bit mapping:
    // bit 0=front(+Z), 1=back(-Z), 2=right(+X), 3=left(-X), 4=top(+Y), 5=bottom(-Y)
    
    // Right face (+X): check if there's a cube at (x+1, y, z)
    if (x + 1 < gridSize && hasCube(x + 1, y, z)) {
        faces.right = false;  // Hidden by adjacent cube
    }
    
    // Left face (-X): check if there's a cube at (x-1, y, z)  
    if (x - 1 >= 0 && hasCube(x - 1, y, z)) {
        faces.left = false;  // Hidden by adjacent cube
    }
    
    // Top face (+Y): check if there's a cube at (x, y+1, z)
    if (y + 1 < gridSize && hasCube(x, y + 1, z)) {
        faces.top = false;  // Hidden by adjacent cube
    }
    
    // Bottom face (-Y): check if there's a cube at (x, y-1, z)
    if (y - 1 >= 0 && hasCube(x, y - 1, z)) {
        faces.bottom = false;  // Hidden by adjacent cube
    }
    
    // Front face (+Z): check if there's a cube at (x, y, z+1)
    if (z + 1 < gridSize && hasCube(x, y, z + 1)) {
        faces.front = false;  // Hidden by adjacent cube
    }
    
    // Back face (-Z): check if there's a cube at (x, y, z-1)
    if (z - 1 >= 0 && hasCube(x, y, z - 1)) {
        faces.back = false;  // Hidden by adjacent cube
    }
    
    // Convert to bitmask matching vertex shader:
    // bit 0=front(+Z), 1=back(-Z), 2=right(+X), 3=left(-X), 4=top(+Y), 5=bottom(-Y)
    uint32_t mask = 0;
    if (faces.front)  mask |= (1 << 0);  // bit 0: front (+Z)
    if (faces.back)   mask |= (1 << 1);  // bit 1: back (-Z)
    if (faces.right)  mask |= (1 << 2);  // bit 2: right (+X)
    if (faces.left)   mask |= (1 << 3);  // bit 3: left (-X)
    if (faces.top)    mask |= (1 << 4);  // bit 4: top (+Y)
    if (faces.bottom) mask |= (1 << 5);  // bit 5: bottom (-Y)
    
    // Debug: For corner cubes, print the face mask
    if ((x == 0 || x == 31) && (y == 0 || y == 31) && (z == 0 || z == 31)) {
        static int debugCount = 0;
        if (debugCount < 8) { // Only print first 8 corner cubes
            std::cout << "Corner cube at (" << x << "," << y << "," << z << ") faceMask: 0x" 
                      << std::hex << mask << std::dec << std::endl;
            debugCount++;
        }
    }
    
    return mask;
}

void SceneManager::setVisibility(int index, bool visible) {
    if (index >= 0 && index < static_cast<int>(cubes.size())) {
        cubes[index].visible = visible;
        visibilityBuffer[index] = visible ? 1 : 0;
    }
}

void SceneManager::updateCubeColor(int index, const glm::vec3& color) {
    if (index >= 0 && index < static_cast<int>(cubes.size())) {
        cubes[index].color = color;
        instanceData[index].color = color;
    }
}

void SceneManager::setCamera(const glm::vec3& position, const glm::vec3& front, const glm::vec3& up) {
    viewMatrix = glm::lookAt(position, position + front, up);
    ubo.view = viewMatrix;
    // No viewPos in UniformBufferObject for this version
}

void SceneManager::updateView(const glm::mat4& view, const glm::mat4& projection) {
    viewMatrix = view;
    projectionMatrix = projection;
    ubo.view = view;
    ubo.proj = projection;
}

void SceneManager::syncWithPhysics(const std::vector<glm::mat4>& transforms) {
    size_t count = std::min(transforms.size(), instanceData.size());
    for (size_t i = 0; i < count; ++i) {
        // Extract position from transform matrix
        glm::vec3 position = glm::vec3(transforms[i][3]);
        
        // Extract current face mask and future data
        uint32_t currentFaceMask = InstanceDataUtils::getFaceMask(instanceData[i].packedData);
        uint32_t currentFutureData = InstanceDataUtils::getFutureData(instanceData[i].packedData);
        
        // Update packed data with new position (treating world pos as relative for now)
        glm::ivec3 relativePos = glm::ivec3(position);
        instanceData[i].packedData = InstanceDataUtils::packInstanceData(
            relativePos.x & 0x1F, relativePos.y & 0x1F, relativePos.z & 0x1F,
            currentFaceMask, currentFutureData
        );
    }
}

void SceneManager::getPhysicsData(std::vector<glm::vec3>& positions, std::vector<glm::vec3>& sizes) const {
    positions.clear();
    sizes.clear();
    
    positions.reserve(cubes.size());
    sizes.reserve(cubes.size());
    
    for (const auto& cube : cubes) {
        positions.push_back(glm::vec3(cube.position));
        sizes.push_back(glm::vec3(1.0f)); // Standard cube size
    }
}

void SceneManager::performFrustumCulling() {
    // Simple frustum culling implementation
    // For now, just mark all as visible
    // In a full implementation, this would test each cube against the view frustum
    for (size_t i = 0; i < cubes.size(); ++i) {
        visibilityBuffer[i] = cubes[i].visible ? 1 : 0;
    }
}

std::vector<glm::vec3> SceneManager::getCubePositions() const {
    std::vector<glm::vec3> positions;
    positions.reserve(cubes.size());
    
    for (const auto& cube : cubes) {
        positions.emplace_back(cube.position);
    }
    
    return positions;
}

glm::vec3 SceneManager::getRandomColor() {
    // Use the same color palette as the original code
    static std::vector<glm::vec3> palette = {
        {0.0f, 1.0f, 0.0f}, // green
        {0.0f, 0.0f, 1.0f}, // blue
        {1.0f, 1.0f, 0.0f}, // yellow
        {1.0f, 0.0f, 1.0f}, // magenta
        {0.0f, 1.0f, 1.0f}  // cyan
    };
    
    return palette[rand() % palette.size()];
}

bool SceneManager::isPositionValid(int x, int y, int z) const {
    return x >= 0 && x < gridSize &&
           y >= 0 && y < gridSize &&
           z >= 0 && z < gridSize;
}

void SceneManager::updatePositionMapping() {
    positionToIndex.clear();
    for (size_t i = 0; i < cubes.size(); ++i) {
        positionToIndex[cubes[i].position] = i;
    }
}

// Ray-AABB intersection test
bool rayAABBIntersect(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, 
                     const glm::vec3& aabbMin, const glm::vec3& aabbMax, float& distance) {
    glm::vec3 invDir = 1.0f / rayDirection;
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

int SceneManager::pickCube(const glm::vec3& rayOrigin, const glm::vec3& rayDirection) const {
    int closestCube = -1;
    float closestDistance = std::numeric_limits<float>::max();
    
    // Test ray against all cubes
    for (size_t i = 0; i < cubes.size(); ++i) {
        const Cube& cube = cubes[i];
        
        // Calculate AABB for the cube (each cube is 1x1x1 unit)
        glm::vec3 cubePos = glm::vec3(cube.position);
        glm::vec3 aabbMin = cubePos - 0.5f; // Cube extends 0.5 units in each direction
        glm::vec3 aabbMax = cubePos + 0.5f;
        
        float distance;
        if (rayAABBIntersect(rayOrigin, rayDirection, aabbMin, aabbMax, distance)) {
            if (distance < closestDistance) {
                closestDistance = distance;
                closestCube = static_cast<int>(i);
            }
        }
    }
    
    return closestCube;
}

void SceneManager::setHoveredCube(int cubeIndex) {
    // Clear previous hover if any
    clearHoveredCube();
    
    if (cubeIndex >= 0 && cubeIndex < static_cast<int>(cubes.size())) {
        hoveredCubeIndex = cubeIndex;
        
        // Store original color
        originalHoveredColor = instanceData[cubeIndex].color;
        
        // Lighten the color like in the original (add 0.3f and clamp to 1.0f)
        instanceData[cubeIndex].color = glm::min(originalHoveredColor + glm::vec3(0.3f), glm::vec3(1.0f));
        
        // Force instance data update on next frame
        instanceDataNeedsUpdate = true;
    }
}

void SceneManager::clearHoveredCube() {
    if (hoveredCubeIndex >= 0 && hoveredCubeIndex < static_cast<int>(cubes.size())) {
        // Restore original color
        instanceData[hoveredCubeIndex].color = originalHoveredColor;
        hoveredCubeIndex = -1;
        
        // Force instance data update on next frame
        instanceDataNeedsUpdate = true;
    }
}

} // namespace Scene
} // namespace VulkanCube
