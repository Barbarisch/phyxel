#pragma once

#include "vulkan/VulkanDevice.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>

namespace Phyxel {
namespace Graphics {

class ShadowMap {
public:
    ShadowMap(Vulkan::VulkanDevice* device, uint32_t width = 2048, uint32_t height = 2048);
    ~ShadowMap();

    bool initialize();
    void cleanup();

    // Render pass management
    void beginRenderPass(VkCommandBuffer commandBuffer);
    void endRenderPass(VkCommandBuffer commandBuffer);

    // Getters
    VkRenderPass getRenderPass() const { return renderPass; }
    VkPipeline getPipeline() const { return pipeline; }
    VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }
    VkImageView getDepthImageView() const { return depthImageView; }
    VkSampler getSampler() const { return sampler; }
    uint32_t getWidth() const { return width; }
    uint32_t getHeight() const { return height; }

    // Extra shadow pipelines for non-static geometry
    VkPipeline getCharacterShadowPipeline() const { return characterPipeline; }
    VkPipelineLayout getCharacterShadowLayout() const { return characterPipelineLayout; }
    VkPipeline getKinematicShadowPipeline() const { return kinematicPipeline; }
    VkPipelineLayout getKinematicShadowLayout() const { return kinematicPipelineLayout; }
    VkPipeline getDynamicShadowPipeline() const { return dynamicPipeline; }
    VkPipelineLayout getDynamicShadowLayout() const { return dynamicPipelineLayout; }

    /// docs/VoxelFoundationPlan.md F2.4: caster pipeline for the 1/8 m face preview (8 B PackedFace instances,
    /// voxfound_face_shadow.vert). Same layout (getPipelineLayout) and state as the chunk pipeline. Created on
    /// first call; VK_NULL_HANDLE if it cannot be built (logged once).
    VkPipeline ensureVoxFoundPipeline();
    /// F2.14 (§4.6 item 7): the same caster with the per-fragment level test (voxfound_face_shadow_lod.vert/.frag:
    /// the main view's size-on-screen keep test at the caster fragment, floored by the cascade's texel). Own layout:
    /// set 0 = the chunk-data layout, push = VoxFoundLodShadowPC (vertex + fragment). Lazy; VK_NULL_HANDLE on failure.
    VkPipeline ensureVoxFoundLodPipeline();
    VkPipelineLayout getVoxFoundLodLayout() const { return voxFoundLodLayout; }
    struct VoxFoundLodShadowPC {
        glm::mat4 lightSpaceMatrix;
        glm::vec3 chunkBaseOffset;   // CAMERA-RELATIVE origin of voxel (0,0,0) of the draw
        uint32_t lodBits;            // the voxel.frag flag bits: maxLevel << 20 | 1 << 23 | code << 24
        uint32_t level;              // this draw's level
        float lodFloor;              // log2(8 * texel): the coarsest level the shadow map cannot tell from 1/8 m
    };
    /// F2.12: a set of the chunk-data layout (set 0 of getPipelineLayout) bound to an external per-draw origin table
    /// (the preview's per-cluster chunk origins), for voxfound_face_shadow.vert's multidraw path. Rewritten (after a
    /// device wait) when the buffer or its generation changes (a recycled handle must not skip the rewrite). VK_NULL_HANDLE on failure.
    VkDescriptorSet voxFoundOriginSet(VkBuffer buffer, VkDeviceSize bytes, uint64_t generation);

