// RippleGpu - the ripple layer's step on the GPU (docs/WaterCore.md 25.34).
//
// RippleLayer stays the CPU reference and keeps all the bookkeeping (fixed-step and advection accumulators, mask and body
// labels, sources, sleep); this class runs the per-substep math as shaders/wc_ripple.comp, every layer of a frame in ONE
// submission, ASYNCHRONOUSLY: the water core's queue is the graphics queue, so a fenced wait in the frame would stall the
// CPU behind the frames' rendering (measured live with one frame of slack: 0.8-1.7 ms of waiting per frame). The device
// state chains on the device (one queue, in order), so a frame's step never waits for the last one's results: up to kRing
// submissions are in flight, each with its own upload staging and readback. The frame protocol is
//     collect(live layers)   - consume every submission whose fence has signalled (no wait; it waits only when the ring
//                              is full): the newest heights into each layer, its sleep rule; free dead layers' state
//     record(layer, dt, vel) - per layer, after its sources: the same planAdvect / planStep as the CPU, then the dispatches
//     submit()               - no wait
// so the heights a frame draws are one to kRing frames behind its sources. Only the current level comes back; the previous
// level is fetched on demand (syncPrev drains the ring first - before a CPU mutation that needs both levels), and a layer
// whose levels the CPU changed is re-uploaded before its next step (markDirty) - results still in flight for it are then
// dropped (the CPU's copy is newer).
#pragma once

#include "core/water/RippleLayer.h"
#include "vulkan/ComputePipeline.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Phyxel::Core::Water {

class RippleGpu {
public:
    struct Stats {
        int layers = 0, stepped = 0, dispatches = 0;   ///< last submission
        double recordMs = 0.0, collectMs = 0.0;        ///< CPU wall: recording + uploads (all layers), collect (wait + readback)
        double waitMs = 0.0;                           ///< the part of collect spent waiting on the fence
        double gpuMs = 0.0;                            ///< device time of the last submission (timestamps; 0 = unsupported)
        long uploadBytes = 0, readBytes = 0;
    };
    RippleGpu() = default;
    ~RippleGpu();
    RippleGpu(const RippleGpu&) = delete;
    RippleGpu& operator=(const RippleGpu&) = delete;

    bool init(VkDevice device, VkPhysicalDevice physical, VkQueue queue, uint32_t queueFamily, const std::string& shaderDir, std::string* err);
    void shutdown();
    bool ready() const { return m_device != VK_NULL_HANDLE && m_pool != VK_NULL_HANDLE; }

    void collect(const std::vector<RippleLayer*>& live);
    /// Steps `L` by `dt` on the device (with advection along `vel` when given). False = nothing this frame (asleep, or not a
    /// whole step yet and no advection due). Refused (false, `err` set) only when device memory is exhausted.
    bool record(RippleLayer& L, float dt, const std::vector<glm::vec2>* vel, std::string* err = nullptr);
    void submit();
    void syncPrev(RippleLayer& L, const std::vector<RippleLayer*>& live);
    /// Wait for and consume everything in flight (syncPrev, switching the backend off).
    void drain(const std::vector<RippleLayer*>& live);
    void markDirty(RippleLayer& L);
    void waitIdle();
    const Stats& stats() const { return m_stats; }
    /// TEST ONLY (25.34 red): the limiter's gather drops the backward edges - a porting fault the parity test must catch.
    void setLimiterFaultForTest(bool on) { m_limiterFault = on; }

private:
    struct Buf { VkBuffer buf = VK_NULL_HANDLE; VkDeviceMemory mem = VK_NULL_HANDLE; VkDeviceSize bytes = 0; void* mapped = nullptr; bool coherent = true; };
    struct State {
        uint64_t uid = 0; int nx = 0, nz = 0; size_t n = 0;
        Buf state, tmp, inp, vel, kern, lab, cells, chunks, part, range, means, broken, prevDown;
        Buf up[3], down[3];              ///< per ring slot (kRing): this slot's uploads, this slot's r + broken per tile
        std::unique_ptr<Vulkan::ComputePipeline> pipe, limit;   // wc_ripple.comp, wc_ripple_limit.comp
        int tiles = 0, tilesX = 0;   ///< the limiter's 16 x 16 core tiles
        int slot[4] = {0, 1, 2, 3};      ///< level slots: [0] r, [1] prev, [2] [3] free
        uint32_t labelRev = ~0u; int nComp = 0, nChunks = 0;
        bool dirty = true;               ///< upload r and prev before the next step
        size_t upUsed = 0;
    };
    bool createBuffer(Buf& b, VkDeviceSize bytes, VkBufferUsageFlags extra, int kind, std::string* err);   // kind 0 device, 1 upload, 2 readback
    void destroyBuffer(Buf& b);
    void destroyState(State& s);
    State* stateFor(RippleLayer& L, std::string* err);
    void ensureCommands();
    void barrier(VkCommandBuffer cmd) const;
    void dispatch(State& s, int mode, const void* push, uint32_t groups);
    VkDeviceSize stage(State& s, const void* data, size_t bytes);   // into s.up[m_cur], returns the offset
    void copyUp(State& s, VkDeviceSize upOff, Buf& dst, VkDeviceSize bytes, VkDeviceSize dstOff = 0);

    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physical = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    uint32_t m_family = 0;
    static constexpr int kRing = 3;
    struct Sub {
        VkFence fence = VK_NULL_HANDLE;
        bool active = false;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        std::vector<std::pair<uint64_t, bool>> layers;   ///< (layer uid, stepped - not only advected)
    };
    void consume(int k, const std::vector<RippleLayer*>& live);
    Sub m_subs[kRing];
    int m_cur = 0;                               ///< the slot this frame records into
    VkCommandPool m_pool = VK_NULL_HANDLE;
    VkQueryPool m_queries = VK_NULL_HANDLE;      ///< 2 per slot
    float m_tsPeriodNs = 0.0f;
    VkCommandBuffer m_cmd = VK_NULL_HANDLE;      ///< this frame's recording (null until the first record)
    bool m_limiterFault = false;
    std::string m_spv, m_spvLimit;
    std::unordered_map<uint64_t, State> m_states;
    Stats m_stats, m_building;
};

}  // namespace Phyxel::Core::Water
