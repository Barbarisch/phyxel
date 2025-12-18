#include "graphics/RaycastVisualizer.h"
#include "vulkan/VulkanDevice.h"
#include "utils/Logger.h"
#include <algorithm>

namespace VulkanCube {

RaycastVisualizer::RaycastVisualizer() {
}

RaycastVisualizer::~RaycastVisualizer() {
    cleanup();
}

void RaycastVisualizer::initialize(Vulkan::VulkanDevice* device) {
    m_device = device;
    LOG_INFO("RaycastVisualizer", "Raycast visualizer initialized");
}

void RaycastVisualizer::cleanup() {
    if (m_device && m_device->getDevice()) {
        if (m_vertexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_device->getDevice(), m_vertexBuffer, nullptr);
            m_vertexBuffer = VK_NULL_HANDLE;
        }
        if (m_vertexBufferMemory != VK_NULL_HANDLE) {
            vkFreeMemory(m_device->getDevice(), m_vertexBufferMemory, nullptr);
            m_vertexBufferMemory = VK_NULL_HANDLE;
        }
    }
}

void RaycastVisualizer::cycleTargetMode() {
    switch (m_targetMode) {
        case TargetMode::Cube:
            m_targetMode = TargetMode::Subcube;
            LOG_INFO("RaycastVisualizer", "Switched to Subcube visualization");
            break;
        case TargetMode::Subcube:
            m_targetMode = TargetMode::Microcube;
            LOG_INFO("RaycastVisualizer", "Switched to Microcube visualization");
            break;
        case TargetMode::Microcube:
            m_targetMode = TargetMode::Cube;
            LOG_INFO("RaycastVisualizer", "Switched to Cube visualization");
            break;
    }
    m_dataChanged = true;
    generateDebugGeometry();
}

void RaycastVisualizer::setRaycastData(const RaycastDebugData& data) {
    m_data = data;
    m_dataValid = true;
    m_dataChanged = true;  // Mark that data has changed
    generateDebugGeometry();
}

void RaycastVisualizer::clearData() {
    m_dataValid = false;
    m_dataChanged = false;
    m_vertices.clear();
    // Note: We don't clear preview boxes here, they are managed separately
}

void RaycastVisualizer::addPreviewBox(const glm::vec3& pos, const glm::vec3& size, const glm::vec3& color) {
    m_previewBoxes.push_back({pos, size, color});
    m_dataChanged = true;
    generateDebugGeometry();
}

void RaycastVisualizer::clearPreviewBoxes() {
    m_previewBoxes.clear();
    m_dataChanged = true;
    generateDebugGeometry();
}

void RaycastVisualizer::addLine(const glm::vec3& start, const glm::vec3& end, const glm::vec3& color) {
    m_lines.push_back({start, end, color});
    m_dataChanged = true;
    generateDebugGeometry();
}

void RaycastVisualizer::clearLines() {
    m_lines.clear();
    m_dataChanged = true;
    generateDebugGeometry();
}

void RaycastVisualizer::beginFrame() {
    if (!m_lines.empty()) {
        m_lines.clear();
        m_dataChanged = true;
        generateDebugGeometry();
    }
}

