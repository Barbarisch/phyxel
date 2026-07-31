#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

#include "core/LodBrick.h"

namespace Phyxel {
namespace Core {

/**
 * @brief C3 (docs/ContinuousLodPlan.md) — serialize one LOD level of a chunk's pyramid.
 *
 * C3 exists to break the R^2 residency law measured in
 * docs/evidence/lod_residency_wall_20260730.txt: ~1.28 MB of working set per RESIDENT chunk,
 * with chunk count growing as R^2, which caps full-resolution view distance at roughly 1,760
 * units even on a 64 GB machine. The LOD cut (C4/C5) cannot help — a chunk must be loaded and
 * meshed to be drawn at ANY level. Persisting the pyramid lets a distant region be served from
 * coarse data WITHOUT its full-resolution chunk ever becoming resident.
 *
 * Keyed (x, y, z, lod) per godot_voxel's schema (LargeWorldScalePlan.md:857).
 *
 * Layout (little-endian), deliberately mirroring ChunkBlobCodec's conventions:
 *
 *   u32 magic 'PXL1' | u8 codecVersion | u8 level | u8 flags (bit0 = u16 palette idx)
 *   u16 paletteCount | u16 dimX | u16 dimY | u16 dimZ
 *   palette entries : paletteCount x (u8 nameLen, name bytes); entry 0 is always air
 *   cell section    : u32 runCount, then runCount x:
 *                       u16 runLen, u8 cellFlags
 *                       if solid    : u64 coverage, idx bulkMaterial, idx skinMaterial
 *                       if opening  : u64 openingCoverage
 *                     RLE over the volume's canonical z-minor order
 *                     (index = z + y*dim.z + x*dim.z*dim.y); run lengths sum to dim.x*y*z.
 *
 * cellFlags: bit0 = solid (coverage > 0), bit1 = preserveOpening.
 * An empty, opening-free run costs 3 bytes, which is what makes air cheap.
 *
 * `coverage` is stored as u64 because it is 729 * 8^level and overflows u32 at about level 8 —
 * the same trap that bit LodCell itself.
 */
class LodBlobCodec {
public:
    static constexpr uint32_t kMagic = 0x314C5850u;   // "PXL1" little-endian
    static constexpr uint8_t  kCodecVersion = 1;

    /// Serialize `volume` plus the material `palette` its cell ids index into.
    /// Never fails for a well-formed volume.
    static std::vector<uint8_t> encode(const LodVolume& volume,
                                       const std::vector<std::string>& palette);

    /// Deserialize. Returns false (outputs unspecified) on malformed, truncated, or
    /// unknown-version data, and NEVER throws — a corrupt row must not be able to hand the
    /// renderer a half-built volume or kill the process on the read path. Validated: magic,
    /// codec version, level range, declared dimensions (capped before the volume is allocated,
    /// since LodVolume reserves dx*dy*dz eagerly), palette-index range on every cell, and that
    /// the run lengths tile the volume EXACTLY.
    static bool decode(const uint8_t* data, size_t size,
                       LodVolume& outVolume, std::vector<std::string>& outPalette);
};

} // namespace Core
} // namespace Phyxel