    // ---- C2.1 (docs/ContinuousLodPlan.md): per-draw chunk data for multidraw ----
    /// One multidraw per arena BLOCK needs the per-chunk origin out of push constants,
    /// because an indirect draw cannot vary them. Origins live in this SSBO, indexed by
    /// gl_DrawIDARB. Bound unconditionally (a descriptor set must be valid even when the
    /// legacy path ignores it); the shader only reads it when the push-constant flag says so,
    /// so the default path stays byte-identical.
    /// PER-FRAME: MAX_FRAMES_IN_FLIGHT is 2 and these buffers are rewritten from the CPU every
    /// frame, so a SINGLE buffer is a write-after-read hazard against the previous frame still
    /// reading it on the GPU. Found by audit 2026-07-30; the first pixel diff passed only by
    /// timing luck, and the hazard is a plausible cause of the intermittent multi-ms spikes seen
    /// only in the GPU-driven series.
    VkDescriptorSet getChunkDataSet(uint32_t frame) const { return chunkDataSet[frame % kFrames]; }
    /// Max per-draw entries the SSBO holds. Sized well past the ~131 shadow draws measured in
    /// docs/RenderDensityPlan.md, with room for brick-granular submission later.
    static constexpr uint32_t kMaxChunkDataEntries = 16384;
    /// Upload `count` chunk origins (xyz used, w padding for std430 vec4 stride).
    void uploadChunkOrigins(uint32_t frame, const glm::vec4* origins, uint32_t count);
    /// Indirect command buffer for the shadow pass, host-visible and persistently mapped.
    VkBuffer getIndirectBuffer(uint32_t frame) const { return indirectBuffer[frame % kFrames]; }
    void* getIndirectMapped(uint32_t frame) const { return indirectMapped[frame % kFrames]; }
    static constexpr uint32_t kMaxIndirectCommands = 16384;

private:
    Vulkan::VulkanDevice* device;
    uint32_t width;
    uint32_t height;

    // Resources
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthImageMemory = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    // Shadow pipelines for non-static geometry
    VkPipelineLayout characterPipelineLayout = VK_NULL_HANDLE;
    VkPipeline characterPipeline = VK_NULL_HANDLE;
    VkPipelineLayout kinematicPipelineLayout = VK_NULL_HANDLE;
    VkPipeline kinematicPipeline = VK_NULL_HANDLE;
    VkPipelineLayout dynamicPipelineLayout = VK_NULL_HANDLE;
    VkPipeline dynamicPipeline = VK_NULL_HANDLE;
    VkPipeline voxFoundPipeline = VK_NULL_HANDLE;   // F2.4 (lazy)
    bool voxFoundFailed = false;
    VkPipeline voxFoundLodPipeline = VK_NULL_HANDLE;           // F2.14 (lazy)
    VkPipelineLayout voxFoundLodLayout = VK_NULL_HANDLE;
    bool voxFoundLodFailed = false;
    VkPipeline buildVoxFoundPipeline(const char* vertSpv, const char* fragSpv, VkPipelineLayout layout);
    VkDescriptorPool voxFoundOriginPool = VK_NULL_HANDLE;   // F2.12 (lazy)
    VkDescriptorSet  voxFoundOriginSetHandle = VK_NULL_HANDLE;
    VkBuffer         voxFoundOriginBound = VK_NULL_HANDLE;
    uint64_t         voxFoundOriginGeneration = UINT64_MAX;

    // (The old m_shadowRange member + UI slider were DEAD — nothing in the render path read
    // them; the live reach knob is RenderCoordinator::s_shadowDistance, POST /api/debug/shadow.
    // Removed 2026-08-05.)

    // C2.1 per-draw chunk data + indirect commands
    static constexpr uint32_t kFrames = 2;   // must match VulkanDevice::MAX_FRAMES_IN_FLIGHT
    VkDescriptorSetLayout chunkDataLayout = VK_NULL_HANDLE;
    VkDescriptorPool      chunkDataPool   = VK_NULL_HANDLE;
    VkDescriptorSet       chunkDataSet[kFrames]    = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkBuffer              chunkDataBuffer[kFrames] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkDeviceMemory        chunkDataMemory[kFrames] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    void*                 chunkDataMapped[kFrames] = { nullptr, nullptr };
    VkBuffer              indirectBuffer[kFrames]  = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkDeviceMemory        indirectMemory[kFrames]  = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    void*                 indirectMapped[kFrames]  = { nullptr, nullptr };
    bool createChunkDataResources();

    // Internal creation methods
    bool createDepthResources();
    bool createRenderPass();
    bool createFramebuffer();
    bool createPipeline();
    bool createSampler();
    bool createCharacterShadowPipeline();
    bool createKinematicShadowPipeline();
    bool createDynamicShadowPipeline();
};

} // namespace Graphics
} // namespace Phyxel
