#pragma once

#include "core/Types.h"
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>

namespace VulkanCube {
namespace Scene {

class SceneManager {
public:
    SceneManager();
    ~SceneManager();

    // Scene initialization
    bool initialize(int gridSize = 32);
    void cleanup();

    // Cube management
    void addCube(int x, int y, int z);
    void removeCube(int x, int y, int z);
    bool hasCube(int x, int y, int z) const;
    void clearCubes();

    // Bulk operations
    void generateRandomCubes(int count, float strength = 2.0f);
    void generateTestScene();

    // Data access for rendering
    const std::vector<Cube>& getCubes() const { return cubes; }
    const std::vector<InstanceData>& getInstanceData() const { return instanceData; }
    size_t getCubeCount() const { return cubes.size(); }

    // Scene updates
    void updateInstanceData();
    void recalculateFaceMasks(); // Calculate face masks once after scene generation
    void setVisibility(int index, bool visible);
    void updateCubeColor(int index, const glm::vec3& color);

    // Camera and view management
    void setCamera(const glm::vec3& position, const glm::vec3& front, const glm::vec3& up);
    void updateView(const glm::mat4& view, const glm::mat4& projection);
    const UniformBufferObject& getUBO() const { return ubo; }

    // Physics integration helpers
    void syncWithPhysics(const std::vector<glm::mat4>& transforms);
    void getPhysicsData(std::vector<glm::vec3>& positions, std::vector<glm::vec3>& sizes) const;

    // Frustum culling support
    void performFrustumCulling();
    const std::vector<uint32_t>& getVisibilityBuffer() const { return visibilityBuffer; }

    // Mouse picking / hovering support
    int pickCube(const glm::vec3& rayOrigin, const glm::vec3& rayDirection) const;
    void setHoveredCube(int cubeIndex);
    void clearHoveredCube();

private:
    int gridSize;
    std::vector<Cube> cubes;
    std::vector<InstanceData> instanceData;
    std::vector<uint32_t> visibilityBuffer;
    std::unordered_map<glm::ivec3, size_t> positionToIndex;
    
    UniformBufferObject ubo;
    glm::mat4 viewMatrix;
    glm::mat4 projectionMatrix;
    
    // Mouse hover state
    int hoveredCubeIndex;
    glm::vec3 originalHoveredColor;

    // Helper functions
    glm::vec3 getRandomColor();
    bool isPositionValid(int x, int y, int z) const;
    void updatePositionMapping();
    uint32_t calculateFaceMask(const glm::ivec3& position);
};

} // namespace Scene
} // namespace VulkanCube
