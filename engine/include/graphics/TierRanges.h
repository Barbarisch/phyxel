#pragma once

// Per-(face direction, voxel tier) instance ranges in a chunk's instance buffer
// (docs/PerfProgram2026-09.md, I5 tier census + I6 per-tier draw ranges).
//
// The buffer is ordered direction-major, tier-minor: bucket = dir * kTiers + tier. That is the order
// the mesher already produced (it builds cube faces, then subcube, then microcube faces, and the
// direction sort is a stable counting sort), so recording these ranges reorders nothing and cannot
// change the image.
//
// Pure functions, no Vulkan: unit-tested in tests/graphics/TierRangesTest.cpp.

#include <array>
#include <cstdint>
#include <vector>

namespace Phyxel::Graphics::TierRanges {

// Tiers are InstanceData scaleLevel (packedData bits 18-19).
constexpr uint32_t kCube = 0, kSub = 1, kMicro = 2, kLodCell = 3;
constexpr uint32_t kTiers = 4;
constexpr uint32_t kBuckets = 6 * kTiers;
constexpr uint32_t kAllTiers = (1u << kTiers) - 1u;

// offsets[b] = first instance of bucket b; offsets[kBuckets] = total instance count.
using Offsets = std::array<uint32_t, kBuckets + 1>;

inline uint32_t tierOf(uint32_t packed) { return (packed >> 18) & 0x3u; }
// faceID, bits 15-17: 0=+Z 1=-Z 2=+X 3=-X 4=+Y 5=-Y (clamped like the direction sort always has).
inline uint32_t dirOf(uint32_t packed) {
    const uint32_t d = (packed >> 15) & 0x7u;
    return d > 5u ? 5u : d;
}
inline uint32_t bucketOf(uint32_t packed) { return dirOf(packed) * kTiers + tierOf(packed); }

// Unit faces of this instance's own tier covered by one (possibly greedy-merged) face: what the
// face count would be with merging off. Cube extents are in packedData bits 20-25 / 26-31 (size-1);
// sub/micro extents are in the light word bits 16-23 / 24-31 (size-1), because for those tiers
// packedData bits 20-31 hold grid positions. A LOD cell counts as one face.
inline uint64_t unitFaces(uint32_t packed, uint32_t light) {
    const uint32_t t = tierOf(packed);
    if (t == kCube) return uint64_t(((packed >> 20) & 0x3Fu) + 1u) * uint64_t(((packed >> 26) & 0x3Fu) + 1u);
    if (t == kSub || t == kMicro) return uint64_t(((light >> 16) & 0xFFu) + 1u) * uint64_t(((light >> 24) & 0xFFu) + 1u);
    return 1u;
}

struct Run {
    uint32_t first;
    uint32_t count;
};

// The instance runs to draw for the directions in dirMask (bit d = faceID d) and tiers in tierMask
// (bit t = tier t), with contiguous kept buckets merged into one run. An EMPTY bucket that is not
// kept does not break a run (it holds no instances); a non-empty one does.
inline void buildRuns(const Offsets& off, uint32_t dirMask, uint32_t tierMask, std::vector<Run>& out) {
    out.clear();
    bool open = false;
    Run cur{0, 0};
    for (uint32_t b = 0; b < kBuckets; ++b) {
        const uint32_t first = off[b];
        const uint32_t count = off[b + 1] - first;
        const bool kept = ((dirMask >> (b / kTiers)) & 1u) && ((tierMask >> (b % kTiers)) & 1u);
        if (kept) {
            if (count == 0) continue;
            if (!open) { cur = {first, 0}; open = true; }
            cur.count += count;
        } else if (count != 0 && open) {
            out.push_back(cur);
            open = false;
        }
    }
    if (open) out.push_back(cur);
}

}  // namespace Phyxel::Graphics::TierRanges
