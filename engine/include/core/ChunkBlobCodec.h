#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <glm/glm.hpp>

namespace Phyxel {

class Chunk;

/**
 * @brief Storage format v2 — serializes a chunk to one compact binary blob.
 *
 * Replaces the v1 row-per-voxel SQLite layout (docs/LargeWorldScalePlan.md
 * Phase 1). Layout (little-endian):
 *
 *   u32 magic 'PXB2'  | u8 codecVersion | u8 flags (bit0 = u16 palette idx)
 *   u16 paletteCount  | u32 cubeCount | u32 subcubeCount | u32 microcubeCount
 *   palette entries   : paletteCount x (u8 nameLen, name bytes)
 *                       entry 0 is always air (empty name)
 *   cube section      : u32 runCount, then runCount x (u16 runLen, idx) —
 *                       RLE over the chunk's canonical z-minor index order
 *                       (index = z + y*32 + x*1024), run lengths sum to 32768
 *   subcube section   : subcubeCount x (u16 cubeIdx, u8 subIdx, idx,
 *                       u8 stateFlags, [3B RGB tint if bit7 of stateFlags])
 *   microcube section : microcubeCount x (u16 cubeIdx, u8 subIdx, u8 microIdx,
 *                       idx, u8 stateFlags, [3B RGB tint])
 *
 * `idx` is a palette index, u8 by default, u16 when flags bit0 is set
 * (paletteCount > 255). stateFlags bits 0-6 = voxel state, bit 7 = has tint
 * (tint bytes present only when set; absent means 0xFFFFFF / no tint).
 * Unlike v1, subcube/microcube tint and state round-trip.
 *
 * Only visible cubes/subcubes/microcubes are serialized (matches v1
 * semantics: presence == visible; subdivided placeholders are not stored).
 */
class ChunkBlobCodec {
public:
    static constexpr uint32_t kMagic = 0x32425850u; // "PXB2" little-endian
    static constexpr uint8_t kCodecVersion = 1;

    struct Counts {
        uint32_t cubes = 0;
        uint32_t subcubes = 0;
        uint32_t microcubes = 0;
    };

    /// Serialize the chunk. Never fails for a valid chunk; `outCounts`
    /// (optional) receives the serialized voxel counts for storage stats.
    static std::vector<uint8_t> encode(const Chunk& chunk, Counts* outCounts = nullptr);

    /// Deserialize into a chunk prepared with initializeForLoading().
    /// Bounds-validates every field; returns false (chunk contents
    /// unspecified) on malformed/truncated/unknown-version data.
    static bool decode(const uint8_t* data, size_t size, Chunk& chunk,
                       Counts* outCounts = nullptr);
};

} // namespace Phyxel