void RaycastVisualizer::generateDebugGeometry() {
    m_vertices.clear();

    // Render custom lines
    for (const auto& line : m_lines) {
        m_vertices.push_back({line.start, line.color});
        m_vertices.push_back({line.end, line.color});
    }

    // Render preview boxes first (always visible if present)
    for (const auto& box : m_previewBoxes) {
        glm::vec3 min = box.position;
        glm::vec3 max = min + box.size;
        glm::vec3 color = box.color;

        // Bottom face
        m_vertices.push_back({glm::vec3(min.x, min.y, min.z), color});
        m_vertices.push_back({glm::vec3(max.x, min.y, min.z), color});
        
        m_vertices.push_back({glm::vec3(max.x, min.y, min.z), color});
        m_vertices.push_back({glm::vec3(max.x, min.y, max.z), color});
        
        m_vertices.push_back({glm::vec3(max.x, min.y, max.z), color});
        m_vertices.push_back({glm::vec3(min.x, min.y, max.z), color});
        
        m_vertices.push_back({glm::vec3(min.x, min.y, max.z), color});
        m_vertices.push_back({glm::vec3(min.x, min.y, min.z), color});

        // Top face
        m_vertices.push_back({glm::vec3(min.x, max.y, min.z), color});
        m_vertices.push_back({glm::vec3(max.x, max.y, min.z), color});
        
        m_vertices.push_back({glm::vec3(max.x, max.y, min.z), color});
        m_vertices.push_back({glm::vec3(max.x, max.y, max.z), color});
        
        m_vertices.push_back({glm::vec3(max.x, max.y, max.z), color});
        m_vertices.push_back({glm::vec3(min.x, max.y, max.z), color});
        
        m_vertices.push_back({glm::vec3(min.x, max.y, max.z), color});
        m_vertices.push_back({glm::vec3(min.x, max.y, min.z), color});

        // Vertical edges
        m_vertices.push_back({glm::vec3(min.x, min.y, min.z), color});
        m_vertices.push_back({glm::vec3(min.x, max.y, min.z), color});
        
        m_vertices.push_back({glm::vec3(max.x, min.y, min.z), color});
        m_vertices.push_back({glm::vec3(max.x, max.y, min.z), color});
        
        m_vertices.push_back({glm::vec3(max.x, min.y, max.z), color});
        m_vertices.push_back({glm::vec3(max.x, max.y, max.z), color});
        
        m_vertices.push_back({glm::vec3(min.x, min.y, max.z), color});
        m_vertices.push_back({glm::vec3(min.x, max.y, max.z), color});
    }

    if (!m_dataValid) {
        return;
    }

    // 1. Ray path - bright white line from origin in ray direction
    if (m_showRayPath) {
        glm::vec3 rayEnd = m_data.rayOrigin + m_data.rayDirection * RAY_LENGTH;
        m_vertices.push_back({m_data.rayOrigin, glm::vec3(1.0f, 1.0f, 1.0f)});  // White
        m_vertices.push_back({rayEnd, glm::vec3(1.0f, 1.0f, 1.0f)});
    }

    // 2. Traversed voxel boxes - cyan wireframe boxes
    if (m_showTraversalBoxes && !m_data.traversedVoxels.empty()) {
        glm::vec3 boxColor(0.0f, 1.0f, 1.0f);  // Cyan
        
        for (const auto& voxelPos : m_data.traversedVoxels) {
            // Create wireframe cube for each traversed voxel
            glm::vec3 min(voxelPos.x, voxelPos.y, voxelPos.z);
            glm::vec3 max = min + glm::vec3(1.0f);

            // Bottom face
            m_vertices.push_back({glm::vec3(min.x, min.y, min.z), boxColor});
            m_vertices.push_back({glm::vec3(max.x, min.y, min.z), boxColor});
            
            m_vertices.push_back({glm::vec3(max.x, min.y, min.z), boxColor});
            m_vertices.push_back({glm::vec3(max.x, min.y, max.z), boxColor});
            
            m_vertices.push_back({glm::vec3(max.x, min.y, max.z), boxColor});
            m_vertices.push_back({glm::vec3(min.x, min.y, max.z), boxColor});
            
            m_vertices.push_back({glm::vec3(min.x, min.y, max.z), boxColor});
            m_vertices.push_back({glm::vec3(min.x, min.y, min.z), boxColor});

            // Top face
            m_vertices.push_back({glm::vec3(min.x, max.y, min.z), boxColor});
            m_vertices.push_back({glm::vec3(max.x, max.y, min.z), boxColor});
            
            m_vertices.push_back({glm::vec3(max.x, max.y, min.z), boxColor});
            m_vertices.push_back({glm::vec3(max.x, max.y, max.z), boxColor});
            
            m_vertices.push_back({glm::vec3(max.x, max.y, max.z), boxColor});
            m_vertices.push_back({glm::vec3(min.x, max.y, max.z), boxColor});
            
            m_vertices.push_back({glm::vec3(min.x, max.y, max.z), boxColor});
            m_vertices.push_back({glm::vec3(min.x, max.y, min.z), boxColor});

            // Vertical edges
            m_vertices.push_back({glm::vec3(min.x, min.y, min.z), boxColor});
            m_vertices.push_back({glm::vec3(min.x, max.y, min.z), boxColor});
            
            m_vertices.push_back({glm::vec3(max.x, min.y, min.z), boxColor});
            m_vertices.push_back({glm::vec3(max.x, max.y, min.z), boxColor});
            
            m_vertices.push_back({glm::vec3(max.x, min.y, max.z), boxColor});
            m_vertices.push_back({glm::vec3(max.x, max.y, max.z), boxColor});
            
            m_vertices.push_back({glm::vec3(min.x, min.y, max.z), boxColor});
            m_vertices.push_back({glm::vec3(min.x, max.y, max.z), boxColor});
        }
    }

    // 3. Hit point - red cross marker
    if (m_showHitPoint && m_data.hasHit) {
        glm::vec3 hitColor(1.0f, 0.0f, 0.0f);  // Red
        glm::vec3 p = m_data.hitPoint;
        float s = HIT_POINT_SIZE;

        // X axis
        m_vertices.push_back({p + glm::vec3(-s, 0, 0), hitColor});
        m_vertices.push_back({p + glm::vec3(s, 0, 0), hitColor});
        
        // Y axis
        m_vertices.push_back({p + glm::vec3(0, -s, 0), hitColor});
        m_vertices.push_back({p + glm::vec3(0, s, 0), hitColor});
        
        // Z axis
        m_vertices.push_back({p + glm::vec3(0, 0, -s), hitColor});
        m_vertices.push_back({p + glm::vec3(0, 0, s), hitColor});
    }

    // 4. Hit normal - green arrow
    if (m_showHitNormal && m_data.hasHit) {
        glm::vec3 normalColor(0.0f, 1.0f, 0.0f);  // Green
        glm::vec3 arrowEnd = m_data.hitPoint + m_data.hitNormal * NORMAL_ARROW_LENGTH;
        
        m_vertices.push_back({m_data.hitPoint, normalColor});
        m_vertices.push_back({arrowEnd, normalColor});
        
        // Arrow head (simple X shape at the end)
        glm::vec3 perpendicular1, perpendicular2;
        if (glm::abs(m_data.hitNormal.y) < 0.9f) {
            perpendicular1 = glm::normalize(glm::cross(m_data.hitNormal, glm::vec3(0, 1, 0)));
        } else {
            perpendicular1 = glm::normalize(glm::cross(m_data.hitNormal, glm::vec3(1, 0, 0)));
        }
        perpendicular2 = glm::normalize(glm::cross(m_data.hitNormal, perpendicular1));
        
        float arrowSize = 0.3f;
        glm::vec3 arrowBase = arrowEnd - m_data.hitNormal * arrowSize;
        
        m_vertices.push_back({arrowEnd, normalColor});
        m_vertices.push_back({arrowBase + perpendicular1 * arrowSize, normalColor});
        
        m_vertices.push_back({arrowEnd, normalColor});
        m_vertices.push_back({arrowBase - perpendicular1 * arrowSize, normalColor});
        
        m_vertices.push_back({arrowEnd, normalColor});
        m_vertices.push_back({arrowBase + perpendicular2 * arrowSize, normalColor});
        
        m_vertices.push_back({arrowEnd, normalColor});
        m_vertices.push_back({arrowBase - perpendicular2 * arrowSize, normalColor});
    }

    // 5. Target placement - magenta wireframe box
    if (m_data.hasTarget) {
        glm::vec3 targetColor(1.0f, 0.0f, 1.0f); // Magenta
        float size = 1.0f;
        
        switch (m_targetMode) {
            case TargetMode::Cube:
                size = 1.0f;
                break;
            case TargetMode::Subcube:
                size = 1.0f / 3.0f;
                break;
            case TargetMode::Microcube:
                size = 1.0f / 9.0f;
                break;
        }

        float halfSize = size * 0.5f;
        
        glm::vec3 center = m_data.targetSubcubeCenter;
        glm::vec3 min = center - glm::vec3(halfSize);
        glm::vec3 max = center + glm::vec3(halfSize);
        
        // Bottom face
        m_vertices.push_back({glm::vec3(min.x, min.y, min.z), targetColor});
        m_vertices.push_back({glm::vec3(max.x, min.y, min.z), targetColor});
        m_vertices.push_back({glm::vec3(max.x, min.y, min.z), targetColor});
        m_vertices.push_back({glm::vec3(max.x, min.y, max.z), targetColor});
        m_vertices.push_back({glm::vec3(max.x, min.y, max.z), targetColor});
        m_vertices.push_back({glm::vec3(min.x, min.y, max.z), targetColor});
        m_vertices.push_back({glm::vec3(min.x, min.y, max.z), targetColor});
        m_vertices.push_back({glm::vec3(min.x, min.y, min.z), targetColor});

        // Top face
        m_vertices.push_back({glm::vec3(min.x, max.y, min.z), targetColor});
        m_vertices.push_back({glm::vec3(max.x, max.y, min.z), targetColor});
        m_vertices.push_back({glm::vec3(max.x, max.y, min.z), targetColor});
        m_vertices.push_back({glm::vec3(max.x, max.y, max.z), targetColor});
        m_vertices.push_back({glm::vec3(max.x, max.y, max.z), targetColor});
        m_vertices.push_back({glm::vec3(min.x, max.y, max.z), targetColor});
        m_vertices.push_back({glm::vec3(min.x, max.y, max.z), targetColor});
        m_vertices.push_back({glm::vec3(min.x, max.y, min.z), targetColor});

        // Vertical edges
        m_vertices.push_back({glm::vec3(min.x, min.y, min.z), targetColor});
        m_vertices.push_back({glm::vec3(min.x, max.y, min.z), targetColor});
        m_vertices.push_back({glm::vec3(max.x, min.y, min.z), targetColor});
        m_vertices.push_back({glm::vec3(max.x, max.y, min.z), targetColor});
        m_vertices.push_back({glm::vec3(max.x, min.y, max.z), targetColor});
        m_vertices.push_back({glm::vec3(max.x, max.y, max.z), targetColor});
        m_vertices.push_back({glm::vec3(min.x, min.y, max.z), targetColor});
        m_vertices.push_back({glm::vec3(min.x, max.y, max.z), targetColor});
    }
}

