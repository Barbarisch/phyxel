#pragma once

#include "core/KinematicVoxelManager.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <unordered_map>
#include <string>
#include <vector>

namespace Phyxel {
namespace Graphics {

/// Renders all KinematicVoxelObjects with full lighting and shadow sampling.
///
/// Rendering strategy:
///   - One vkCmdDraw per object: 6 vertices × faceCount instances (procedural face generation)
///   - Per-object mat4 model matrix uploaded as a push constant (64 bytes)
///   - Shared VkBuffer stores all face instances; each object occupies a contiguous range
///   - Reuses the existing set-0 descriptor (UBO + texture atlas + shadow map + lights)
///   - Fragment shader: voxel.frag (same as static and dynamic voxel pipelines)
///   - Vertex shader: kinematic_voxel.vert (uses gl_VertexIndex, no separate vertex buffer)
///
/// Buffer rebuild policy:
///   - Call rebuildBuffer() when objects are added or removed.
///   - Transform-only changes (setTransform) do NOT require a rebuild — only the push constant updates.
class KinematicVoxelPipeline {
public:
    KinematicVoxelPipeline();
    ~KinematicVoxelPipeline();

    /// Initialize the pipeline.
    /// @param uboDescriptorSetLayout  Existing set-0 layout (UBO + atlas + shadow + lights)
    /// @param uboDescriptorSet        Active set-0 descriptor set
    bool initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                    VkRenderPass renderPass, VkExtent2D extent,
                    VkDescriptorSetLayout uboDescriptorSetLayout,
                    VkDescriptorSet uboDescriptorSet);

    void cleanup();

    /// Rebuild the shared face instance buffer from current object composition.
    /// Must be called when objects are added or removed from the manager.
    void rebuildBuffer(const std::unordered_map<std::string, Core::KinematicVoxelObject>& objects);

    /// Record render commands for all visible objects into the given command buffer.
    /// @param uboSet  Per-frame UBO descriptor set (set 0: view/proj/atlas/shadow/lights).
    /// Call between vkCmdBeginRenderPass and vkCmdEndRenderPass.
    void render(VkCommandBuffer cmd,
                const std::unordered_map<std::string, Core::KinematicVoxelObject>& objects,
                VkDescriptorSet uboSet);

    /// Recreate pipeline after swapchain resize.
    void recreatePipeline(VkRenderPass renderPass, VkExtent2D extent);

    /// Maximum total face instances across all kinematic objects combined.
    static constexpr size_t MAX_TOTAL_FACES = 8192;

private:
    void     createPipeline(VkRenderPass renderPass, VkExtent2D extent,
                             VkDescriptorSetLayout uboLayout);
    void     createInstanceBuffer();
    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const;

    VkDevice         m_device         = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline       m_pipeline       = VK_NULL_HANDLE;

    VkBuffer       m_instanceBuffer       = VK_NULL_HANDLE;
    VkDeviceMemory m_instanceBufferMemory = VK_NULL_HANDLE;


    struct ObjectRange {
        uint32_t startFace = 0;
        uint32_t faceCount = 0;
    };
    std::unordered_map<std::string, ObjectRange> m_objectRanges;

    uint32_t m_totalFaces = 0;
};

} // namespace Graphics
} // namespace Phyxel
