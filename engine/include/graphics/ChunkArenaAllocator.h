#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <vector>

namespace Phyxel {
namespace Graphics {

/**
 * Backend that materializes arena blocks. Production backs a block with one
 * VkBuffer + VkDeviceMemory (persistently mapped); unit tests mock it so the
 * allocator core is testable headless (docs/RegionArenaPlan.md A0).
 * Handles are opaque non-negative ints; -1 = creation failure.
 */
struct ArenaBlockBackend {
    virtual ~ArenaBlockBackend() = default;
    virtual int  createBlock(size_t bytes) = 0;
    virtual void destroyBlock(int handle) = 0;
};

/// A suballocated byte range inside an arena block. Invalid when blockId == kInvalid.
struct ArenaSpan {
    static constexpr uint32_t kInvalid = 0xFFFFFFFFu;
    uint32_t blockId  = kInvalid;  ///< allocator-internal block index (stable for block life)
    size_t   offset   = 0;         ///< byte offset inside the block (kAlignment-aligned)
    size_t   capacity = 0;         ///< usable bytes (>= requested, alignment-rounded)
    bool valid() const { return blockId != kInvalid; }
};

/**
 * Region-keyed GPU buffer suballocator (docs/RegionArenaPlan.md, Phase 4.3).
 *
 * Owns large backend blocks keyed by a spatial REGION (8x4x8 chunks in production;
 * the key is opaque here) and hands out aligned spans from per-block free lists.
 * Freed spans pass through a RETIRE queue: their bytes become reusable only
 * kRetireMargin tick()s later, honoring the same frames-in-flight contract as
 * DeferredBufferReclaim (a span freed this frame may still be referenced by an
 * in-flight command buffer). Blocks that stay fully empty for
 * emptyBlockRetireTicks are released to the backend.
 *
 * Single-threaded by contract (main thread), like all chunk GPU lifecycle today.
 */
class ChunkArenaAllocator {
public:
    static constexpr size_t   kAlignment        = 256;
    static constexpr size_t   kDefaultBlockSize = 64ull << 20;  // 64 MB
    static constexpr uint32_t kRetireMargin     = 3;            // ticks before byte reuse

    ChunkArenaAllocator(ArenaBlockBackend* backend,
                        size_t blockSize = kDefaultBlockSize,
                        uint32_t emptyBlockRetireTicks = 600);

    ~ChunkArenaAllocator();

    /// Allocate >= bytes (alignment-rounded) from the region's blocks; opens a new
    /// block when nothing fits. Returns an invalid span only on backend failure or
    /// bytes > blockSize.
    ArenaSpan allocate(uint64_t regionKey, size_t bytes);

    /// Queue a span's bytes for reuse after kRetireMargin ticks. The span handle
    /// is dead to the caller immediately.
    void retire(const ArenaSpan& span);

    /// Frame tick: advance the clock, return margin-expired retired spans to their
    /// free lists (with coalescing), release blocks empty for emptyBlockRetireTicks.
    void tick();

    /// Backend handle for a span's block (production: index into the VkBuffer table).
    int blockHandle(uint32_t blockId) const;

    // ---- stats (get_render_stats wiring lands in A3) ----
    size_t liveBlocks() const;
    size_t liveSpans() const;
    size_t bytesUsed() const;      ///< sum of live span capacities
    size_t bytesCapacity() const;  ///< sum of live block sizes

private:
    struct FreeRange { size_t offset, bytes; };
    struct Block {
        int      handle = -1;
        uint64_t regionKey = 0;
        size_t   size = 0;
        size_t   used = 0;                 // live span bytes
        std::vector<FreeRange> freeList;   // sorted by offset, coalesced
        uint64_t emptySinceTick = 0;       // valid when used == 0
        bool     alive = false;
    };
    struct RetiredSpan { ArenaSpan span; uint64_t reusableAtTick; };

    ArenaSpan tryAllocateFromBlock(uint32_t blockId, size_t bytes);
    void returnToFreeList(const ArenaSpan& span);

    ArenaBlockBackend* m_backend;
    size_t m_blockSize;
    uint32_t m_emptyBlockRetireTicks;
    uint64_t m_tick = 0;
    std::vector<Block> m_blocks;                          // blockId -> Block (ids stable)
    std::map<uint64_t, std::vector<uint32_t>> m_regions;  // regionKey -> block ids
    std::deque<RetiredSpan> m_retired;
    size_t m_liveSpans = 0;
    size_t m_bytesUsed = 0;
};

} // namespace Graphics
} // namespace Phyxel
