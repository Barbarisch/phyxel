#pragma once

#include "Types.h"
#include <glm/glm.hpp>
#include <atomic>
#include <cstddef>
#include <functional>
#include <string>

namespace Phyxel {
    class WorldStorage;   // Forward declaration
}

namespace Phyxel {

// Hash functions for glm integer vectors used as unordered_map keys (chunk coordinates).
struct ChunkCoordHash2D {
    std::size_t operator()(const glm::ivec2& coord) const {
        return std::hash<int>()(coord.x) ^ (std::hash<int>()(coord.y) << 1);
    }
};

struct ChunkCoordHash {
    std::size_t operator()(const glm::ivec3& coord) const {
        // Combine X, Y, Z coordinates into a single hash
        // Using prime numbers to reduce hash collisions
        return std::hash<int>()(coord.x) ^
               (std::hash<int>()(coord.y) << 1) ^
               (std::hash<int>()(coord.z) << 2);
    }
};

/**
 * ChunkStreamingManager -- what is left after docs/VoxelFoundationPlan.md D4 deleted today's chunk store.
 *
 * The world is the switched 1/8 m world (voxfound/); no Chunk object exists. This class keeps only what the
 * switched world calls: the world database (WorldStorage open / close / access -- the switched world's own
 * tables and the world meta live in it) and the "today's world is disabled" flag the boot sets.
 */
class ChunkStreamingManager {
public:
    /// docs/VoxelFoundationPlan.md step 7 stage D2: TODAY'S WORLD NEVER LOADS. Since D4 there is no today's
    /// store at all; the flag is kept because the boot and the debug routes still read it.
    static void setTodayWorldDisabled(bool on) { s_todayDisabled.store(on); }
    static bool todayWorldDisabled() { return s_todayDisabled.load(); }
    static inline std::atomic<bool> s_todayDisabled{false};

    ChunkStreamingManager();
    ~ChunkStreamingManager();
    ChunkStreamingManager(const ChunkStreamingManager&) = delete;
    ChunkStreamingManager& operator=(const ChunkStreamingManager&) = delete;

    // World storage management
    bool initializeWorldStorage(const std::string& worldPath);
    void disconnectWorldStorage();
    WorldStorage* getWorldStorage() const { return worldStorage; }

    /// Loading observability (/api/debug/load_state): no chunk generation or deferred DB loads exist since D4.
    size_t pendingGenerationCount() const { return 0; }
    bool hasDeferredDbLoads() const { return false; }

private:
    WorldStorage* worldStorage = nullptr;
};

} // namespace Phyxel
