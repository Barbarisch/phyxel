#pragma once

#include "core/TemplateLodChain.h"
#include "graphics/FarTerrainTypes.h"

#include <vulkan/vulkan.h>
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Phyxel {

class VoxelTemplate;

namespace Graphics {

/// World Rendering v2, M2 — per-species instanced LOD meshes.
///
/// For each tree species (TreeSpeciesTable) this registry builds the TemplateLodChain of its
/// representative template and meshes every level ONCE (neighbor-culled quads in the FarVertex
/// format, so instanced far trees sample the SAME material atlas as near voxels — identical
/// bark and leaf pixels, which is what kills the card tier's color mismatch). Distant trees
/// then render as instanced draws of these meshes: a forest of ten thousand oaks pays for one
/// mesh set plus a position buffer.
///
/// Lazy per species, built OFF-THREAD: the first request queues the species to a background
/// builder (chain + CPU meshing there), and tick() finalizes at most one finished species per
/// frame on the main thread (GPU upload only). Until then level() returns null and callers
/// draw cards. Meshes are template-local — trunk base at the origin, +Y up — and instances
/// place them via the per-tile FarTreeInstance buffers.
class TreeLodMeshRegistry {
public:
    /// Resolves a template name to its loaded VoxelTemplate (wired from the application's
    /// ObjectTemplateManager after template load). Returning null disables that species'
    /// mesh tier (its trees stay on cards).
    using TemplateProvider = std::function<const VoxelTemplate*(const std::string&)>;

    struct CpuMesh {
        std::vector<FarVertex> vertices;   ///< template-local, trunk base at origin
        std::vector<uint32_t>  indices;
    };

    /// Pure meshing of one chain level: exterior faces only (neighbors within the cell set
    /// cull shared faces). Static so tests exercise the shipped math headlessly.
    ///
    /// `anchor` places the template-local grid relative to the instance position and MUST
    /// replicate the near stamp: decorateChunk stamps at base = worldPos - maxExtent/2 while
    /// FarTreeInstance sits at the column CENTER (worldX + 0.5), so the matching anchor is
    /// -(maxExtent/2 + 0.5) on X/Z (see stampAnchorFor). A mismatched anchor renders the LOD
    /// tree laterally offset from the real tree it must dissolve into — seen live as a
    /// dithered ghost ~half a footprint beside the resident oak (tree_ladder_band, 2026-08-02).
    static CpuMesh buildLevelMesh(const Core::TemplateLodChain::Level& level,
                                  const FarMaterialResolver& resolveTex,
                                  const glm::vec3& anchor = glm::vec3(-0.5f, 0.0f, -0.5f));

    /// The stamp-parity anchor for a template (see buildLevelMesh doc).
    static glm::vec3 stampAnchorFor(const VoxelTemplate& t);

    struct GpuLevel {
        VkBuffer       vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
        VkBuffer       indexBuffer  = VK_NULL_HANDLE;
        VkDeviceMemory indexMemory  = VK_NULL_HANDLE;
        uint32_t       indexCount   = 0;
    };
    struct SpeciesMeshes {
        bool built = false;    ///< attempted (template missing => built with empty levels)
        std::array<GpuLevel, Core::TemplateLodChain::kLevelCount> levels{};
    };

    TreeLodMeshRegistry(VkDevice device, VkPhysicalDevice physicalDevice);
    ~TreeLodMeshRegistry();
    TreeLodMeshRegistry(const TreeLodMeshRegistry&) = delete;
    TreeLodMeshRegistry& operator=(const TreeLodMeshRegistry&) = delete;

    void setTemplateProvider(TemplateProvider p) { m_provider = std::move(p); }
    void setMaterialResolver(FarMaterialResolver r) { m_resolveTex = std::move(r); }
    bool ready() const { return bool(m_provider) && bool(m_resolveTex); }

    /// The GPU mesh for (species, level). A species not yet built is QUEUED for the
    /// background builder and null is returned — the caller keeps drawing cards until the
    /// set lands (building a big template takes real time: doing it here froze the UI for
    /// whole seconds when a zoom-out pulled several new species into view at once —
    /// user-reported "huge lag spikes").
    const GpuLevel* level(int speciesId, int levelIdx);

    /// Main-thread pump: uploads finished CPU mesh sets to the GPU (bounded per call).
    /// Call once per frame.
    void tick();

    /// Public upload helpers for the structure-LOD tier (RenderCoordinator): same
    /// host-visible buffer path the species meshes use.
    bool uploadLevel(const CpuMesh& cpu, GpuLevel& out);
    bool createInstanceBuffer(const FarTreeInstance& inst, VkBuffer& buf, VkDeviceMemory& mem);
    void destroyBuffer(VkBuffer buf, VkDeviceMemory mem);

    void cleanup();

private:
    struct BuiltSpecies {
        int speciesId = -1;
        std::array<CpuMesh, Core::TemplateLodChain::kLevelCount> levels;
    };

    void builderLoop();
    void stopBuilder();
    bool createHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage, const void* data,
                          VkBuffer& buffer, VkDeviceMemory& memory);

    VkDevice         m_device;
    VkPhysicalDevice m_physicalDevice;
    TemplateProvider m_provider;          ///< read from the builder thread — templates are
                                          ///< immutable after startup (same contract the
                                          ///< streaming flora worker relies on)
    FarMaterialResolver m_resolveTex;
    std::unordered_map<int, SpeciesMeshes> m_species;   // main thread only
    std::unordered_set<int> m_queued;                    // main thread only

    std::thread             m_builder;
    std::mutex              m_queueMutex;
    std::condition_variable m_queueCv;
    std::deque<int>         m_buildQueue;    // guarded by m_queueMutex
    std::mutex              m_doneMutex;
    std::vector<BuiltSpecies> m_done;        // guarded by m_doneMutex
    std::atomic<bool>       m_stop{false};
};

} // namespace Graphics
} // namespace Phyxel
