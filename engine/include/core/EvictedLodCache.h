#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "core/Types.h"

namespace Phyxel {

class Chunk;

namespace Core {

/**
 * @brief World-look A1/A2 (docs/WorldLookBacklog.md) — far-LOD geometry for chunks that were
 * evicted WITHOUT ever being saved.
 *
 * The far-field representation gap: beyond the residency radius the only tiers are far terrain
 * (a bare heightmap — structurally cannot show a tree or a building) and the persisted LOD
 * pyramid (`chunk_lod_blobs`), which exists only for SAVED chunks. Streaming worlds never save
 * pristine generated terrain (regenerable ⇒ never dirty), so generated trees and structures
 * simply vanish at the unload radius.
 *
 * This cache closes the gap in memory: at eviction, while the chunk is still fully alive, its
 * level-1 LOD volume is built and stashed here (encoded with LodBlobCodec — the same format as
 * the persisted pyramid, typically a few KB per chunk). The far-LOD tier then serves evicted
 * chunks from this cache exactly as it serves saved ones from storage. No DB writes: the DB
 * stays the store of *edits*; this is a render cache with an LRU cap.
 *
 * Only levels >= 1 are served (the far tier never asks for level 0), and higher levels are
 * derived on demand by re-squashing the stored level-1 volume — the identical chain
 * buildAndPersist runs, so a memory-served level matches its storage-served twin.
 *
 * Thread model: main thread only (the streaming pump stashes, the render loop reads), same as
 * the rest of the chunk/render path. Not internally synchronized.
 */
class EvictedLodCache {
public:
    /// ⚠️ QUARANTINED DEFAULT-OFF (2026-08-02, user verdict: "squashing of trees just makes
    /// weird floating voxels ... very very broken"). The MECHANISM works — eviction stashes,
    /// the far tier serves, the A/B proved structures/trees appear at distance — but the LOOK
    /// of coarse-squashed ORGANIC content is unshippable at mid-distance: OR-occupancy turns a
    /// canopy into full solid cells with NOTHING under them (the support column between canopy
    /// and ground is empty), so from any elevated viewpoint the whole eviction band reads as
    /// floating confetti, and isolated trunks fatten into dark towers (the known M2 defect,
    /// here at scale). Ground-level verification poses hid this — the band compresses into the
    /// horizon; a plan view exposes it. Evidence pair: screenshot_20260802_094148 (on) vs
    /// _094253 (off), same pose.
    /// Re-enable only WITH a look fix, likely some combination of: a distance floor (only serve
    /// beyond ~2x the unload radius where cells merge into silhouettes), a per-cell coverage
    /// floor (sparse canopy cells culled, solid masses kept), support-aware squash, and the
    /// resident-path level cap. Toggle live via set_far_lod {"eviction_cache": true}.
    /// This flag gates the WIRING (ChunkManager's eviction stash + the renderer's cache union);
    /// the class itself stays fully functional so its tests keep meaning.
    static bool s_evictionFeedEnabled;

    /// Default LRU capacity in chunks. Entries are RLE blobs (measured ~1-8 KB each), so this
    /// bounds the cache at a few tens of MB worst-case.
    static constexpr size_t kDefaultCapacity = 4096;

    /// Build + stash the chunk's level-1 LOD blob. Always erases any prior entry for the same
    /// coord first (mirrors refreshPyramid's delete-then-rebuild: a chunk whose structure was
    /// removed must stop being served). Chunks with no sub/microcube content are NOT cached —
    /// same policy as LodPyramidService::shouldPersist: plain generated terrain is far
    /// terrain's job, and trees/structures/edits are exactly the sub/microcube carriers.
    void stash(const Chunk& chunk);

    /// Serve renderable faces for `lod` (>= 1), re-squashing the stored level-1 volume as
    /// needed. Returns false when the chunk has no entry. Touches the LRU.
    bool facesForLevel(const glm::ivec3& chunkCoord, int lod, std::vector<InstanceData>& outFaces);

    bool contains(const glm::ivec3& chunkCoord) const { return m_entries.count(chunkCoord) > 0; }

    /// Append every cached coord to `out` (for the far-LOD candidate set).
    void appendCoords(std::vector<glm::ivec3>& out) const;

    /// Bumped on every insert/erase/clear. The far-LOD tier rescans its candidate set when this
    /// changes — WITHOUT it, a chunk evicted while the camera is stationary would never be
    /// picked up (the candidate rescan is otherwise keyed on camera chunk crossings; that
    /// stationary blind spot is a documented defect class on FarTerrainManager).
    uint64_t revision() const { return m_revision; }

    void clear();
    size_t chunkCount() const { return m_entries.size(); }
    size_t totalBytes() const { return m_bytes; }
    void setCapacity(size_t maxChunks);

private:
    struct CoordHash {
        std::size_t operator()(const glm::ivec3& c) const {
            return std::hash<int>()(c.x) ^ (std::hash<int>()(c.y) << 1) ^
                   (std::hash<int>()(c.z) << 2);
        }
    };
    struct Entry {
        std::vector<uint8_t> blob;                 ///< LodBlobCodec-encoded level-1 volume
        std::list<glm::ivec3>::iterator lruIt;     ///< position in m_lru (front = hottest)
    };

    void eraseEntry(const glm::ivec3& coord);
    void evictOverCapacity();

    std::list<glm::ivec3> m_lru;
    std::unordered_map<glm::ivec3, Entry, CoordHash> m_entries;
    size_t m_capacity = kDefaultCapacity;
    size_t m_bytes = 0;
    uint64_t m_revision = 0;
};

} // namespace Core
} // namespace Phyxel
