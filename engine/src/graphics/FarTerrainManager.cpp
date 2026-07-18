#include "graphics/FarTerrainManager.h"
#include "graphics/FarTerrainMesher.h"
#include "core/MaterialRegistry.h"
#include "core/WorldGenerator.h"
#include "utils/Logger.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Phyxel {
namespace Graphics {

namespace {
// Retired GPU buffers survive this many update() ticks before destruction, so no
// in-flight frame can still reference them (frames in flight ≤ 3; copy of the
// ChunkStreamingManager frame-deferred deletion pattern).
constexpr int kGraveyardFrames = 4;
// Recompute the wanted tile set when the camera has moved this far (world units).
constexpr float kRefreshDistance = 64.0f;
// Eviction hysteresis: tiles are kept while within this many tile-sizes beyond
// their ring's annulus, so camera jitter at a boundary doesn't thrash build/evict.
constexpr float kEvictSlack = 1.5f;
}

FarTerrainManager::FarTerrainManager(VkDevice device, VkPhysicalDevice physicalDevice)
    : m_device(device), m_physicalDevice(physicalDevice) {}

FarTerrainManager::~FarTerrainManager() {
    stopWorker();
    clearTiles();
    for (auto& [frames, tile] : m_graveyard) destroyTile(tile);
    m_graveyard.clear();
}

void FarTerrainManager::configure(const WorldGenerator& generator) {
    stopWorker();

    // Private copy: WorldGenerator::sampleColumn is non-const and runs on the worker —
    // the streaming generator must never be shared.
    auto gen = std::make_unique<WorldGenerator>(generator);
    FarMaterialResolver resolver = [](const std::string& material, int faceID) -> uint16_t {
        return Core::MaterialRegistry::instance().getTextureIndex(material, faceID);
    };
    m_mesher = std::make_unique<FarTerrainMesher>(std::move(gen), std::move(resolver));

    m_hasRefreshed = false;  // force a wanted-set refresh on the next update()
    if (m_params.threaded) {
        m_stopWorker = false;
        m_worker = std::thread([this] { workerLoop(); });
    }
    LOG_INFO("FarTerrain", "Configured far-terrain mesher (threaded={})", m_params.threaded);
}

void FarTerrainManager::stopWorker() {
    if (!m_worker.joinable()) return;
    m_stopWorker = true;
    m_requestCv.notify_all();
    m_worker.join();
    m_stopWorker = false;
}

void FarTerrainManager::workerLoop() {
    for (;;) {
        TileRequest req;
        {
            std::unique_lock<std::mutex> lock(m_requestMutex);
            m_requestCv.wait(lock, [this] { return m_stopWorker || !m_requests.empty(); });
            if (m_stopWorker) return;
            req = m_requests.front();
            m_requests.pop_front();
        }
        // An uncaught exception in a worker thread std::terminates the whole process
        // silently (no crash dump) — catch and log; the tile is simply re-requested on
        // a later refresh.
        try {
            FarTileMesh mesh = m_mesher->buildTile(req.key, req.step);
            std::lock_guard<std::mutex> lock(m_resultMutex);
            m_results.push_back(std::move(mesh));
        } catch (const std::exception& e) {
            LOG_ERROR("FarTerrain", "Tile worker exception at ring={} tile=({},{}): {}",
                      req.key.ring, req.key.x, req.key.z, e.what());
        } catch (...) {
            LOG_ERROR("FarTerrain", "Tile worker: unknown exception at ring={} tile=({},{})",
                      req.key.ring, req.key.x, req.key.z);
        }
    }
}

std::vector<FarTerrainManager::RingSpec> FarTerrainManager::computeRings() const {
    std::vector<RingSpec> rings;
    float start = 0.0f;
    for (size_t i = 0; i < m_params.ringSteps.size(); ++i) {
        RingSpec r;
        r.ring     = int(i) + 1;
        r.step     = std::max(1, m_params.ringSteps[i]);
        r.tileSize = FarTerrainMesher::kColumns * r.step;
        r.startR   = start;
        // Default band edges double per ring (512, 1024, 2048, ...); the last ring
        // always extends to maxDistance so the configured horizon is honored.
        float end = 512.0f * float(1u << i);
        if (i + 1 == m_params.ringSteps.size()) end = m_params.maxDistance;
        r.endR = std::min(std::max(end, start), m_params.maxDistance);
        rings.push_back(r);
        start = r.endR;
        if (start >= m_params.maxDistance) break;
    }
    return rings;
}

void FarTerrainManager::refreshWantedSet(const glm::vec3& cameraPos) {
    m_lastRefreshPos = glm::vec2(cameraPos.x, cameraPos.z);
    m_hasRefreshed = true;
    m_wanted.clear();
    m_keep.clear();

    const glm::vec2 cam = m_lastRefreshPos;
    std::vector<TileRequest> missing;

    for (const RingSpec& ring : computeRings()) {
        const float T = float(ring.tileSize);
        const float keepStart = std::max(0.0f, ring.startR - kEvictSlack * T);
        const float keepEnd   = ring.endR + kEvictSlack * T;
        const int tx0 = int(std::floor((cam.x - keepEnd) / T));
        const int tx1 = int(std::floor((cam.x + keepEnd) / T));
        const int tz0 = int(std::floor((cam.y - keepEnd) / T));
        const int tz1 = int(std::floor((cam.y + keepEnd) / T));
        for (int tz = tz0; tz <= tz1; ++tz) {
            for (int tx = tx0; tx <= tx1; ++tx) {
                const glm::vec2 center((tx + 0.5f) * T, (tz + 0.5f) * T);
                const float d = glm::length(center - cam);
                if (d >= keepEnd || d < keepStart) continue;
                FarTileKey key{ring.ring, tx, tz};
                m_keep.insert(key);
                // CROSS-RING OVERLAP: a coarser ring reaches one tile INWARD past its
                // inner boundary. At the handoff the coarser ring quantizes lower (bigger
                // step + larger down-bias), so with edge-to-edge annuli a grazing view
                // over a finer-ring crest at the boundary saw a sky slit before the
                // coarse shell rose again (user repro: holes tracking the camera at
                // ~ring-1/2 distance). Overlapping shells fill the slit; the per-ring
                // Y-bias ordering already resolves which wins where both exist.
                const float innerR = ring.ring > 1 ? ring.startR - T : ring.startR;
                if (d < innerR || d >= ring.endR) continue;
                // Skip tiles fully covered by loaded chunk columns — but ONLY well inside
                // the near field. The suppression exists so a surface-only tile doesn't
                // skin over player-dug holes/caves, which is an INTERIOR concern; at the
                // streaming frontier "every column has a loaded chunk" routinely lies
                // visually (chunks queued/unmeshed, vertical bands missing), and dropping
                // the tile there opened SKY HOLES through saddles at the seam. Frontier
                // tiles now always draw — the near field z-beats them wherever it really
                // has geometry (quantize-down + push-down + depth bias).
                if (m_chunkCoverage) {
                    const float tileFarDist = d + T * 0.7071f;  // farthest tile corner
                    const bool wellInterior =
                        m_nearFieldRadius > 0.0f && tileFarDist < m_nearFieldRadius - 32.0f;
                    if (wellInterior) {
                        glm::ivec2 minXZ(tx * ring.tileSize, tz * ring.tileSize);
                        glm::ivec2 maxXZ(minXZ.x + ring.tileSize, minXZ.y + ring.tileSize);
                        if (m_chunkCoverage(minXZ, maxXZ)) continue;
                    }
                }
                m_wanted.insert(key);
                if (!m_tiles.count(key)) missing.push_back({key, ring.step, d});
            }
        }
    }

    // Rebuild the request queue nearest-first; drop superseded requests wholesale.
    std::sort(missing.begin(), missing.end(),
              [](const TileRequest& a, const TileRequest& b) { return a.dist < b.dist; });
    m_pending.clear();
    for (const auto& r : missing) m_pending.insert(r.key);
    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        m_requests.assign(missing.begin(), missing.end());
    }
    m_requestCv.notify_all();
}

