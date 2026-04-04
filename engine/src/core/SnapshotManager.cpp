#include "core/SnapshotManager.h"
#include <algorithm>

namespace Phyxel {
namespace Core {

// ============================================================================
// Named Snapshots
// ============================================================================

bool SnapshotManager::addSnapshot(const RegionSnapshot& snapshot) {
    if (snapshot.name.empty()) return false;

    std::lock_guard<std::mutex> lock(m_mutex);

    // Reject duplicate names
    if (m_snapshots.count(snapshot.name)) return false;

    // Evict oldest if at capacity
    while (m_snapshots.size() >= MAX_SNAPSHOTS && !m_insertOrder.empty()) {
        m_snapshots.erase(m_insertOrder.front());
        m_insertOrder.erase(m_insertOrder.begin());
    }

    m_snapshots[snapshot.name] = snapshot;
    m_insertOrder.push_back(snapshot.name);
    return true;
}

const RegionSnapshot* SnapshotManager::getSnapshot(const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_snapshots.find(name);
    if (it == m_snapshots.end()) return nullptr;
    return &it->second;
}

bool SnapshotManager::deleteSnapshot(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_snapshots.find(name);
    if (it == m_snapshots.end()) return false;
    m_snapshots.erase(it);
    m_insertOrder.erase(
        std::remove(m_insertOrder.begin(), m_insertOrder.end(), name),
        m_insertOrder.end());
    return true;
}

std::vector<RegionSnapshot> SnapshotManager::listSnapshots() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<RegionSnapshot> result;
    result.reserve(m_snapshots.size());
    for (const auto& [name, snap] : m_snapshots) {
        // Return metadata only (skip voxel data for listing)
        RegionSnapshot meta;
        meta.name = snap.name;
        meta.min = snap.min;
        meta.max = snap.max;
        meta.size = snap.size;
        meta.totalVolume = snap.totalVolume;
        meta.createdAt = snap.createdAt;
        // Include voxel count but not voxel data
        meta.voxels.resize(0); // empty
        result.push_back(std::move(meta));
    }
    return result;
}

size_t SnapshotManager::snapshotCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_snapshots.size();
}

// ============================================================================
// Clipboard
// ============================================================================

void SnapshotManager::setClipboard(const RegionSnapshot& data) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_clipboard = data;
    m_hasClipboard = true;
}

const RegionSnapshot* SnapshotManager::getClipboard() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_hasClipboard) return nullptr;
    return &m_clipboard;
}

void SnapshotManager::clearClipboard() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_clipboard = RegionSnapshot{};
    m_hasClipboard = false;
}

bool SnapshotManager::hasClipboard() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_hasClipboard;
}

// ============================================================================
// Utility
// ============================================================================

void SnapshotManager::rotateY90(RegionSnapshot& snapshot) {
    // Rotate 90° clockwise around Y axis:
    //   new_x = (size.z - 1) - old_z
    //   new_y = old_y
    //   new_z = old_x
    // Dimensions swap: new_size = (old_size.z, old_size.y, old_size.x)
    glm::ivec3 oldSize = snapshot.size;
    for (auto& v : snapshot.voxels) {
        int newX = (oldSize.z - 1) - v.offset.z;
        int newZ = v.offset.x;
        v.offset.x = newX;
        v.offset.z = newZ;
        // y unchanged

        // Rotate subcube local position within 3x3 grid (90° CW around Y)
        if (v.level >= SnapshotVoxelLevel::Subcube) {
            int newSX = (2) - v.subcubePos.z;
            int newSZ = v.subcubePos.x;
            v.subcubePos.x = newSX;
            v.subcubePos.z = newSZ;
        }
        // Rotate microcube local position within 3x3 grid (90° CW around Y)
        if (v.level == SnapshotVoxelLevel::Microcube) {
            int newMX = (2) - v.microcubePos.z;
            int newMZ = v.microcubePos.x;
            v.microcubePos.x = newMX;
            v.microcubePos.z = newMZ;
        }
    }
    snapshot.size = glm::ivec3(oldSize.z, oldSize.y, oldSize.x);
}

// ============================================================================
// Undo / Redo Stack
// ============================================================================

void SnapshotManager::pushUndo(RegionSnapshot&& snapshot) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_redoStack.clear();  // any new action invalidates redo history
    m_undoStack.push_back(std::move(snapshot));
    while (m_undoStack.size() > MAX_UNDO_DEPTH) {
        m_undoStack.pop_front();
    }
}

void SnapshotManager::pushUndoOnly(RegionSnapshot&& snapshot) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_undoStack.push_back(std::move(snapshot));
    while (m_undoStack.size() > MAX_UNDO_DEPTH) {
        m_undoStack.pop_front();
    }
}

void SnapshotManager::pushRedo(RegionSnapshot&& snapshot) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_redoStack.push_back(std::move(snapshot));
    while (m_redoStack.size() > MAX_UNDO_DEPTH) {
        m_redoStack.pop_front();
    }
}

const RegionSnapshot* SnapshotManager::peekUndo() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_undoStack.empty()) return nullptr;
    return &m_undoStack.back();
}

RegionSnapshot SnapshotManager::popUndo() {
    std::lock_guard<std::mutex> lock(m_mutex);
    RegionSnapshot snap = std::move(m_undoStack.back());
    m_undoStack.pop_back();
    return snap;
}

RegionSnapshot SnapshotManager::popRedo() {
    std::lock_guard<std::mutex> lock(m_mutex);
    RegionSnapshot snap = std::move(m_redoStack.back());
    m_redoStack.pop_back();
    return snap;
}

bool SnapshotManager::canUndo() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_undoStack.empty();
}

bool SnapshotManager::canRedo() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_redoStack.empty();
}

size_t SnapshotManager::undoDepth() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_undoStack.size();
}

size_t SnapshotManager::redoDepth() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_redoStack.size();
}

void SnapshotManager::clearUndoRedo() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_undoStack.clear();
    m_redoStack.clear();
}

std::vector<SnapshotManager::UndoEntry> SnapshotManager::listUndoStack() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<UndoEntry> result;
    result.reserve(m_undoStack.size());
    for (const auto& s : m_undoStack) {
        result.push_back({s.name, s.min, s.max, s.voxels.size()});
    }
    return result;
}

std::vector<SnapshotManager::UndoEntry> SnapshotManager::listRedoStack() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<UndoEntry> result;
    result.reserve(m_redoStack.size());
    for (const auto& s : m_redoStack) {
        result.push_back({s.name, s.min, s.max, s.voxels.size()});
    }
    return result;
}

} // namespace Core
} // namespace Phyxel
