#pragma once
// docs/VoxelFoundationPlan.md §4.6 item 9, F2.16 -- edits on a region preview with an INCREMENTAL rebuild.
//
// Keeps, per chunk of a region preview, what an edit needs: the 1/8 m voxels (materials only, dense: 256^3 B), the
// border planes from the neighbour chunks, the level-0 faces in cluster order with their direction groups, the leaf
// sprigs and the per-block "blocks sight" bits. apply() sets the voxels of a brush, updates the neighbours' border
// planes, and rebuilds only what can change (Mesher.h meshRunsDelta, updateSprigs, updateBlockBlocking,
// applyClusteredDelta) -- exact vs a full rebuild (VoxFoundMesher.IncrementalRebuildEqualsAFullRebuild; live:
// the voxfound_preview "verify" option rebuilds the touched chunks with FaceBuilderGpu and compares).
// assemble() writes level 0 of a Preview from the store (the drawn lists are then re-uploaded whole: item 10).

#include <functional>
#include <vector>

#include <glm/glm.hpp>

#include <memory>
#include "core/voxfound/FaceBuilderGpu.h"
#include "core/voxfound/FacePool.h"
#include "core/voxfound/Mesher.h"
#include "core/voxfound/Preview.h"
#include "core/voxfound/StorageLab.h"

namespace Phyxel::Core::VoxFound {

class RegionEdit {
public:
    struct Chunk {
        glm::ivec3 chunk{0};         // engine chunk coordinate
        glm::ivec3 originVoxel{0};   // world 1/8 m voxel of grid voxel (0,0,0)
        BlockStore store;            // F3.2a: the voxels (materials), the F1 store instead of a dense 256^3 grid
        Neighbours border;
        std::vector<PackedFace> faces;     // level-0 solid faces, cluster order
        std::vector<FaceCluster> groups;   // their direction groups
        std::vector<PackedFace> sprigs;
        std::vector<uint8_t> blocking;
        BlockConnectivity conn;     // F3.5b: connect kept incrementally over `blocking`
        bool connStale = false;     // F3.5b: a legacy flood changed `blocking` behind `conn`
        FaceConnect connect{};
        bool edited = false;   // F3.3: an edit or a load changed its voxels (it differs from the generator: saved)
    };
    struct Stats {
        size_t chunksEdited = 0, chunksRebuilt = 0, voxelsChanged = 0;
        size_t facesRemoved = 0, facesAdded = 0, rowsMeshed = 0, sprigsAdded = 0;
        double facesMs = 0, sprigMs = 0, connectMs = 0, clusterMs = 0, setMs = 0, collapseMs = 0;
        double planMs = 0, rowsWallMs = 0, rowsGpuMs = 0;   // facesMs = plan + rows (device wall, or the CPU executor)
        size_t uploadBytes = 0, residentMisses = 0;
        size_t connectFloods = 0;
        size_t blocksFilled = 0;     // F3.2b: blocks written whole   // F3.1: floods run (only when a block's "blocks sight" bit changed)
        bool onDevice = false;
        std::vector<size_t> rebuilt;   // chunk indices re-planned (faces / sprigs / connectivity recomputed near the edit)
        std::vector<size_t> changed;   // F3.5a: those whose lists actually changed (the ones assemble must rewrite)
        glm::ivec3 boxLo{2147483647}, boxHi{-2147483647 - 1};   // F7.6d: the edit's box (world voxels, clipped)
    };

    void clear() {
        m_chunks.clear();
        m_pool.reset();
        m_in.clear();
        m_sprigSlack = 0.25;
    }
    bool empty() const { return m_chunks.empty(); }
    size_t chunkCount() const { return m_chunks.size(); }
    const Chunk& chunk(size_t i) const { return m_chunks[i]; }
    /// F3.2a: chunk i's voxels as a dense grid (dim + material), for the paths that need one (verify, replace_all).
    VoxelGrid dense(size_t i) const {
        VoxelGrid g;
        m_chunks[i].store.toGrid(g);
        return g;
    }
    size_t bytes() const;

