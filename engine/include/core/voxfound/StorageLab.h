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
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/voxfound/Mesher.h"
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
/// F7.9a: the pre-F7.9a build (one generator material lookup per voxel) -- the exactness reference for
/// buildTerrainArea, which looks a material up once per (metre column, surface metre, metre). setTerrainLegacy(true)
/// makes buildTerrainArea run the reference (the red / A-B knob).
VoxelArea buildTerrainAreaReference(WorldGenerator& gen, glm::ivec3 originMetres, glm::ivec3 sizeMetres);
void setTerrainLegacy(bool on);
/// F7.20 (tests): true = the 1/8 m terrain ignores road grading (follows bedHeight everywhere) -- the red.
void setRoadGradeStub(bool on);

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
/// `plantWindow` (optional; x = min X, y = min Z, z = max X, w = max Z, world columns, inclusive): only plants whose
/// column lies inside are stamped -- planFlora(rect, inset) on a bounded world is exactly the plants inside the inset
/// rect, so this reproduces today's bounded decoration (GameDefinitionLoader: decorateFlora, inset 8) chunk by chunk.
FloraStampStats stampFlora(VoxelArea& area, WorldGenerator& gen, const std::string& templateDir, int marginM = 24,
                           const glm::ivec4* plantWindow = nullptr);

/// F2.8: a region of whole 32 m engine chunks at 1/8 m, generated ONE CHUNK AT A TIME (each chunk is
/// buildTerrainArea + stampFlora over the chunk plus a 1 m margin), so a region costs one chunk of memory at a
/// time. Each chunk comes with its border solidity (Neighbours, from the margin), so faces built per chunk equal
/// the region built whole (F2.7). Materials are indices into ONE region palette (stable once assigned).
/// "Chunks must not be visible": the voxels of every chunk equal the region generated whole (tested).
constexpr int kChunkM = 32;
struct RegionChunk {
    glm::ivec3 chunk{0};          // engine chunk coordinate (origin = chunk * kChunkM metres)
    glm::ivec3 originVoxel{0};    // world 1/8 m position of grid voxel (0,0,0)
    VoxelGrid grid;               // 256^3, materials index Region::palette
    Neighbours border;            // solidity of the voxel layer outside each face
    /// F2.13: the area the chunk was cut from (the chunk + a 1 m margin on every side, flora stamped, palette remapped);
    /// valid only during the callback. The margin lets coarse LOD levels (up to 1 m) see their real neighbours.
    const VoxelGrid* area = nullptr;
    int areaMarginVoxels = 0;
};
struct RegionBuild {
    std::vector<std::string> palette{"air"};
    FloraStampStats flora;        // summed over the chunks (a tree spanning chunks counts once per chunk)
    size_t chunks = 0;
};
/// Calls `each(chunk, palette so far)` for every chunk in [chunkLo, chunkHi] (inclusive), x-major.
/// `floraDir` null = bare terrain. `plantWindow`: as stampFlora (null = every plant, a streaming world).
RegionBuild forEachRegionChunk(
    WorldGenerator& gen, glm::ivec3 chunkLo, glm::ivec3 chunkHi, const std::string* floraDir,
    const std::function<void(const RegionChunk&, const std::vector<std::string>& palette)>& each,
    const glm::ivec4* plantWindow = nullptr,
    int threads = 1);

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
    BlockStore() : BlockStore(glm::ivec3(0)) {}
    /// F3.2a: the store of a dense grid (whole blocks written at once: uniform blocks stay one entry).
    static BlockStore fromGrid(const VoxelGrid& g);
    /// F3.2a: the dense grid of the store (dim + material only), for the paths that need one (verify, uploads).
    void toGrid(VoxelGrid& g) const;
    glm::ivec3 dim() const { return m_dim; }
    bool in(glm::ivec3 p) const {
        return p.x >= 0 && p.y >= 0 && p.z >= 0 && p.x < m_dim.x && p.y < m_dim.y && p.z < m_dim.z;
    }
    /// get() without the range check (p must be inside).
    uint8_t at(glm::ivec3 p) const {
        const glm::ivec3 b(p.x >> 3, p.y >> 3, p.z >> 3);
        const uint64_t e = m_dir[blockIndex(b)];
        if (!(e & kDetailed)) return uint8_t(e & 0xFF);
        return m_pool[uint32_t(e & 0xFFFFFFFFull)].mat[(p.z & 7) + 8 * ((p.y & 7) + 8 * (p.x & 7))];
    }
    /// F3.3 codec v3 (§3.6; format in docs/evidence/voxfound_f33_prediction.txt): 'PXF3', dims, a chunk-local
    /// palette (names from `palette`, indexed by this store's materials; entry 0 = air), the directory RLE in block
    /// order (tag 255 = detailed), then each detailed block's 512 voxels RLE. Canonical: a detailed block holding one
    /// material is written uniform. Empty result + `err` if the store uses more than 255 materials or a material has
    /// no palette name.
    std::vector<uint8_t> encodeV3(const std::vector<std::string>& palette, std::string* err = nullptr) const;
    /// Decodes a v3 blob: `out` gets the voxels (materials = indices into `names`, the blob's local palette).
    /// Bounds-checks everything; false (+ `err`) on malformed / truncated / unknown-version data.
    static bool decodeV3(const uint8_t* data, size_t size, BlockStore& out, std::vector<std::string>& names,
                         std::string* err = nullptr);
    /// Rewrites every material m to map[m] (map must cover every material in the store).
    void remapMaterials(const std::vector<uint8_t>& map);
    /// F3.2b: writes block b (block coordinates) as ONE uniform entry of material m (its detailed slot, if any, is
    /// freed) and returns how many of its voxels changed. Equal to 512 set() calls + collapse().
    int fillBlock(glm::ivec3 b, uint8_t m);
    /// true + the material when block b is one uniform entry (a detailed block may still hold one material until
    /// collapse()).
    bool uniformBlock(glm::ivec3 b, uint8_t& m) const {
        const uint64_t e = m_dir[blockIndex(b)];
        if (e & kDetailed) return false;
        m = uint8_t(e & 0xFF);
        return true;
    }
    void set(glm::ivec3 p, uint8_t material);
    uint8_t get(glm::ivec3 p) const;             // out of range reads air
    bool blockSolidAny(glm::ivec3 block) const;  // §3.2 solidity summary: any solid voxel in this block
    /// F7.1: solid voxels in block b, 0..512 (a uniform entry: 0 or 512; a detailed block: its mask's popcount).
    /// Out of range reads 0.
    int blockSolidCount(glm::ivec3 block) const;
    /// slack (F3.2b, the edit path): keep free pool slots for reuse until they exceed this fraction of the pool;
    /// 0 = compact and shrink every time (F1's behaviour, what memoryBytes() is measured after).
    void collapse(double slack = 0.0);
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

