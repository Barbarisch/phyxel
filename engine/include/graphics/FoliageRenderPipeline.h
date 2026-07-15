#pragma once

#include "graphics/WindSystem.h"
#include "core/KinematicVoxelManager.h"   // KinematicVoxelObject (kinematic foliage, F3)

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <unordered_map>

namespace Phyxel {
namespace Graphics {

/// Renders tree/bush leaves as lightweight cutout billboard cards, replacing solid leaf voxels.
///
/// Strategy (sibling of GrassRenderPipeline; see the foliage plan):
///   - The chunk mesher skips solid faces for "billboarded" leaf subcubes and emits one
///     FoliageInstanceData (8B) per EXPOSED leaf subcube into each chunk's foliageBuffer.
///   - foliage.vert fans each instance into `cardsPerVoxel` leaf cards (6 verts/card via
///     gl_VertexIndex, no vertex buffer), each a quad in a HASHED 3D orientation (crossed cards
///     filling the subcube volume → volumetric foliage from every angle).
///   - foliage.frag carves a ROUNDED leaf silhouette via discard (cutout → opaque pass, no OIT),
///     colour sampled from the voxel's leaf texture × baked light.
///   - Because cards fill the same subcubes the canopy already occupied, the tree's silhouette
///     envelope is preserved — the appearance changes from solid blob to leafy, not the shape.
///   - Unlike grass, NO aggressive distance fade: trees keep their leaves far away. Frustum
///     culling still applies; a generous radius knob exists only as a safety cap.
class FoliageRenderPipeline {
public:
    FoliageRenderPipeline();
    ~FoliageRenderPipeline();

    bool initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                    VkRenderPass renderPass, VkExtent2D extent,
                    VkDescriptorSetLayout uboDescriptorSetLayout);

    /// Create the shadow-caster variant (depth-only, same alpha cutout) against the shadow map's
    /// render pass. Call AFTER initialize(); safe to skip — foliage then simply casts no shadows.
    bool initializeShadow(VkRenderPass shadowRenderPass, VkExtent2D shadowExtent);

    void cleanup();

    /// Runtime-tunable knobs (Phase 4).
    struct Params {
        bool     enabled       = true;
        float    radius        = 512.0f; ///< safety cap (world units); large by default = "no fade"
        float    cardSize      = 0.42f;  ///< leaf card half-extent (world units; subcube ≈ 0.33)
        float    windStrength  = 0.05f;  ///< master wind amplitude (world units of sprig sway)
        uint32_t cardsPerVoxel = 5;      ///< leaf cards fanned per leaf subcube
        /// Shared wind-field state — overwritten every frame by RenderCoordinator from the
        /// single WindSystem (grass and foliage always see identical wind). Not user-tunable
        /// here; tune via /api/debug/wind.
        WindSystem::State wind;
    };

    /// One draw record per visible foliage chunk.
    struct ChunkDraw {
        VkBuffer  buffer;
        uint32_t  count;
        glm::vec3 origin;
    };

    /// Record foliage draws. Call inside the scene render pass, after grass.
    void render(VkCommandBuffer cmd, VkDescriptorSet uboSet, const std::vector<ChunkDraw>& chunks);

    /// Record foliage SHADOW draws (dappled canopy shadows). Call inside the shadow render pass.
    /// The main per-frame descriptor set is reused; the shadow shaders statically use only the
    /// UBO + albedo array bindings, so the shadow-map sampler entry in that set is never touched.
    void renderShadow(VkCommandBuffer cmd, VkDescriptorSet uboSet, const std::vector<ChunkDraw>& chunks);

    void recreatePipeline(VkRenderPass renderPass, VkExtent2D extent);

    // ---- KINEMATIC foliage (F3): leaf cards riding moving coherent fragments ----
    // A felled tree's canopy stays card-rendered while it falls: instances live in a
    // shared buffer with per-object ranges (mirrors KinematicVoxelPipeline); each draw
    // pushes model = currentTransform * translate(foliageOrigin). No wind, no shadow
    // casting (v1, disclosed). Call initializeKinematic AFTER initialize().
    bool initializeKinematic(VkRenderPass renderPass, VkExtent2D extent,
                             VkDescriptorSetLayout uboDescriptorSetLayout);
    /// Rebuild the shared kinematic-foliage instance buffer (call on manager dirty).
    void rebuildKinematicBuffer(
        const std::unordered_map<std::string, Core::KinematicVoxelObject>& objects);
    /// Draw kinematic foliage. Call in the scene pass after the chunk foliage.
    void renderKinematic(
        VkCommandBuffer cmd, VkDescriptorSet uboSet,
        const std::unordered_map<std::string, Core::KinematicVoxelObject>& objects);

    static constexpr size_t MAX_KINEMATIC_FOLIAGE = 16384;  ///< shared instance cap

    Params&       params()       { return m_params; }
    const Params& params() const { return m_params; }

private:
    void createPipeline(VkRenderPass renderPass, VkExtent2D extent, VkDescriptorSetLayout uboLayout);
    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const;

    VkDevice         m_device         = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline       m_pipeline       = VK_NULL_HANDLE;
    VkPipeline       m_shadowPipeline = VK_NULL_HANDLE;  // depth-only cutout caster (may be null)
    Params           m_params;

    // Kinematic foliage state (F3)
    struct KinRange { uint32_t first = 0, count = 0; };
    VkPipelineLayout m_kinPipelineLayout = VK_NULL_HANDLE;
    VkPipeline       m_kinPipeline       = VK_NULL_HANDLE;
    VkBuffer         m_kinBuffer         = VK_NULL_HANDLE;
    VkDeviceMemory   m_kinBufferMemory   = VK_NULL_HANDLE;
    std::unordered_map<std::string, KinRange> m_kinRanges;
};

} // namespace Graphics
} // namespace Phyxel
