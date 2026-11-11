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
//  - BlockStore (F1, 2026-11-11): the CPU form of the store (directory + detailed-block pool), tested equal
//    to the dense reference, including under random edit churn.
//
// Equality is defined against the dense reference of the SAME 1/8 m content, never against today's
// cube/subcube/microcube store (1/9 m content cannot equal 1/8 m content voxel for voxel). Today's store is
// the baseline for COST only (memory, frame time, face count).

#include <cstddef>
#include <cstdint>
#include <map>
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

/// A template on the foundation grid (`# grid: N` + B/V lines, written by tools/voxgrid.py, e.g.
/// tree_forge.py --grid 8). Cells are rebased to 0; bounds in cells.
struct GridTemplate {
    struct Box {
        glm::ivec3 pos, size;
        std::string material;
    };
    int cellsPerM = 0;
    glm::ivec3 bounds{0};
    std::vector<Box> boxes;
    size_t cells = 0;
    std::string error;  // non-empty: the file was refused (reason)
};
GridTemplate loadGridTemplate(const std::string& path);

/// Today's flora decoration (ObjectTemplateManager::decorateChunk), on the 1/8 m area: the ENGINE's
/// planFlora over the area's columns (plus `marginM` so canopies rooted outside reach in), each pool placement
/// stamped from `<templateDir>/<templateName>.voxel` (grid 8), footprint centred on the column centre, trunk
/// base on the area's ground at that column, clipped to the area. Like today's stamp, tree cells overwrite.
struct FloraStampStats {
    size_t placements = 0, stamped = 0, procedural = 0, missing = 0, cellsWritten = 0;
    std::map<std::string, int> stampedByName;
    std::vector<std::string> missingNames;
};
FloraStampStats stampFlora(VoxelArea& area, WorldGenerator& gen, const std::string& templateDir, int marginM = 24);

struct BlockStats {
    size_t blocks = 0, uniformAir = 0, uniformSolid = 0, detailed = 0;
    size_t blockColumns = 0;                  // (x, z) block columns
    size_t estimatedBytes() const;            // §3.1/3.2: 8 B directory per block + 576 B per detailed block
};
BlockStats blockStats(const VoxelGrid& g);    // grid dims must be multiples of kBlock

/// F1. The new store (§3.1-3.3), CPU form: a directory entry per 8^3 block (uniform material, or an index
/// into a pool of detailed blocks) and a pool of detailed blocks (512 B material + 64 B solidity mask).
/// set() marks the block changed; collapse() ends an edit batch: changed detailed blocks that became uniform
/// return to one directory entry, and the pool is compacted (the GPU pool will need the same; §3.4).
/// Counts and memoryBytes() are exact after collapse(); between collapses a block may be detailed but uniform.
class BlockStore {
public:
    explicit BlockStore(glm::ivec3 dimVoxels);   // dims must be multiples of kBlock
    glm::ivec3 dim() const { return m_dim; }
    void set(glm::ivec3 p, uint8_t material);
    uint8_t get(glm::ivec3 p) const;             // out of range reads air
    bool blockSolidAny(glm::ivec3 block) const;  // §3.2 solidity summary: any solid voxel in this block
    void collapse();
    size_t uniformBlocks() const;
    size_t detailedBlocks() const;
    size_t memoryBytes() const;                  // directory + pool, as allocated

    struct Detailed {
        uint8_t mat[512];
        uint64_t solid[8];
    };
    static_assert(sizeof(Detailed) == 576, "detailed block is 512 B material + 64 B mask (§3.1)");

private:
    // Directory entry (8 B): bit 63 = detailed, bit 62 = changed since the last collapse;
    // uniform: bits 0-7 = material; detailed: bits 0-31 = pool index.
    static constexpr uint64_t kDetailed = 1ull << 63, kChanged = 1ull << 62;
    size_t blockIndex(glm::ivec3 b) const {
        return size_t(b.z) + size_t(m_nb.z) * (size_t(b.y) + size_t(m_nb.y) * size_t(b.x));
    }
    glm::ivec3 m_dim, m_nb;
    std::vector<uint64_t> m_dir;
    std::vector<Detailed> m_pool;
    std::vector<uint32_t> m_free;
    std::vector<uint32_t> m_changed;             // directory indices marked changed
};

/// Compares every voxel. Returns the number of mismatches (first few described in `why`).
size_t compareToReference(const BlockStore& s, const VoxelGrid& ref, std::string* why = nullptr);

}  // namespace Phyxel::Core::VoxFound
