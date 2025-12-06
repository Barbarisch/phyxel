#pragma once

#include "core/Types.h"
#include <glm/glm.hpp>
#include <vector>
#include <memory>

namespace VulkanCube {

// Forward declarations
namespace Vulkan {
    class VulkanDevice;
}

/**
 * RaycastVisualizer - Debug visualization for raycasting operations
 * 
 * Renders debug geometry to visualize:
 * - Ray path from camera through crosshair
 * - Voxels traversed during DDA raycasting
 * - Hit point and surface normal
 * - Hit face highlighting
 * 
 * Uses simple line rendering with VK_PRIMITIVE_TOPOLOGY_LINE_LIST
 */
class RaycastVisualizer {
public:
    struct DebugVertex {
        glm::vec3 position;
        glm::vec3 color;
    };

    struct RaycastDebugData {
        glm::vec3 rayOrigin;
        glm::vec3 rayDirection;
        std::vector<glm::ivec3> traversedVoxels;  // DDA traversal path
        glm::vec3 hitPoint;
        glm::vec3 hitNormal;
        bool hasHit;
        int hitFace;  // -1 if no hit, 0-5 for faces
    };

    RaycastVisualizer();
    ~RaycastVisualizer();

    // Initialize Vulkan resources
    void initialize(Vulkan::VulkanDevice* device);
    void cleanup();

    // Update debug data
    void setRaycastData(const RaycastDebugData& data);
    void clearData();

    // Rendering
    void updateBuffers(uint32_t currentFrame);
    void render(VkCommandBuffer commandBuffer, uint32_t currentFrame);

    // Toggle visualization elements
    void setShowRayPath(bool show) { m_showRayPath = show; }
    void setShowTraversalBoxes(bool show) { m_showTraversalBoxes = show; }
    void setShowHitPoint(bool show) { m_showHitPoint = show; }
    void setShowHitNormal(bool show) { m_showHitNormal = show; }

    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled) { m_enabled = enabled; }

private:
    void generateDebugGeometry();
    void createBuffers();
    void updateVertexBuffer(uint32_t currentFrame);

    // Vulkan resources
    Vulkan::VulkanDevice* m_device = nullptr;
    VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexBufferMemory = VK_NULL_HANDLE;
    std::vector<DebugVertex> m_vertices;

    // Debug data
    RaycastDebugData m_data;
    bool m_dataValid = false;
    bool m_dataChanged = false;

    // Visualization options
    bool m_enabled = false;
    bool m_showRayPath = true;
    bool m_showTraversalBoxes = false;  // Disabled by default
    bool m_showHitPoint = true;
    bool m_showHitNormal = true;

    // Constants
    static constexpr float RAY_LENGTH = 100.0f;
    static constexpr float NORMAL_ARROW_LENGTH = 2.0f;
    static constexpr float HIT_POINT_SIZE = 0.5f;
};

} // namespace VulkanCube