    /// Starts a store for the chunks [lo, hi]. cards = leaf materials (no faces, sprigs); seeThrough = F2.10's set.
    void begin(glm::ivec3 chunkLo, glm::ivec3 chunkHi, int clusterBlocks, bool octantOrder, uint32_t sprigThreshold24);
    void setMaterialSets(const CardSet& cards, const CardSet& seeThrough) { m_cards = cards; m_seeThrough = seeThrough; }
    const CardSet& cards() const { return m_cards; }
    const CardSet& seeThrough() const { return m_seeThrough; }
    /// Adds a chunk as the region build made it (call in the build's chunk order): its grid, border and the GPU
    /// builder's output (faces + sprigs, any order).
    void addChunk(const RegionChunk& rc, const std::vector<PackedFace>& builderOut);
    /// F7.9c: addChunk's chunk without adding it (prepared off the swap frame), then inserted; rebox drops the chunks
    /// outside [lo, hi] and makes that the box (the pool and the resident keys start over); sprigParamsFor = the
    /// sprig parameters of a chunk at originVoxel; indexOf = the chunk's index or -1.
    Chunk makeChunk(const RegionChunk& rc, const std::vector<PackedFace>& builderOut) const;
    void insertChunk(Chunk&& c) { m_chunks.push_back(std::move(c)); }
    void rebox(glm::ivec3 chunkLo, glm::ivec3 chunkHi);
    SprigParams sprigParamsFor(glm::ivec3 originVoxel) const;
    int indexOf(glm::ivec3 chunk) const { return index(chunk); }
    glm::ivec3 chunkLo() const { return m_lo; }
    glm::ivec3 chunkHi() const { return m_hi; }
    /// Replaces chunk i's faces and sprigs by a full builder output (the A/B reference for the incremental lists).
    void replaceFaces(size_t i, const std::vector<PackedFace>& builderOut);

    /// Sets every voxel p (world 1/8 m) in [lo, hi] with inside(p) to `material` (clipped to the region) and rebuilds
    /// incrementally. `gpu` (optional): the planned rows are re-meshed on the device against resident chunk grids
    /// (FaceBuilderGpu::rowsDelta, keyed per store generation and chunk); null = the CPU executor (meshRunsPlan).
    Stats apply(glm::ivec3 lo, glm::ivec3 hi, const std::function<bool(glm::ivec3)>& inside, uint8_t material,
                FaceBuilderGpu* gpu = nullptr, std::string* err = nullptr);
    /// F3.2b: a BOX edit (every voxel in [lo, hi]); blocks it covers entirely are written whole (BlockStore::fillBlock).
    Stats applyBox(glm::ivec3 lo, glm::ivec3 hi, uint8_t material, FaceBuilderGpu* gpu = nullptr,
                   std::string* err = nullptr);
    /// F5.5c: sets each listed voxel (world 1/8 m, clipped to the region) to its own material and rebuilds
    /// incrementally over the cells' box (the bodies lab's bake: one call per baked body).
    Stats applyCells(const std::vector<std::pair<glm::ivec3, uint8_t>>& cells, FaceBuilderGpu* gpu = nullptr,
                     std::string* err = nullptr);
    /// F3.2b: the brush of size s (1, 2, 4, 8 voxels) whose cell holds world voxel `at` (brushCell). A size outside
    /// {1, 2, 4, 8} is refused: false and `err`.
    bool brush(glm::ivec3 at, int size, uint8_t material, Stats& st, FaceBuilderGpu* gpu = nullptr,
               std::string* err = nullptr);
    /// F3.2b: the first solid voxel along a ray (world 1/8 m voxel units; dir need not be normalised), within
    /// maxVoxels of the origin. normal = the face the ray entered through (0 if the origin is inside the voxel);
    /// distance in voxels along the normalised dir.
    struct PickHit {
        bool hit = false;
        glm::ivec3 voxel{0}, normal{0};
        double distance = 0.0;
        uint8_t material = 0;
        size_t steps = 0;   // voxels + skipped blocks visited
    };
    PickHit pick(glm::dvec3 origin, glm::dvec3 dir, double maxVoxels) const;
    /// F3.3: writes a saved chunk (same dims, materials in the region palette) into chunk i through the exact
    /// incremental rebuild (block by block where they differ; the neighbours' border planes follow) and marks it
    /// edited.
    Stats loadChunk(size_t i, const BlockStore& saved);
    /// F3.2b: the material of world voxel p (air outside the region).
    uint8_t voxelAt(glm::ivec3 p) const;
    /// F7.6d: the chunks whose voxels lie within `margin` voxels of the box [lo, hi] (none for an empty box)
    std::vector<size_t> chunksNear(glm::ivec3 lo, glm::ivec3 hi, int margin) const;
    /// F7.6d: the region's world voxel box
    glm::ivec3 voxelLo() const;
    glm::ivec3 voxelHi() const;
    /// F7.3: the store of engine chunk c (its voxel (0,0,0) = world voxel c * 256), nullptr when the region lacks it;
    /// and the region's chunk coordinates -- what VoxFound::WorldReads reads.
    const BlockStore* chunkStore(glm::ivec3 c) const {
        const int i = index(c);
        return i < 0 ? nullptr : &m_chunks[size_t(i)].store;
    }
    std::vector<glm::ivec3> chunkCoords() const {
        std::vector<glm::ivec3> out;
        for (const Chunk& ch : m_chunks) out.push_back(ch.chunk);
        return out;
    }

