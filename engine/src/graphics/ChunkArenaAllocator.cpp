#include "graphics/ChunkArenaAllocator.h"

#include <algorithm>

namespace Phyxel {
namespace Graphics {

namespace {
inline size_t alignUp(size_t v, size_t a) { return (v + a - 1) / a * a; }
} // namespace

ChunkArenaAllocator::ChunkArenaAllocator(ArenaBlockBackend* backend, size_t blockSize,
                                         uint32_t emptyBlockRetireTicks)
    : m_backend(backend), m_blockSize(blockSize),
      m_emptyBlockRetireTicks(emptyBlockRetireTicks) {}

ChunkArenaAllocator::~ChunkArenaAllocator() {
    // Release every live block. Retired spans reference blocks we are about to
    // destroy — the whole allocator is going away, so just drop them.
    for (Block& b : m_blocks)
        if (b.alive) { m_backend->destroyBlock(b.handle); b.alive = false; }
}

ArenaSpan ChunkArenaAllocator::allocate(uint64_t regionKey, size_t bytes) {
    if (bytes == 0 || bytes > m_blockSize) return {};
    const size_t aligned = alignUp(bytes, kAlignment);
    if (aligned > m_blockSize) return {};

    // First fit across the region's existing blocks.
    auto regionIt = m_regions.find(regionKey);
    if (regionIt != m_regions.end()) {
        for (uint32_t blockId : regionIt->second) {
            if (!m_blocks[blockId].alive) continue;
            ArenaSpan s = tryAllocateFromBlock(blockId, aligned);
            if (s.valid()) return s;
        }
    }

    // Nothing fits: open a new block for this region.
    const int handle = m_backend->createBlock(m_blockSize);
    if (handle < 0) return {};
    const uint32_t blockId = uint32_t(m_blocks.size());
    Block b;
    b.handle = handle;
    b.regionKey = regionKey;
    b.size = m_blockSize;
    b.used = 0;
    b.freeList = {{0, m_blockSize}};
    b.emptySinceTick = m_tick;
    b.alive = true;
    m_blocks.push_back(std::move(b));
    m_regions[regionKey].push_back(blockId);
    return tryAllocateFromBlock(blockId, aligned);
}

ArenaSpan ChunkArenaAllocator::tryAllocateFromBlock(uint32_t blockId, size_t bytes) {
    Block& b = m_blocks[blockId];
    for (size_t i = 0; i < b.freeList.size(); ++i) {
        FreeRange& r = b.freeList[i];
        if (r.bytes < bytes) continue;
        ArenaSpan s;
        s.blockId = blockId;
        s.offset = r.offset;
        s.capacity = bytes;
        if (r.bytes == bytes) {
            b.freeList.erase(b.freeList.begin() + long(i));
        } else {
            r.offset += bytes;
            r.bytes -= bytes;
        }
        b.used += bytes;
        m_liveSpans += 1;
        m_bytesUsed += bytes;
        return s;
    }
    return {};
}

void ChunkArenaAllocator::retire(const ArenaSpan& span) {
    if (!span.valid() || span.blockId >= m_blocks.size()) return;
    Block& b = m_blocks[span.blockId];
    if (!b.alive) return;
    m_retired.push_back({span, m_tick + kRetireMargin});
    m_liveSpans -= 1;
    m_bytesUsed -= span.capacity;
    b.used -= span.capacity;
    if (b.used == 0) b.emptySinceTick = m_tick;
}

void ChunkArenaAllocator::tick() {
    m_tick += 1;

    // Return margin-expired retired spans to their free lists. reusableAtTick is
    // monotonic in insertion order, so the front of the deque expires first.
    while (!m_retired.empty() && m_retired.front().reusableAtTick <= m_tick) {
        returnToFreeList(m_retired.front().span);
        m_retired.pop_front();
    }

    // Release blocks that have been fully empty (no LIVE spans) for the window.
    // A block may still have entries in the retire queue (freed spans whose
    // bytes are waiting out the margin) — purge them at destroy time so nothing
    // later "returns" bytes into a dead (or reincarnated) block slot.
    for (uint32_t blockId = 0; blockId < uint32_t(m_blocks.size()); ++blockId) {
        Block& b = m_blocks[blockId];
        if (!b.alive || b.used != 0) continue;
        if (m_tick - b.emptySinceTick < m_emptyBlockRetireTicks) continue;
        m_backend->destroyBlock(b.handle);
        b.alive = false;
        b.freeList.clear();
        m_retired.erase(std::remove_if(m_retired.begin(), m_retired.end(),
                                       [blockId](const RetiredSpan& r) {
                                           return r.span.blockId == blockId;
                                       }),
                        m_retired.end());
        auto regionIt = m_regions.find(b.regionKey);
        if (regionIt != m_regions.end()) {
            auto& ids = regionIt->second;
            ids.erase(std::remove(ids.begin(), ids.end(), blockId), ids.end());
            if (ids.empty()) m_regions.erase(regionIt);
        }
    }
}

void ChunkArenaAllocator::returnToFreeList(const ArenaSpan& span) {
    if (span.blockId >= m_blocks.size()) return;
    Block& b = m_blocks[span.blockId];
    if (!b.alive) return;  // block released while this span waited out the margin

    // Insert sorted by offset, then coalesce with the previous/next ranges.
    FreeRange range{span.offset, span.capacity};
    auto it = std::lower_bound(b.freeList.begin(), b.freeList.end(), range,
                               [](const FreeRange& a, const FreeRange& c) {
                                   return a.offset < c.offset;
                               });
    it = b.freeList.insert(it, range);
    // Coalesce forward first (so `it` stays valid), then backward.
    if (it + 1 != b.freeList.end() && it->offset + it->bytes == (it + 1)->offset) {
        it->bytes += (it + 1)->bytes;
        b.freeList.erase(it + 1);
    }
    if (it != b.freeList.begin()) {
        auto prev = it - 1;
        if (prev->offset + prev->bytes == it->offset) {
            prev->bytes += it->bytes;
            b.freeList.erase(it);
        }
    }
}

int ChunkArenaAllocator::blockHandle(uint32_t blockId) const {
    if (blockId >= m_blocks.size() || !m_blocks[blockId].alive) return -1;
    return m_blocks[blockId].handle;
}

size_t ChunkArenaAllocator::liveBlocks() const {
    size_t n = 0;
    for (const Block& b : m_blocks) n += b.alive ? 1 : 0;
    return n;
}

size_t ChunkArenaAllocator::liveSpans() const { return m_liveSpans; }

size_t ChunkArenaAllocator::bytesUsed() const { return m_bytesUsed; }

size_t ChunkArenaAllocator::bytesCapacity() const {
    size_t n = 0;
    for (const Block& b : m_blocks) n += b.alive ? b.size : 0;
    return n;
}

} // namespace Graphics
} // namespace Phyxel
