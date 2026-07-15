#pragma once

#include <glm/glm.hpp>
#include <functional>
#include <map>
#include <string>
#include <tuple>

namespace Phyxel {
namespace Core {

// ============================================================================
// ChopManager — pure per-tree chop-progress bookkeeping for axe felling.
//
// SCOPE (deliberate seam): this system accumulates chop damage against a tree,
// identified by its trunk-base cube, and fires onTreeFelled EXACTLY ONCE when a
// tree's accumulated chop power crosses its hardness. It changes NO voxels.
// The topple/fall physics + gatherable-log drop is owned by the destruction
// session, which consumes the onTreeFelled event (or the ChopManager callback)
// to spawn coherent falling fragments. Keep it that way — this class must stay
// free of ChunkManager / rendering / physics dependencies so it is unit-testable
// in isolation. The host (Application) does voxel identification and calls
// addChop(); this class only tracks numbers.
// ============================================================================

struct TreeFellEvent {
    glm::ivec3  base{0};          // trunk-base cube (world coords)
    std::string material;         // trunk material at contact (e.g. "Log")
    int         trunkHeight = 0;  // contiguous trunk voxels above base at first contact
    float       totalChop = 0.0f; // accumulated chop power when it fell
};

class ChopManager {
public:
    struct ChopResult {
        float      progress = 0.0f;  // 0..1 toward felling
        bool       felled   = false; // true only on the swing that crosses the threshold
        bool       alreadyFelled = false; // tree was already felled by an earlier swing
        glm::ivec3 base{0};
    };

    // Register one axe bite against the tree whose trunk base is `base`.
    //   material     — trunk material at contact (recorded for the fell event)
    //   trunkHeight  — contiguous trunk voxel count (for the fell event)
    //   chopPower    — power delivered by this swing (e.g. the axe's damage)
    //   hardness     — total chop power required to fell (caller derives it,
    //                  typically from the trunk height); clamped to >= 1.
    // The first call for a given base fixes that tree's hardness/material/height.
    ChopResult addChop(const glm::ivec3& base, const std::string& material,
                       int trunkHeight, float chopPower, float hardness);

    // Fired once per tree, the moment it is felled. Never re-fires for the same
    // tree unless it is forgotten and chopped anew.
    void setOnTreeFelled(std::function<void(const TreeFellEvent&)> cb) {
        m_onFelled = std::move(cb);
    }

    // 0..1 progress for a tree (0 if unknown; 1 if felled).
    float progressAt(const glm::ivec3& base) const;

    // True once a tree at `base` has been felled (until forgotten).
    bool isFelled(const glm::ivec3& base) const;

    // Drop a tree's state — call after the destruction session consumes the
    // fell event and the tree is gone, so the same site can grow+be chopped
    // again without stale progress.
    void forget(const glm::ivec3& base);

    // Wipe all state (scene change / world reload).
    void clear();

    // Number of trees currently being tracked (chopped but not forgotten).
    size_t trackedCount() const { return m_trees.size(); }

private:
    using Key = std::tuple<int, int, int>;
    static Key keyOf(const glm::ivec3& b) { return {b.x, b.y, b.z}; }

    struct TreeState {
        float       accumulated = 0.0f;
        float       hardness    = 1.0f;
        int         trunkHeight = 0;
        std::string material;
        bool        felled      = false;
    };

    std::map<Key, TreeState> m_trees;
    std::function<void(const TreeFellEvent&)> m_onFelled;
};

} // namespace Core
} // namespace Phyxel