/// F3.2b (§1 "brush"): a brush is an axis-aligned cube of s = 1, 2, 4 or 8 voxels whose corner is a multiple of s.
inline bool brushSizeOk(int s) { return s == 1 || s == 2 || s == 4 || s == 8; }
inline int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
inline glm::ivec3 brushCell(glm::ivec3 p, int s) {
    return {floorDiv(p.x, s) * s, floorDiv(p.y, s) * s, floorDiv(p.z, s) * s};
}

/// F3.4 (§3.6, §7a): today's chunk blob (PXB2, ChunkBlobCodec v1/v2) -> a 256^3 store. A present cube becomes its
/// 8^3 block (exact); a cube cell holding subcubes / microcubes is a 9^3 grid of ninths resampled to 8^3 by volume per
/// material (air counted; ties: solid first, then the lower blob palette index). `names` = the blob's palette (entry
/// 0 = air ""), the store's materials index it. Tint, state and water spans are not carried (counted). False + err
/// on malformed / truncated / unknown-version blobs, or more than 255 palette entries.
struct Pxb2ConvertStats {
    int version = 0;
    size_t cubes = 0, subcubes = 0, microcubes = 0, cellsResampled = 0;
    size_t tinted = 0, withState = 0, waterSpans = 0;
    size_t rescued = 0;              // materials a cell's vote dropped, put back (§7a step 4)
    std::vector<double> volumeIn;    // m^3 per blob palette index: cubes 1, subcubes 1/27, microcubes 1/729
    std::vector<size_t> voxelsOut;   // 1/8 m voxels per blob palette index after the conversion (512 = 1 m^3)
};
bool convertPxb2(const uint8_t* data, size_t size, BlockStore& out, std::vector<std::string>& names,
                 Pxb2ConvertStats* stats = nullptr, std::string* err = nullptr);

/// F3.4: a region of whole chunks from STORED PXB2 blobs (a saved world) through convertPxb2 instead of the generator.
/// readBlob(chunk, out) returns false where the world has no row (that chunk is air). Every chunk of [lo-1, hi+1] is
/// converted once (the outer ring gives the border planes), materials remapped to one region palette; `each` sees the
/// region's chunks in forEachRegionChunk's order, with `area` null (no generated margin). Volume per material is
/// summed over the region's chunks (not the ring).
struct StoredRegionStats {
    size_t chunksStored = 0, chunksMissing = 0;
    Pxb2ConvertStats sum;
    double convertMs = 0.0;
    std::map<std::string, double> volumeIn, volumeOut;   // m^3 per material name
};
RegionBuild forEachStoredChunk(const std::function<bool(glm::ivec3, std::vector<uint8_t>&)>& readBlob,
                               glm::ivec3 chunkLo, glm::ivec3 chunkHi,
                               const std::function<void(const RegionChunk&, const std::vector<std::string>&)>& each,
                               StoredRegionStats* stats = nullptr, std::string* err = nullptr);


/// Compares every voxel. Returns the number of mismatches (first few described in `why`).
size_t compareToReference(const BlockStore& s, const VoxelGrid& ref, std::string* why = nullptr);

}  // namespace Phyxel::Core::VoxFound
