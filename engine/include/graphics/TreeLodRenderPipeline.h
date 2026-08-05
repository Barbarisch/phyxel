#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>

namespace Phyxel {
namespace Graphics {

/// World Rendering v2, M2 — draws instanced far-tree LOD MESHES (TreeLodMeshRegistry levels)
/// placed by the per-tile FarTreeInstance buffers. One indexed instanced draw per
/// (tile, species-run). Fragment stage is far_terrain.frag: same atlas, same lighting as
/// far terrain, which is what unifies the look across tiers.
class TreeLodRenderPipeline {
public:
    TreeLodRenderPipeline();
    ~TreeLodRenderPipeline();

    bool initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                    VkRenderPass renderPass, VkExtent2D extent,
                    VkDescriptorSetLayout uboDescriptorSetLayout);
    void cleanup();

    struct Params {
        bool  enabled     = true;
        float fadeNear0   = 300.0f;   ///< dither-in start (synced per frame to loadDistance)
        float fadeNear1   = 360.0f;
        float bandEnd     = 1600.0f;  ///< tiles beyond this stay on cards (tile-center dist);
                                      ///< end of the L1..L5 ladder — cards cover to ~2 km
    };
    Params& params() { return m_params; }
    const Params& params() const { return m_params; }

    struct MeshDraw {
        VkBuffer  vertexBuffer = VK_NULL_HANDLE;   ///< species-level mesh
        VkBuffer  indexBuffer  = VK_NULL_HANDLE;
        uint32_t  indexCount   = 0;
        VkBuffer  instances    = VK_NULL_HANDLE;   ///< tile FarTreeInstance buffer
        uint32_t  firstInstance = 0;               ///< species run start
        uint32_t  instanceCount = 0;
        glm::vec2 origin{0.0f};                    ///< tile world-space min corner
        float     baseHeight = 8.0f;               ///< species card height (scale reference)
        float     minFade = 0.0f;                  ///< residency handoff floor (1 = chunks
                                                   ///< under this tile not resident: stay solid)
    };

    void render(VkCommandBuffer cmd, VkDescriptorSet uboSet, const std::vector<MeshDraw>& draws);
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
