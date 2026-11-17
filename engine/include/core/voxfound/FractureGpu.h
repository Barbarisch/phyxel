#pragma once
// docs/VoxelFoundationPlan.md F4.3 -- the fracture core on the GPU.
//
// shaders/voxfound_fracture.comp, one thread per voxel: pattern ids, then cut bits + damage, then connected
// components over the uncut bonds (hook + pointer jumping, repeated until a round changes nothing). The per-hit
// scalars and tables come from planFracture (the host prelude the CPU reference uses too); piece numbering,
// anchoring and kinds come from finishPieces over the GPU's labels (shared host code, O(voxels)).
// Accepted only on EXACT equality with fractureReference: cut, damage, piece, pieceAnchored, pieceSize, pieceKind
// (VoxFoundFractureGpu, tests/integration).

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include "core/voxfound/FractureLab.h"

namespace Phyxel { namespace Vulkan { class ComputePipeline; } }

namespace Phyxel::Core::VoxFound {

class FractureGpu {
public:
    static constexpr int kMaxSide = 2048;          // keeps every q16 distance and product inside the shader's ranges
    static constexpr int kRoundsPerSubmit = 8;
    static constexpr int kMaxRounds = 512;

    FractureGpu();
    ~FractureGpu();
    FractureGpu(const FractureGpu&) = delete;
    FractureGpu& operator=(const FractureGpu&) = delete;

    bool init(VkDevice device, VkPhysicalDevice physical, VkQueue queue, uint32_t queueFamily,
              const std::string& shaderDir, std::string* err);
    void shutdown();
    bool initialized() const { return m_device != VK_NULL_HANDLE; }

    /// The same result as fractureReference(g, hit, mats, knobs). Refuses (false + reason) a side over kMaxSide or a
    /// toughness too large for the shader's 32-bit products.
    bool fracture(const VoxelGrid& g, const Hit& hit, const std::vector<FractureMaterial>& mats, FractureResult& out,
                  std::string* err, const FractureKnobs& knobs = {});

    double lastGpuMs() const { return m_lastGpuMs; }      // device time of all passes (timestamps; < 0 unavailable)
    int lastRounds() const { return m_lastRounds; }       // hook + jump rounds until nothing changed
    int lastSubmits() const { return m_lastSubmits; }

private:
    struct Buffer {
        VkBuffer buf = VK_NULL_HANDLE;
        VkDeviceMemory mem = VK_NULL_HANDLE;
        VkDeviceSize bytes = 0;
        void* mapped = nullptr;
    };
    bool createBuffer(Buffer& b, VkDeviceSize bytes, bool hostVisible, std::string* err);
    void destroyBuffer(Buffer& b);
    uint32_t findMemoryType(uint32_t filter, VkMemoryPropertyFlags props) const;
    bool ensure(Buffer& b, Buffer* staging, VkDeviceSize bytes, bool hostVisible, std::string* err);
    bool submit(const std::function<void(VkCommandBuffer)>& record, bool timed, double* ms, std::string* err);

    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physical = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    VkCommandPool m_pool = VK_NULL_HANDLE;
    VkFence m_fence = VK_NULL_HANDLE;
    VkQueryPool m_queries = VK_NULL_HANDLE;
    double m_timestampMs = 0.0;
    double m_lastGpuMs = -1.0;
    int m_lastRounds = 0, m_lastSubmits = 0;
    std::unique_ptr<Vulkan::ComputePipeline> m_pipe;
    Buffer m_vox, m_params, m_ids, m_out, m_label;     // device-local
    Buffer m_flags;                                    // host-visible: per round, the number of changes
    Buffer m_stageIn, m_stageOut;                      // host-visible
    bool m_bound = false;
};

}  // namespace Phyxel::Core::VoxFound
