#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <utility>

// WaterCore Phase D slice D3 (docs/WaterCore.md 16.4): the key the span render grid rebuilds on.
//
// The grid used to rebuild when `chunkMap.size()` changed. Streaming routinely swaps N chunks for N
// others (walk along a shoreline), so the count stayed equal while the SET changed and the grid
// kept drawing dry water over chunks that held spans (5,120 columns measured by the camera-walk
// probe, WaterRethink WP0). The key is therefore a function of the resident set itself, plus the
// two runtime inputs that change what the grid should hold without any residency change: the span
// revision (a write-back, an edit or a ground sync rewrote spans in resident chunks) and the awake
// active-volume revision (an AV woke or slept, so the set of columns the grid must leave to the AV
// surface changed).
//
// Residency is a COST/coverage input here (the rule terrain obeys: water renders where its ground
// is); it never decides whether water exists - that is the spans' job.
namespace Phyxel::Core {

/// FNV-1a over the 12 bytes of a chunk coordinate, then a finaliser so that summing per-coordinate
/// hashes (the order-independent combine below) does not cancel for simple symmetric sets.
inline uint64_t chunkCoordHash64(const glm::ivec3& c) {
    uint64_t h = 1469598103934665603ull;
    const int32_t v[3] = {c.x, c.y, c.z};
    for (int i = 0; i < 3; ++i) {
        const uint32_t u = static_cast<uint32_t>(v[i]);
        for (int b = 0; b < 4; ++b) {
            h ^= (u >> (8 * b)) & 0xffu;
            h *= 1099511628211ull;
        }
    }
    // splitmix64 finaliser: spreads the low entropy of small coordinates over all 64 bits.
    h ^= h >> 30; h *= 0xbf58476d1ce4e5b9ull;
    h ^= h >> 27; h *= 0x94d049bb133111ebull;
    h ^= h >> 31;
    return h;
}

/// coordOf: accept both a bare coordinate and an unordered_map<ivec3, Chunk*> value_type.
inline const glm::ivec3& coordOf(const glm::ivec3& c) { return c; }
template <typename V>
inline const glm::ivec3& coordOf(const std::pair<const glm::ivec3, V>& kv) { return kv.first; }

/// Order-independent hash of a set of chunk coordinates: the sum (mod 2^64) of the per-coordinate
/// hashes, mixed with the count. Iteration order of an unordered_map cannot change it.
template <typename It>
uint64_t residentSetHash(It begin, It end) {
    uint64_t sum = 0, count = 0;
    for (It it = begin; it != end; ++it) {
        sum += chunkCoordHash64(coordOf(*it));
        ++count;
    }
    // Mix the count in so that a set and its own hash-sum collision class differ by size at least.
    uint64_t h = sum ^ (count * 0x9e3779b97f4a7c15ull);
    h ^= h >> 29;
    return h;
}

struct SpanGridKey {
    uint64_t residency = 0;      ///< residentSetHash over the resident chunk set
    uint64_t spanRevision = 0;   ///< ChunkManager::waterSpanRevision(): runtime span writes
    uint64_t awakeRevision = 0;  ///< WaterCoreManager::avRevision(): AVs waking/sleeping
    uint64_t lookRevision = 0;   ///< RenderCoordinator: look / wind / wave changes re-pack the grid's G/B/A (Phase D5)
    bool operator==(const SpanGridKey& o) const {
        return residency == o.residency && spanRevision == o.spanRevision && awakeRevision == o.awakeRevision && lookRevision == o.lookRevision;
    }
    bool operator!=(const SpanGridKey& o) const { return !(*this == o); }
};

template <typename It>
SpanGridKey makeSpanGridKey(It begin, It end, uint64_t spanRevision, uint64_t awakeRevision, uint64_t lookRevision = 0) {
    SpanGridKey k;
    k.residency = residentSetHash(begin, end);
    k.spanRevision = spanRevision;
    k.awakeRevision = awakeRevision;
    k.lookRevision = lookRevision;
    return k;
}

}  // namespace Phyxel::Core
