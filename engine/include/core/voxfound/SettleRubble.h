#pragma once
// docs/VoxelFoundationPlan.md F8.2 -- rubble settled into WHOLE voxels (a world rig's end): per material, `units[m] / 256`
// voxels (a whole number by construction; the remainder reported) go to that material's candidate cells, fullest
// first, then to the nearest open cells by a flood that passes through solids too (an enclosed pocket must not strand
// what is owed) but fills only open ones (never overwrites a solid). The decisions come back as cell -> material.
// Header-only (VoxFoundSettleRubble tests); the editor's voxFoundSettleRubble feeds it from the fluid's exact ledger.

#include <algorithm>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "core/voxfound/VoxelGrid.h"

namespace Phyxel::Core::VoxFound {

struct SettleStats {
    size_t placed = 0, flooded = 0, unplaced = 0, remainderUnits = 0;
};

/// units: per material, the units to settle; cand: per material, candidate cell index -> its units. `w.mat` is read for
/// solidity and written with the placed voxels; every decision is also in `write` (cell index -> material).
inline SettleStats settleIntoWholeVoxels(VoxelGrid& w, const std::map<uint8_t, uint64_t>& units,
                                         const std::map<uint8_t, std::map<size_t, uint64_t>>& cand,
                                         std::map<size_t, uint8_t>& write) {
    SettleStats st;
    std::vector<char> taken(w.size(), 0);
    static const glm::ivec3 N6[6] = {{0, -1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, 1, 0}};
    for (const auto& [m, u] : units) {
        size_t n = size_t(u / 256);
        st.remainderUnits += size_t(u % 256);
        std::vector<std::pair<uint64_t, size_t>> order;
        auto it = cand.find(m);
        if (it != cand.end())
            for (const auto& [i, uu] : it->second) order.push_back({uu, i});
        std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        auto put = [&](size_t i) {
            w.mat[i] = m;
            taken[i] = 1;
            write[i] = m;
            ++st.placed;
            --n;
        };
        for (const auto& [uu, i] : order) {
            if (!n) break;
            if (!w.mat[i] && !taken[i]) put(i);
        }
        if (n) {
            std::vector<char> seen(w.size(), 0);
            std::vector<size_t> frontier;
            for (const auto& [uu, i] : order) frontier.push_back(i), seen[i] = 1;
            if (frontier.empty()) {   // no candidate: the surface at the centre
                glm::ivec3 c(w.dim.x / 2, w.dim.y - 1, w.dim.z / 2);
                while (c.y > 0 && !w.mat[w.idx(c - glm::ivec3(0, 1, 0))]) --c.y;
                frontier.push_back(w.idx(c)), seen[frontier[0]] = 1;
                if (!w.mat[frontier[0]] && !taken[frontier[0]] && n) put(frontier[0]), ++st.flooded;
            }
            for (size_t h = 0; h < frontier.size() && n; ++h) {
                const glm::ivec3 v = w.pos(frontier[h]);
                for (const auto& d : N6) {
                    const glm::ivec3 q = v + d;
                    if (!w.in(q)) continue;
                    const size_t qi = w.idx(q);
                    if (seen[qi]) continue;
                    seen[qi] = 1;
                    frontier.push_back(qi);
                    if (!taken[qi] && !w.mat[qi] && n) put(qi), ++st.flooded;
                }
            }
        }
        st.unplaced += n;
    }
    return st;
}

}  // namespace Phyxel::Core::VoxFound
