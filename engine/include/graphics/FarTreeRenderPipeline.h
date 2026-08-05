#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>

namespace Phyxel {
namespace Graphics {

/// Far-tree impostors (world-look A1 rethink, 2026-08-02): one camera-facing procedural card
/// per tree, instanced per far-terrain tile. The instances come from the DETERMINISTIC flora
/// plan on the far-terrain worker (FarTerrainMesher::planTrees) — no chunk data, so forests
/// exist on tiles the camera has never visited, out to ~2 km.
///
/// This replaces the rejected chunk-squash representation for far trees ("weird floating
/// voxels"): a card is *shaped like a tree* (conifer cone / broadleaf canopy / palm / bare
/// snag, cutout-discarded in the fragment shader) instead of being reconstructed from voxel
/// wreckage. Cylindrical billboarding (yaw only) so trees never tilt with camera pitch.
///
/// Sibling of FarTerrainRenderPipeline: same set-0 layout, same camera-relative push scheme,
/// same scene-pass slot (after near geometry, so z-rejection does the near/far compositing).
/// Never enters the shadow pass.
class FarTreeRenderPipeline {
public:
    FarTreeRenderPipeline();
    ~FarTreeRenderPipeline();

    bool initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                    VkRenderPass renderPass, VkExtent2D extent,
                    VkDescriptorSetLayout uboDescriptorSetLayout);

    void cleanup();

    /// Distance band (world units, camera→tree): cards scale in from nothing across
    /// [fadeNear0, fadeNear1] — placed just past chunk residency so impostors never overlap
    /// real trees — and scale out across [fadeFar0, fadeFar1].
    struct Params {
        bool  enabled   = true;
        float fadeNear0 = 300.0f;
        float fadeNear1 = 360.0f;
        float fadeFar0  = 1850.0f;
        float fadeFar1  = 2050.0f;
    };
    Params& params() { return m_params; }
    const Params& params() const { return m_params; }

    /// One instanced draw per far-terrain tile (or per species-run, when used as the
    /// per-range fallback for a species the mesh tier couldn't build).
    struct TreeDraw {
        VkBuffer  instances = VK_NULL_HANDLE;  ///< FarTreeInstance array
        uint32_t  count = 0;
        glm::vec2 origin{0.0f};                ///< tile world-space min corner (x, z)
        uint32_t  firstInstance = 0;           ///< run start within the tile buffer
        float     minFade = 0.0f;              ///< residency handoff floor (near fade only)
    };

    /// Record draws inside the scene render pass, after far terrain.
    void render(VkCommandBuffer cmd, VkDescriptorSet uboSet, const std::vector<TreeDraw>& draws);

    /// True camera world position, set per frame before render() (camera-relative rendering).
    void setCameraWorld(const glm::dvec3& cw) { m_cameraWorld = cw; }

private:
    void createPipeline(VkRenderPass renderPass, VkExtent2D extent, VkDescriptorSetLayout uboLayout);

    VkDevice         m_device         = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline       m_pipeline       = VK_NULL_HANDLE;
    Params           m_params;
    glm::dvec3       m_cameraWorld    = glm::dvec3(0.0);
};

} // namespace Graphics
} // namespace Phyxel