void FarTerrainManager::drainResults() {
    std::vector<FarTileMesh> ready;
    {
        std::lock_guard<std::mutex> lock(m_resultMutex);
        if (m_results.empty()) return;
        const size_t n = std::min(m_results.size(), size_t(std::max(1, m_params.uploadBudgetPerFrame)));
        ready.assign(std::make_move_iterator(m_results.begin()),
                     std::make_move_iterator(m_results.begin() + n));
        m_results.erase(m_results.begin(), m_results.begin() + n);
    }

    bool changed = false;
    for (FarTileMesh& mesh : ready) {
        // originXZ is exactly key * tileSize (see buildTile), so integer division
        // reconstructs the key exactly, negatives included.
        const int ts = std::max(1, mesh.tileSize);
        FarTileKey key{mesh.ring, mesh.originXZ.x / ts, mesh.originXZ.y / ts};
        m_pending.erase(key);
        if (!m_wanted.count(key)) continue;          // superseded while meshing
        if (m_tiles.count(key)) continue;            // already resident
        if (mesh.vertices.empty()) continue;
        if (uploadTile(key, mesh)) changed = true;
    }
    if (changed) rebuildDrawList();
}

void FarTerrainManager::evictTiles(const glm::vec3& cameraPos) {
    bool changed = false;
    for (auto it = m_tiles.begin(); it != m_tiles.end();) {
        if (!m_keep.count(it->first)) {
            retireTile(it->second);
            it = m_tiles.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }

    // LRU cap: evict the farthest non-wanted tiles first if we're over budget.
    if (int(m_tiles.size()) > m_params.maxResidentTiles) {
        const glm::vec2 cam(cameraPos.x, cameraPos.z);
        std::vector<std::pair<float, FarTileKey>> byDist;
        byDist.reserve(m_tiles.size());
        for (const auto& [key, tile] : m_tiles) {
            glm::vec2 center = tile.origin + glm::vec2((tile.aabbMax.x - tile.aabbMin.x) * 0.5f,
                                                       (tile.aabbMax.z - tile.aabbMin.z) * 0.5f);
            byDist.push_back({glm::length(center - cam), key});
        }
        std::sort(byDist.begin(), byDist.end(),
                  [](const auto& a, const auto& b) { return a.first > b.first; });
        for (const auto& [d, key] : byDist) {
            if (int(m_tiles.size()) <= m_params.maxResidentTiles) break;
            if (m_wanted.count(key)) continue;  // never LRU-evict a wanted tile
            auto it = m_tiles.find(key);
            if (it != m_tiles.end()) {
                retireTile(it->second);
                m_tiles.erase(it);
                changed = true;
            }
        }
        if (int(m_tiles.size()) > m_params.maxResidentTiles) {
            LOG_WARN("FarTerrain", "Wanted tile set ({}) exceeds maxResidentTiles ({})",
                     m_tiles.size(), m_params.maxResidentTiles);
        }
    }

    if (changed) rebuildDrawList();
}

void FarTerrainManager::tickGraveyard() {
    for (auto it = m_graveyard.begin(); it != m_graveyard.end();) {
        if (--it->first <= 0) {
            destroyTile(it->second);
            it = m_graveyard.erase(it);
        } else {
            ++it;
        }
    }
}

void FarTerrainManager::update(const glm::vec3& cameraPos) {
    tickGraveyard();
    if (!m_mesher) return;

    if (!m_params.enabled) {
        // Disabled: stop feeding the worker; keep resident tiles (cheap, redraw-ready).
        if (m_wasEnabled) {
            std::lock_guard<std::mutex> lock(m_requestMutex);
            m_requests.clear();
            m_pending.clear();
            m_wasEnabled = false;
        }
        return;
    }
    m_wasEnabled = true;

    const glm::vec2 camXZ(cameraPos.x, cameraPos.z);
    if (!m_hasRefreshed || glm::length(camXZ - m_lastRefreshPos) > kRefreshDistance) {
        refreshWantedSet(cameraPos);
        evictTiles(cameraPos);
    }

    if (!m_params.threaded) {
        // Fallback mode: build ONE tile per frame synchronously (bisect aid).
        TileRequest req{};
        bool has = false;
        {
            std::lock_guard<std::mutex> lock(m_requestMutex);
            if (!m_requests.empty()) { req = m_requests.front(); m_requests.pop_front(); has = true; }
        }
        if (has) {
            FarTileMesh mesh = m_mesher->buildTile(req.key, req.step);
            std::lock_guard<std::mutex> lock(m_resultMutex);
            m_results.push_back(std::move(mesh));
        }
    }

    drainResults();
}

size_t FarTerrainManager::debugBuildTile(const glm::vec3& worldPos, int step) {
    if (!m_mesher || step <= 0) return 0;
    if (m_worker.joinable()) {
        // The worker owns the mesher while running; route the debug build through it.
        const int tileSize = FarTerrainMesher::kColumns * step;
        FarTileKey key{1, int(std::floor(worldPos.x / float(tileSize))),
                       int(std::floor(worldPos.z / float(tileSize)))};
        m_wanted.insert(key);
        m_pending.insert(key);
        {
            std::lock_guard<std::mutex> lock(m_requestMutex);
            m_requests.push_front({key, step, 0.0f});
        }
        m_requestCv.notify_all();
        return FarTerrainMesher::kColumns * FarTerrainMesher::kColumns;  // built async
    }

    const int tileSize = FarTerrainMesher::kColumns * step;
    FarTileKey key{1, int(std::floor(worldPos.x / float(tileSize))),
                   int(std::floor(worldPos.z / float(tileSize)))};
    FarTileMesh mesh = m_mesher->buildTile(key, step);
    if (mesh.vertices.empty()) return 0;
    auto it = m_tiles.find(key);
    if (it != m_tiles.end()) {
        retireTile(it->second);
        m_tiles.erase(it);
    }
    m_wanted.insert(key);
    m_keep.insert(key);
    if (!uploadTile(key, mesh)) return 0;
    rebuildDrawList();
    LOG_INFO("FarTerrain", "Debug tile built: tile=({},{}) step={} verts={}",
             key.x, key.z, step, mesh.vertices.size());
    return mesh.vertices.size();
}

bool FarTerrainManager::uploadTile(const FarTileKey& key, const FarTileMesh& mesh) {
    GpuTile tile;
    tile.indexCount = uint32_t(mesh.indices.size());
    tile.origin     = glm::vec2(float(mesh.originXZ.x), float(mesh.originXZ.y));
    tile.aabbMin    = glm::vec3(float(mesh.originXZ.x), mesh.minY, float(mesh.originXZ.y));
    tile.aabbMax    = glm::vec3(float(mesh.originXZ.x + mesh.tileSize), mesh.maxY,
                                float(mesh.originXZ.y + mesh.tileSize));

    if (!createHostBuffer(mesh.vertices.size() * sizeof(FarVertex),
                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mesh.vertices.data(),
                          tile.vertexBuffer, tile.vertexMemory) ||
        !createHostBuffer(mesh.indices.size() * sizeof(uint32_t),
                          VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mesh.indices.data(),
                          tile.indexBuffer, tile.indexMemory)) {
        destroyTile(tile);
        LOG_ERROR("FarTerrain", "Failed to allocate GPU buffers for tile ({},{})", key.x, key.z);
        return false;
    }

    m_tiles.emplace(key, tile);
    return true;
}

void FarTerrainManager::retireTile(GpuTile& tile) {
    m_graveyard.push_back({kGraveyardFrames, tile});
    tile = GpuTile{};
}

void FarTerrainManager::destroyTile(GpuTile& tile) {
    if (tile.vertexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(m_device, tile.vertexBuffer, nullptr);
    if (tile.vertexMemory != VK_NULL_HANDLE) vkFreeMemory(m_device, tile.vertexMemory, nullptr);
    if (tile.indexBuffer  != VK_NULL_HANDLE) vkDestroyBuffer(m_device, tile.indexBuffer, nullptr);
    if (tile.indexMemory  != VK_NULL_HANDLE) vkFreeMemory(m_device, tile.indexMemory, nullptr);
    tile = GpuTile{};
}

void FarTerrainManager::clearTiles() {
    for (auto& [key, tile] : m_tiles) destroyTile(tile);
    m_tiles.clear();
    m_draws.clear();
}

void FarTerrainManager::rebuildDrawList() {
    m_draws.clear();
    m_draws.reserve(m_tiles.size());
    for (const auto& [key, tile] : m_tiles) {
        TileDraw d;
        d.draw.vertexBuffer = tile.vertexBuffer;
        d.draw.indexBuffer  = tile.indexBuffer;
        d.draw.indexCount   = tile.indexCount;
        d.draw.origin       = tile.origin;
        d.aabbMin           = tile.aabbMin;
        d.aabbMax           = tile.aabbMax;
        m_draws.push_back(d);
    }
}

bool FarTerrainManager::createHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                         const void* data, VkBuffer& buffer, VkDeviceMemory& memory) {
    if (size == 0) return false;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size        = size;
    bufferInfo.usage       = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) return false;

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(m_device, buffer, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(
        memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        vkDestroyBuffer(m_device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        return false;
    }
    vkBindBufferMemory(m_device, buffer, memory, 0);

    void* mapped = nullptr;
    if (vkMapMemory(m_device, memory, 0, size, 0, &mapped) != VK_SUCCESS) {
        vkDestroyBuffer(m_device, buffer, nullptr);
        vkFreeMemory(m_device, memory, nullptr);
        buffer = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
        return false;
    }
    std::memcpy(mapped, data, size_t(size));
    vkUnmapMemory(m_device, memory);
    return true;
}

uint32_t FarTerrainManager::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    LOG_ERROR("FarTerrain", "No suitable memory type for far-terrain buffers");
    return 0;
}

} // namespace Graphics
} // namespace Phyxel
