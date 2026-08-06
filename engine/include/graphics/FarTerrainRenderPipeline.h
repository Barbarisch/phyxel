#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>

namespace Phyxel {
namespace Graphics {

/// Renders far-terrain LOD tiles (see FarTerrainManager / FarTerrainMesher).
///
/// Deliberately a SIBLING of the static voxel pipeline, not a variant of it:
///   - Dedicated 16-byte FarVertex format (indexed triangle-list quads), not InstanceData.
///   - Own winding/cull state, independent of the fragile FRONT_BIT static pipeline.
///   - Reuses the existing set-0 descriptor layout (UBO + texture atlas arrays), so far
///     tiles sample the same materials as near terrain.
/// Far tiles never enter the shadow pass (shadows are distance-capped anyway).
class FarTerrainRenderPipeline {
public:
    FarTerrainRenderPipeline();
    ~FarTerrainRenderPipeline();

    /// @param uboDescriptorSetLayout  Existing set-0 layout (UBO + atlas arrays + lights)
    bool initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                    VkRenderPass renderPass, VkExtent2D extent,
                    VkDescriptorSetLayout uboDescriptorSetLayout);

    void cleanup();

    /// One draw record per visible tile (assembled by RenderCoordinator after frustum
    /// culling the tile AABBs).
    struct TileDraw {
        VkBuffer  vertexBuffer;
        VkBuffer  indexBuffer;
        uint32_t  indexCount;
        glm::vec2 origin;   ///< tile world-space min corner (x, z) — push constant
    };

    /// Record tile draws. Call inside the scene render pass, after static geometry
    /// (near chunks fill depth first, so far-tile pixels behind them are z-rejected).
    void render(VkCommandBuffer cmd, VkDescriptorSet uboSet, const std::vector<TileDraw>& tiles);

    /// Camera-relative rendering: true camera world position, set per frame before
    /// render(). Tile origins are double-subtracted against it for clip space.
    void setCameraWorld(const glm::dvec3& cw) { m_cameraWorld = cw; }

    /// FAR-CASCADE shadow caster variant (depth-only, far_terrain_shadow.vert): far hills
    /// shade their own valleys past the mid map's reach. Create against the FAR shadow
    /// map's render pass/extent (static-viewport rule — see NearShadowCascade.md).
    bool initializeShadow(VkRenderPass shadowRenderPass, VkExtent2D shadowExtent,
                          VkDescriptorSetLayout uboLayout);
    void renderShadow(VkCommandBuffer cmd, VkDescriptorSet uboSet,
                      const std::vector<TileDraw>& tiles);

private:
    void createPipeline(VkRenderPass renderPass, VkExtent2D extent, VkDescriptorSetLayout uboLayout);

    VkDevice         m_device         = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline       m_pipeline       = VK_NULL_HANDLE;
    VkPipelineLayout m_shadowPipelineLayout = VK_NULL_HANDLE;
    VkPipeline       m_shadowPipeline = VK_NULL_HANDLE;
    glm::dvec3       m_cameraWorld    = glm::dvec3(0.0);  // camera-relative rendering
};

} // namespace Graphics
} // namespace Phyxel
