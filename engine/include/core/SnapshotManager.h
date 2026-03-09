#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <glm/glm.hpp>

namespace Phyxel {
namespace Core {

// ============================================================================
// SnapshotManager
//
// Provides named region snapshots for undo/redo and a clipboard for
// copy/paste operations.  All methods are thread-safe.
//
// Usage from API:
//   create_snapshot(name, min, max)  — capture every voxel in the box
//   restore_snapshot(name)           — clear the region then replace voxels
//   delete_snapshot(name)            — free memory
//   list_snapshots()                 — enumerate all stored snapshots
//   copy_region(min, max)            — copy region into clipboard
//   paste_region(position, [rot])    — paste clipboard at new origin
// ============================================================================

/// Single voxel entry in a snapshot (position is relative to the region origin).
struct VoxelEntry {
    glm::ivec3 offset;       // relative to region min corner
    std::string material;    // material name (case-sensitive)
};

/// Snapshot of a voxel region.
struct RegionSnapshot {
    std::string name;
    glm::ivec3 min;           // world-space min corner
    glm::ivec3 max;           // world-space max corner
    glm::ivec3 size;          // max - min + 1
    std::vector<VoxelEntry> voxels;   // only occupied voxels (sparse)
    int64_t totalVolume;      // size.x * size.y * size.z
    std::chrono::system_clock::time_point createdAt;
};

class SnapshotManager {
public:
    /// Maximum number of named snapshots (oldest auto-evicted if exceeded).
    static constexpr size_t MAX_SNAPSHOTS = 50;

    /// Maximum region volume for a single snapshot (100k voxels).
    static constexpr int64_t MAX_VOLUME = 100000;

    // ------------------------------------------------------------------
    // Named Snapshots (undo)
    // ------------------------------------------------------------------

    /// Store a new snapshot.  Returns false if name is empty or already exists.
    bool addSnapshot(const RegionSnapshot& snapshot);

    /// Retrieve a snapshot by name.  Returns nullptr if not found.
    const RegionSnapshot* getSnapshot(const std::string& name) const;

    /// Delete a snapshot.  Returns false if not found.
    bool deleteSnapshot(const std::string& name);

    /// List all snapshot names with metadata (no voxel data).
    std::vector<RegionSnapshot> listSnapshots() const;

    /// Number of stored snapshots.
    size_t snapshotCount() const;

    // ------------------------------------------------------------------
    // Clipboard (copy / paste)
    // ------------------------------------------------------------------

    /// Store voxels into the clipboard (replaces any previous clipboard).
    void setClipboard(const RegionSnapshot& data);

    /// Get the current clipboard contents.  Returns nullptr if empty.
    const RegionSnapshot* getClipboard() const;

    /// Clear the clipboard.
    void clearClipboard();

    /// Check if clipboard has data.
    bool hasClipboard() const;

    // ------------------------------------------------------------------
    // Utility
    // ------------------------------------------------------------------

    /// Rotate voxel offsets 90° clockwise around the Y axis (in-place).
    static void rotateY90(RegionSnapshot& snapshot);

private:
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, RegionSnapshot> m_snapshots;
    std::vector<std::string> m_insertOrder;  // for LRU eviction

    RegionSnapshot m_clipboard;
    bool m_hasClipboard = false;
};

} // namespace Core
} // namespace Phyxel
