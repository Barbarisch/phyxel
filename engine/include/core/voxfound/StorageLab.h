#pragma once
// docs/VoxelFoundationPlan.md §3 + §8 — the storage lab (F0 skeleton; the store is built in F1).
//
// What exists now (F0):
//  - buildTerrainArea(): an area at 1/8 m generated from the ENGINE's terrain function
//    (WorldGenerator::sampleColumn: the continuous ground height bedHeight, bilinear between metre samples,
//    and materialForColumn for the layers). Provenance: engine terrain generator, sampled at 1/8 m by this
//    function. Not covered yet: flora, structures, creek shelves (all on thirds today; they arrive with FC).
//  - the DENSE REFERENCE (a VoxelGrid) and blockStats(): uniform vs detailed 8^3 blocks, the input to the
//    §3.5a memory estimate, measured on real generated terrain instead of guessed;
//  - BlockStore: the F1 store's interface with a STUB body (stores nothing, reads air). The storage-lab
//    equality tests against the dense reference are therefore RED until F1.
//
// Equality is defined against the dense reference of the SAME 1/8 m content, never against today's
// cube/subcube/microcube store (1/9 m content cannot equal 1/8 m content voxel for voxel). Today's store is
// the baseline for COST only (memory, frame time, face count).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/voxfound/VoxelGrid.h"

namespace Phyxel { class WorldGenerator; }

namespace Phyxel::Core::VoxFound {

constexpr int kVoxelsPerM = 8;
constexpr int kBlock = 8;  // voxels per block side

struct VoxelArea {
    glm::ivec3 originVoxel{0};                // world position of grid voxel (0,0,0), in 1/8 m units
    VoxelGrid grid;
    std::vector<std::string> palette;         // palette[material] = engine material name; [0] = air
};

/// Terrain of an area sizeMetres (multiples of 1 m) starting at originMetres, at 1/8 m voxels.
VoxelArea buildTerrainArea(WorldGenerator& gen, glm::ivec3 originMetres, glm::ivec3 sizeMetres);

struct BlockStats {
    size_t blocks = 0, uniformAir = 0, uniformSolid = 0, detailed = 0;
    size_t blockColumns = 0;                  // (x, z) block columns
    size_t estimatedBytes() const;            // §3.1/3.2: 8 B directory per block + 576 B per detailed block
};
BlockStats blockStats(const VoxelGrid& g);    // grid dims must be multiples of kBlock

/// F1. The new store. STUB in F0: set() stores nothing, get() returns air, counts are 0.
class BlockStore {
public:
    explicit BlockStore(glm::ivec3 dimVoxels);
    glm::ivec3 dim() const { return m_dim; }
    void set(glm::ivec3 p, uint8_t material);
    uint8_t get(glm::ivec3 p) const;
    size_t uniformBlocks() const;
    size_t detailedBlocks() const;
    size_t memoryBytes() const;

private:
    glm::ivec3 m_dim;
};

/// Compares every voxel. Returns the number of mismatches (first few described in `why`).
size_t compareToReference(const BlockStore& s, const VoxelGrid& ref, std::string* why = nullptr);

}  // namespace Phyxel::Core::VoxFound