void RaycastVisualizer::updateBuffers(uint32_t currentFrame) {
    if (!m_enabled || m_vertices.empty() || !m_dataChanged) {
        return;  // Only update if data has changed
    }

    // Wait for device to be idle before recreating buffers
    vkDeviceWaitIdle(m_device->getDevice());

    VkDeviceSize bufferSize = sizeof(DebugVertex) * m_vertices.size();
    
    // Destroy old buffer
    if (m_vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device->getDevice(), m_vertexBuffer, nullptr);
        vkFreeMemory(m_device->getDevice(), m_vertexBufferMemory, nullptr);
    }

    // Create new buffer
    m_device->createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_vertexBuffer,
        m_vertexBufferMemory
    );

    // Copy vertex data
    void* data;
    vkMapMemory(m_device->getDevice(), m_vertexBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, m_vertices.data(), (size_t)bufferSize);
    vkUnmapMemory(m_device->getDevice(), m_vertexBufferMemory);
    
    m_dataChanged = false;  // Reset changed flag
}

void RaycastVisualizer::render(VkCommandBuffer commandBuffer, uint32_t currentFrame) {
    if (!m_enabled || m_vertices.empty() || m_vertexBuffer == VK_NULL_HANDLE) {
        return;
    }

    VkBuffer vertexBuffers[] = {m_vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdDraw(commandBuffer, static_cast<uint32_t>(m_vertices.size()), 1, 0, 0);
}

} // namespace VulkanCube
