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

private:
    void createPipeline(VkRenderPass renderPass, VkExtent2D extent, VkDescriptorSetLayout uboLayout);

    VkDevice         m_device         = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline       m_pipeline       = VK_NULL_HANDLE;
};

} // namespace Graphics
} // namespace Phyxel
