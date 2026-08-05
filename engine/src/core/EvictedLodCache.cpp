#include "core/EvictedLodCache.h"

#include "core/Chunk.h"
#include "core/LodBlobCodec.h"
#include "core/LodChunkMesh.h"
#include "core/LodPyramidService.h"
#include "utils/Logger.h"

namespace Phyxel {
namespace Core {

// Quarantined default-OFF — see the header. The coarse-tree look must be fixed first.
bool EvictedLodCache::s_evictionFeedEnabled = false;

void EvictedLodCache::stash(const Chunk& chunk) {
    const glm::ivec3 coord = chunk.getWorldOrigin() / 32;
    // Delete-then-maybe-rebuild, mirroring LodPyramidService::refreshPyramid: if the chunk's
    // detail is gone by this eviction (structure demolished, tree felled), the stale entry must
    // go with it or the far tier keeps rendering the pre-edit world.
    eraseEntry(coord);
    if (!LodPyramidService::shouldPersist(chunk)) return;

    std::vector<std::string> palette;
    LodVolume v = LodChunkMesh::volumeFromChunk(chunk, &palette);
    v = squash(v, SquashConfig{});   // store level 1; higher levels re-squash on demand

    Entry e;
    e.blob = LodBlobCodec::encode(v, palette);
    m_bytes += e.blob.size();
    m_lru.push_front(coord);
    e.lruIt = m_lru.begin();
    m_entries.emplace(coord, std::move(e));
    ++m_revision;
    evictOverCapacity();
}

bool EvictedLodCache::facesForLevel(const glm::ivec3& chunkCoord, int lod,
                                    std::vector<InstanceData>& outFaces) {
    outFaces.clear();
    if (lod < 1) return false;
    auto it = m_entries.find(chunkCoord);
    if (it == m_entries.end()) return false;

    // Touch: this chunk is being served, so it is the last the LRU should shed.
    m_lru.splice(m_lru.begin(), m_lru, it->second.lruIt);

    LodVolume v;
    std::vector<std::string> palette;
    if (!LodBlobCodec::decode(it->second.blob.data(), it->second.blob.size(), v, palette))
        return false;   // never hand the renderer a half-built volume

    // The stored volume is level 1; chain squashes exactly as buildAndPersist does, so a
    // memory-served level is bit-identical to its storage-served twin. Stop once the volume
    // is a single cell — further squashing cannot add information.
    for (int l = 1; l < lod; ++l) {
        if (v.dim().x <= 1 && v.dim().y <= 1 && v.dim().z <= 1) break;
        v = squash(v, SquashConfig{});
    }
    LodChunkMesh::emitFaces(v, palette, outFaces);
    return true;
}

void EvictedLodCache::appendCoords(std::vector<glm::ivec3>& out) const {
    for (const auto& kv : m_entries) out.push_back(kv.first);
}

void EvictedLodCache::clear() {
    if (m_entries.empty()) return;
    m_entries.clear();
    m_lru.clear();
    m_bytes = 0;
    ++m_revision;
}

void EvictedLodCache::setCapacity(size_t maxChunks) {
    m_capacity = maxChunks > 0 ? maxChunks : 1;
    evictOverCapacity();
}

void EvictedLodCache::eraseEntry(const glm::ivec3& coord) {
    auto it = m_entries.find(coord);
    if (it == m_entries.end()) return;
    m_bytes -= it->second.blob.size();
    m_lru.erase(it->second.lruIt);
    m_entries.erase(it);
    ++m_revision;
}

void EvictedLodCache::evictOverCapacity() {
    while (m_entries.size() > m_capacity && !m_lru.empty()) {
        eraseEntry(m_lru.back());
    }
}

} // namespace Core
} // namespace Phyxel
