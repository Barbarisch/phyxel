#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Phyxel {

// ── Palette-compressed static voxel state (docs/LargeWorldScalePlan.md Phase 4.2) ──
//
// A chunk's 32³ cubes stored as (palette of material names) + (one index per voxel) + (one
// state byte per voxel) — i.e. the shape ChunkBlobCodec already writes to DISK for storage v2,
// brought into RAM.
//
// WHY: a heap `Cube` is ~176 B + ~48 B of Debug heap header, and a solid chunk holds 32,768 of
// them (~7.6 MB) — the dominant per-chunk cost after Phase 4.1. But a *static* voxel only ever
// needs its material (71 callers) and a visible bit (46); its position is derivable from the
// index, and every other field (bonds 72 B, voxelBody/physics 52 B, damage) is physics-only.
// This store holds exactly that static part in ~96 KB/chunk — ~80× less than the Cubes it will
// replace once authority flips (Phase 4.2b) and `Cube`s are materialized on demand.
//
// SCOPE (4.2a): this is a read-only MIRROR maintained alongside the authoritative `cubes`
// vector, so it can be proven equivalent before anything reads it. It costs ~96 KB/chunk
// (<1% of the current 10.5 MB) and changes no behaviour.
//
// NOT thread-safe; owned and mutated by ChunkVoxelManager on the chunk's own thread.
class ChunkVoxelStore {
public:
    static constexpr size_t kVoxels = 32 * 32 * 32;
    static constexpr uint16_t kEmpty = 0xFFFF;   // palette index meaning "air"

    // State byte layout (mirrors ChunkBlobCodec's stateFlags intent; bits 1-7 reserved for
    // the flags that follow when authority flips — broken/tint/etc).
    enum StateBit : uint8_t { kVisible = 1u << 0 };

    ChunkVoxelStore() { clear(); }

    // Drop all voxels AND the palette (fresh chunk).
    void clear();

    // Set the voxel at a flat index (z + y*32 + x*1024). Interns `material` into the palette.
    void set(size_t idx, const std::string& material, bool visible);
    // Mark a voxel empty. The palette entry is intentionally NOT reference-counted: palettes are
    // tiny (a handful of materials per chunk) and churn-free, so reclaiming entries would cost
    // more than it saves.
    void erase(size_t idx);

    bool solid(size_t idx) const { return idx < kVoxels && m_idx[idx] != kEmpty; }
    bool visible(size_t idx) const { return idx < kVoxels && (m_state[idx] & kVisible) != 0; }
    // Material name at a voxel; empty string when the voxel is air.
    const std::string& material(size_t idx) const;

    size_t paletteSize() const { return m_palette.size(); }
    size_t solidCount() const;

    // Bytes held by this store — for the Phase 4.2 RAM gate.
    size_t approxBytes() const;

private:
    uint16_t intern(const std::string& material);

    std::vector<std::string> m_palette;                      // index -> material name
    std::unordered_map<std::string, uint16_t> m_lookup;      // material name -> index
    std::vector<uint16_t> m_idx;                             // kVoxels palette indices
    std::vector<uint8_t> m_state;                            // kVoxels state bytes
};

} // namespace Phyxel
