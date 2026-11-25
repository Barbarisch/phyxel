#pragma once

#include <cstdint>
#include <cstddef>

namespace Phyxel {

/**
 * @brief Storage format v2 (PXB2) — one chunk as one compact binary blob.
 *
 * Since docs/VoxelFoundationPlan.md D4 there is no Chunk object to encode or decode into: the format lives on as
 * what stored worlds contain. It is read by Core::VoxFound::convertPxb2 (StorageLab.h), and written by
 * Core::VoxFound::encodePxb2FromRows (the v1 row -> PXB2 migration). Layout (little-endian):
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
 *   water spans (v2)  : u32 spanCount, then spanCount x (u8 localX, u8 localZ, f32 bottom, f32 top)
 *
 * `idx` is a palette index, u8 by default, u16 when flags bit0 is set (paletteCount > 255). stateFlags bits 0-6 =
 * voxel state, bit 7 = has tint (tint bytes present only when set; absent means 0xFFFFFF / no tint).
 */
class ChunkBlobCodec {
public:
    static constexpr uint32_t kMagic = 0x32425850u; // "PXB2" little-endian
    /// v2 (docs/Water.md §2 layer 1): appends the WATER SPAN section after the microcubes. Readers accept v1 (no
    /// water section).
    static constexpr uint8_t kCodecVersion = 2;
    static constexpr uint8_t kMinDecodeVersion = 1;

    struct Counts {
        uint32_t cubes = 0;
        uint32_t subcubes = 0;
        uint32_t microcubes = 0;
    };
};

} // namespace Phyxel
