#pragma once

#include "core/KinematicVoxelManager.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <unordered_map>
#include <string>
#include <vector>
#include <functional>

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

    /// Maximum total face instances across all kinematic objects combined.
    /// 131072 × 40 B ≈ 5 MB — a felled large tree is ~5-10k faces after mixed-
    /// scale culling; 8192 silently dropped whole felled trees from rendering.
    static constexpr size_t MAX_TOTAL_FACES = 131072;

    struct ObjectRange {
        uint32_t startFace = 0;
        uint32_t faceCount = 0;
    };

    /// Rebuild the shared face instance buffer from current object composition.
    /// Must be called when objects are added or removed from the manager.
    void rebuildBuffer(const std::unordered_map<std::string, Core::KinematicVoxelObject>& objects);

    /// Record render commands for all visible objects into the given command buffer.
    /// @param uboSet  Per-frame UBO descriptor set (set 0: view/proj/atlas/shadow/lights).
    /// Call between vkCmdBeginRenderPass and vkCmdEndRenderPass.
    void render(VkCommandBuffer cmd,
                const std::unordered_map<std::string, Core::KinematicVoxelObject>& objects,
                VkDescriptorSet uboSet);

    /// Same as render(), but uses the FRONT/BACK-flipped reflection pipeline and is meant to
    /// be called inside the mirror reflection pass with the reflected-camera descriptor set.
    /// The reflected view (mainView * reflMat) has det=-1 and flips winding, so kinematic
    /// objects need the opposite cull from the main pass.
    void renderReflection(VkCommandBuffer cmd,
                const std::unordered_map<std::string, Core::KinematicVoxelObject>& objects,
                VkDescriptorSet reflectionUboSet);

    /// Accessors for shadow pass draw (shadow pipeline reuses the same instance buffer)
    VkBuffer getInstanceBuffer() const { return m_instanceBuffer; }
    const std::unordered_map<std::string, ObjectRange>& getObjectRanges() const { return m_objectRanges; }

    /// Recreate pipeline after swapchain resize.
    void recreatePipeline(VkRenderPass renderPass, VkExtent2D extent);

    /// Phase 4: sample the baked light field at a world position so furniture reacts to
    /// skylight + block light like the world (darkens indoors, picks up glow/spell light).
    /// Returns vec4(skylight, blockR, blockG, blockB) each 0..1. If unset, furniture is
    /// rendered full-bright (vec4(1)). Wired from RenderCoordinator (which owns ChunkManager).
    using LightSampler = std::function<glm::vec4(const glm::vec3& worldPos)>;
    void setLightSampler(LightSampler fn) { m_lightSampler = std::move(fn); }
private:
    void     createPipeline(VkRenderPass renderPass, VkExtent2D extent,
                             VkDescriptorSetLayout uboLayout);
    void     recordDraws(VkCommandBuffer cmd,
                         const std::unordered_map<std::string, Core::KinematicVoxelObject>& objects,
                         VkDescriptorSet uboSet, VkPipeline pipeline);
    void     createInstanceBuffer();
    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const;

    VkDevice         m_device         = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline       m_pipeline       = VK_NULL_HANDLE;
    VkPipeline       m_reflectionPipeline = VK_NULL_HANDLE;  // BACK_BIT variant for the mirror reflection pass

    VkBuffer       m_instanceBuffer       = VK_NULL_HANDLE;
    VkDeviceMemory m_instanceBufferMemory = VK_NULL_HANDLE;

    std::unordered_map<std::string, ObjectRange> m_objectRanges;

    uint32_t m_totalFaces = 0;

    LightSampler m_lightSampler; // Phase 4 baked-light sampler (null = full bright)
};

} // namespace Graphics
} // namespace Phyxel
