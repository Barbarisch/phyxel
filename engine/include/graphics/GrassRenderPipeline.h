#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>

namespace Phyxel {
namespace Graphics {

/// Renders the lightweight grass-blade layer on top of grass-topped terrain voxels.
///
/// Strategy (see the grass plan):
///   - One GPU instance per grass voxel (GrassInstanceData, 8 bytes), stored in each chunk's own
///     grassBuffer (parallel to its face buffer, built at remesh). No per-frame CPU work.
///   - The vertex shader (grass.vert) procedurally fans each instance into `bladesPerVoxel` blades
///     via gl_VertexIndex — 6 vertices/blade, no vertex buffer. Wind + sprout-in growth read
///     UBO.elapsedTime; distance fade reads UBO.cameraPosition.
///   - Cutout (alpha-tested via discard in grass.frag), so it renders in the opaque scene pass with
///     NO transparency/OIT sort cost. Cull OFF (blades are double-sided).
///   - Reuses the existing set-0 descriptor (UBO + texture arrays) — colour is sampled from the
///     voxel's own grass-top texture, so biome variants (forest/savanna) tint automatically.
class GrassRenderPipeline {
public:
    GrassRenderPipeline();
    ~GrassRenderPipeline();

    /// @param uboDescriptorSetLayout  Existing set-0 layout (UBO + atlas arrays + shadow + lights)
    bool initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                    VkRenderPass renderPass, VkExtent2D extent,
                    VkDescriptorSetLayout uboDescriptorSetLayout);

    void cleanup();

    /// Runtime-tunable knobs (Phase 3). Conservative defaults keep the cost bounded.
    struct Params {
        bool     enabled        = true;
        float    radius         = 48.0f;  ///< world units: grass drawn only within this of camera
        float    fadeRange      = 14.0f;  ///< world units of fade-to-zero height before the radius edge
        float    bladeHeight    = 0.32f;  ///< full-grown blade height (world units; character ≈ 1.75)
        float    windStrength   = 0.13f;  ///< tip sway amplitude (scaled by blade height in-shader)
        float    growDuration   = 6.0f;   ///< seconds for the sprout-in ramp
        uint32_t bladesPerVoxel = 20;     ///< procedural blades/voxel, grouped into tufts (~7/clump)
    };

    /// One draw record per visible grass chunk (assembled by RenderCoordinator from the chunks that
    /// survived static-geometry culling AND lie within Params::radius of the camera).
    struct ChunkDraw {
        VkBuffer  buffer;   ///< chunk's grass instance buffer
        uint32_t  count;    ///< grass instance count (voxels)
        glm::vec3 origin;   ///< chunk world origin (push-constant base offset)
    };

    /// Record grass draws. Call inside the scene render pass, after static geometry.
    /// @param uboSet  Per-frame set 0 (view/proj/elapsedTime/cameraPosition/atlas).
    void render(VkCommandBuffer cmd, VkDescriptorSet uboSet, const std::vector<ChunkDraw>& chunks);

    /// Recreate pipeline after swapchain resize (caller must re-run initialize()).
    void recreatePipeline(VkRenderPass renderPass, VkExtent2D extent);

    Params&       params()       { return m_params; }
    const Params& params() const { return m_params; }

private:
    void createPipeline(VkRenderPass renderPass, VkExtent2D extent, VkDescriptorSetLayout uboLayout);

    VkDevice         m_device         = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline       m_pipeline       = VK_NULL_HANDLE;
    Params           m_params;
};

} // namespace Graphics
} // namespace Phyxel