    /// Level 0 of pv (faces, clusters, chunk ranges, sprigs, connectivity) from the store; selectLevel(0). leafTex =
    /// the sprig texture per palette material. F2.17: level 0 is a FACE POOL image (FacePool): with `changed` (the
    /// chunks an edit rebuilt) only their spans are rewritten and pv.poolDirty names the ranges; without it (or on
    /// the first call, or when the pool overflows) every chunk is placed again (poolDirty.full).
    void assemble(Preview& pv, const std::vector<uint16_t>& leafTex, const std::vector<size_t>* changed = nullptr);
    const FacePool* pool() const { return m_pool.get(); }

    SprigParams sprigParams(size_t i) const;

private:
    int index(glm::ivec3 chunk) const;   // -1 = not in the store
    /// inside == nullptr: a BOX edit (blocks it covers entirely are written whole)
    /// source (F3.3, loadChunk): the voxels of chunk sourceChunk come from this store instead of `material`.
    Stats applyImpl(glm::ivec3 lo, glm::ivec3 hi, const std::function<bool(glm::ivec3)>* inside, uint8_t material,
                    FaceBuilderGpu* gpu, std::string* err, const BlockStore* source = nullptr,
                    size_t sourceChunk = SIZE_MAX,
                    const std::function<uint8_t(glm::ivec3)>* materialAt = nullptr);   // F5.5c: per voxel
    std::vector<Chunk> m_chunks;
    glm::ivec3 m_lo{0}, m_hi{-1};
    int m_clusterBlocks = 4;
    bool m_octant = true;
    uint32_t m_threshold24 = 0;
    uint64_t m_generation = 0;   // resident keys: generation << 20 | chunk index
    std::unique_ptr<FacePool> m_pool;   // F2.17: level 0's spans
    /// F3.1: each chunk's pool input, kept between edits (only the rebuilt chunks' are recomputed), and the Preview
    /// revision the last assemble produced (anything else touched the drawn arrays: copy whole).
    std::vector<PoolChunk> m_in;
    uint64_t m_assembledRevision = ~0ull;
    uint32_t m_sprigLayouts = 0;   // F3.1: times every sprig span was placed (the first assemble + overflows)
    double m_sprigSlack = 0.25;    // F3.1: slack per sprig span, x1.5 after each overflow
    bool m_legacyF31 = false;
    bool m_legacyBorder = false;
    bool m_legacyConnect = false;

public:
    /// F3.1 A/B: true = F2.17's edit path (rows over the whole slice width, the flood on every edit, every chunk's pool
    /// input rebuilt and the whole level image copied into the Preview). Same results, F2.17's cost.
    void setLegacyF31(bool on) { m_legacyF31 = on; }
    bool legacyF31() const { return m_legacyF31; }
    /// F3.5a A/B: true = F3.5's path (a neighbour re-planned whenever its border plane is written, every re-planned
    /// chunk assembled, the plan scanning every face). Same results.
    void setLegacyBorder(bool on) { m_legacyBorder = on; }
    bool legacyBorder() const { return m_legacyBorder; }
    /// F3.5b A/B: true = F3.5a's connectivity (the whole flood whenever a blocking bit flips). Same results.
    void setLegacyConnect(bool on) { m_legacyConnect = on; }
    bool legacyConnect() const { return m_legacyConnect; }
    size_t connectBytes() const;   // F3.5b: every chunk's labels and union-find
    uint32_t sprigLayouts() const { return m_sprigLayouts; }

private:
    void poolInput(size_t i);

public:
    /// FaceBuilderGpu::rowsDelta's resident key for chunk i of this store (unique across region builds).
    uint64_t residentKey(size_t i) const { return (m_generation << 20) | uint64_t(i); }

private:
    CardSet m_cards, m_seeThrough;
};

/// F5.5c: an area grid (any dims, materials as they are) as region chunks of 256^3: area voxel p is region voxel
/// p + offset (offset >= 0). Chunks [0, *chunkHi]; chunk c's originVoxel = c * 256; every voxel outside the area is
/// air and so is every border plane outside the region; inner border planes come from the neighbour chunk.
std::vector<RegionChunk> areaRegionChunks(const VoxelGrid& area, glm::ivec3 offset, glm::ivec3* chunkHi);

}  // namespace Phyxel::Core::VoxFound
