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
        VoxelGrid grid;              // dim + mat only (part / fixed left empty)
        Neighbours border;
        std::vector<PackedFace> faces;     // level-0 solid faces, cluster order
        std::vector<FaceCluster> groups;   // their direction groups
        std::vector<PackedFace> sprigs;
        std::vector<uint8_t> blocking;
        FaceConnect connect{};
    };
    struct Stats {
        size_t chunksEdited = 0, chunksRebuilt = 0, voxelsChanged = 0;
        size_t facesRemoved = 0, facesAdded = 0, rowsMeshed = 0, sprigsAdded = 0;
        double facesMs = 0, sprigMs = 0, connectMs = 0, clusterMs = 0, setMs = 0;
        double planMs = 0, rowsWallMs = 0, rowsGpuMs = 0;   // facesMs = plan + rows (device wall, or the CPU executor)
        size_t uploadBytes = 0, residentMisses = 0;
        bool onDevice = false;
        std::vector<size_t> rebuilt;   // chunk indices whose faces / sprigs / connectivity were updated
    };

    void clear() {
        m_chunks.clear();
        m_pool.reset();
    }
    bool empty() const { return m_chunks.empty(); }
    size_t chunkCount() const { return m_chunks.size(); }
    const Chunk& chunk(size_t i) const { return m_chunks[i]; }
    size_t bytes() const;

    /// Starts a store for the chunks [lo, hi]. cards = leaf materials (no faces, sprigs); seeThrough = F2.10's set.
    void begin(glm::ivec3 chunkLo, glm::ivec3 chunkHi, int clusterBlocks, bool octantOrder, uint32_t sprigThreshold24);
    void setMaterialSets(const CardSet& cards, const CardSet& seeThrough) { m_cards = cards; m_seeThrough = seeThrough; }
    const CardSet& cards() const { return m_cards; }
    const CardSet& seeThrough() const { return m_seeThrough; }
    /// Adds a chunk as the region build made it (call in the build's chunk order): its grid, border and the GPU
    /// builder's output (faces + sprigs, any order).
    void addChunk(const RegionChunk& rc, const std::vector<PackedFace>& builderOut);
    /// Replaces chunk i's faces and sprigs by a full builder output (the A/B reference for the incremental lists).
    void replaceFaces(size_t i, const std::vector<PackedFace>& builderOut);

    /// Sets every voxel p (world 1/8 m) in [lo, hi] with inside(p) to `material` (clipped to the region) and rebuilds
    /// incrementally. `gpu` (optional): the planned rows are re-meshed on the device against resident chunk grids
    /// (FaceBuilderGpu::rowsDelta, keyed per store generation and chunk); null = the CPU executor (meshRunsPlan).
    Stats apply(glm::ivec3 lo, glm::ivec3 hi, const std::function<bool(glm::ivec3)>& inside, uint8_t material,
                FaceBuilderGpu* gpu = nullptr, std::string* err = nullptr);

    /// Level 0 of pv (faces, clusters, chunk ranges, sprigs, connectivity) from the store; selectLevel(0). leafTex =
    /// the sprig texture per palette material. F2.17: level 0 is a FACE POOL image (FacePool): with `changed` (the
    /// chunks an edit rebuilt) only their spans are rewritten and pv.poolDirty names the ranges; without it (or on
    /// the first call, or when the pool overflows) every chunk is placed again (poolDirty.full).
    void assemble(Preview& pv, const std::vector<uint16_t>& leafTex, const std::vector<size_t>* changed = nullptr);
    const FacePool* pool() const { return m_pool.get(); }

    SprigParams sprigParams(size_t i) const;

private:
    int index(glm::ivec3 chunk) const;   // -1 = not in the store
    std::vector<Chunk> m_chunks;
    glm::ivec3 m_lo{0}, m_hi{-1};
    int m_clusterBlocks = 4;
    bool m_octant = true;
    uint32_t m_threshold24 = 0;
    uint64_t m_generation = 0;   // resident keys: generation << 20 | chunk index
    std::unique_ptr<FacePool> m_pool;   // F2.17: level 0's spans

public:
    /// FaceBuilderGpu::rowsDelta's resident key for chunk i of this store (unique across region builds).
    uint64_t residentKey(size_t i) const { return (m_generation << 20) | uint64_t(i); }

private:
    CardSet m_cards, m_seeThrough;
};

}  // namespace Phyxel::Core::VoxFound
