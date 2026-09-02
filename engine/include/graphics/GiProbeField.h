#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <cstdint>

namespace Phyxel {
namespace Vulkan { class VulkanDevice; }
namespace Graphics {

/**
 * M5.1 — the indirect-light probe field (docs/UnifiedLightingPlan.md).
 *
 * A coarse grid of probes around the viewer. Each stores the irradiance arriving at its position
 * from the sky and from every registered emitter, computed by tracing the SAME sub-voxel occupancy
 * every other lighting consumer uses. Scene fragment shaders sample it trilinearly in place of a
 * flat ambient constant, so an interior is dark because geometry blocks the sky rather than because
 * a per-cell field says so.
 *
 * ⚠️ THIS IS A RENDER CACHE, NOT WORLD STATE. It is keyed on world position, rebuilt from geometry
 * every update, and never persisted or baked per chunk. Baking it per chunk would reinstate exactly
 * the per-cell light storage M0 deleted and M3-REDESIGN mistakenly brought back -- see the
 * contradiction note at the head of M3-REDESIGN in the plan.
 *
 * The pipeline binds the SHARED set-0 layout rather than a private one, because occupancy.glsl
 * hardcodes bindings 11/12 and exists precisely so the CPU mirror, the fragment shaders and this
 * compute pass cannot disagree about what is solid.
 */
class GiProbeField {
public:
    GiProbeField() = default;
    ~GiProbeField() = default;
    GiProbeField(const GiProbeField&) = delete;
    GiProbeField& operator=(const GiProbeField&) = delete;

    /// Grid extent. Deliberately modest for M5.1: this increment exists to measure whether the
    /// approach is affordable at all, and a grid too large to update would answer the wrong
    /// question. 48 x 24 x 48 at 2 u spacing covers 96 x 48 x 96 world units around the viewer.
    static constexpr int kDimX = 48;
    static constexpr int kDimY = 24;
    static constexpr int kDimZ = 48;
    static constexpr int kProbeCount = kDimX * kDimY * kDimZ;   // 55,296
    static constexpr float kSpacing = 2.0f;

    bool initialize(Vulkan::VulkanDevice* device);
    void cleanup();

    /// Record the probe-update dispatch. `set` must be the shared set-0 descriptor set for this
    /// frame (it carries occupancy 11/12, the light SSBO 3, and this field at 13).
    void recordUpdate(VkCommandBuffer cmd, VkDescriptorSet set,
                      const glm::vec3& gridOrigin, const glm::vec3& ambientColor,
                      const glm::ivec4& occBox);

    VkBuffer buffer() const { return m_buffer; }

    /// Grid origin snapped to the probe lattice, so the field does not shimmer as the viewer moves.
    /// Returned as xyz = origin, w = spacing, which is exactly what the shader push constant wants.
    static glm::vec4 gridFor(const glm::vec3& viewerWorld);

private:
    Vulkan::VulkanDevice* m_device = nullptr;
    VkBuffer         m_buffer   = VK_NULL_HANDLE;
    VkDeviceMemory   m_memory   = VK_NULL_HANDLE;
    VkPipeline       m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_layout   = VK_NULL_HANDLE;
};

}  // namespace Graphics
}  // namespace Phyxel
