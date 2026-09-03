#include "core/FloraSweep.h"

#include <algorithm>
#include <queue>
#include <set>
#include <tuple>

namespace Phyxel {
namespace Core {

std::vector<glm::ivec3> planOrphanedFloraSweep(
    const SweepBounds& bounds,
    const std::function<bool(const glm::ivec3&)>& isFlora,
    const std::function<bool(const glm::ivec3&)>& isSolid) {
    std::vector<glm::ivec3> orphans;
    if (!isFlora || !isSolid) return orphans;
    if (bounds.max.x < bounds.min.x || bounds.max.y < bounds.min.y ||
        bounds.max.z < bounds.min.z)
        return orphans;

    using Key = std::tuple<int, int, int>;
    auto key = [](const glm::ivec3& p) { return Key{p.x, p.y, p.z}; };
    std::set<Key> visited;
    static const glm::ivec3 kNeigh[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
                                         {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

    // Scan order (x, y, z) makes both the component seeds and the emitted list
    // deterministic regardless of how the caller's probes are implemented.
    for (int x = bounds.min.x; x <= bounds.max.x; ++x)
        for (int y = bounds.min.y; y <= bounds.max.y; ++y)
            for (int z = bounds.min.z; z <= bounds.max.z; ++z) {
                const glm::ivec3 seed(x, y, z);
                if (visited.count(key(seed))) continue;
                if (!isFlora(seed)) continue;

                // Flood this component of tree matter, deciding as we go whether it
                // still reaches support. We must walk the WHOLE component before
                // judging: one supported cell anywhere holds the whole canopy up.
                std::vector<glm::ivec3> component;
                std::queue<glm::ivec3> q;
                q.push(seed);
                visited.insert(key(seed));
                bool supported = false;
                bool leavesBox = false;

                while (!q.empty()) {
                    const glm::ivec3 c = q.front();
                    q.pop();
                    component.push_back(c);
                    // Support = standing on something that is NOT part of the tree:
                    // ground, or a structure (a canopy lying on a roof is held up).
                    const glm::ivec3 below(c.x, c.y - 1, c.z);
                    if (!isFlora(below) && isSolid(below)) supported = true;
                    // A component touching the shell may continue past it unseen —
                    // its support is UNKNOWN, and unknown is never swept.
                    if (bounds.onBoundary(c)) leavesBox = true;

                    for (const auto& d : kNeigh) {
                        const glm::ivec3 n = c + d;
                        if (!bounds.contains(n)) continue;   // shell handled by leavesBox
                        if (visited.count(key(n))) continue;
                        if (!isFlora(n)) continue;
                        visited.insert(key(n));
                        q.push(n);
                    }
                }

                if (supported || leavesBox) continue;
                orphans.insert(orphans.end(), component.begin(), component.end());
            }

    std::sort(orphans.begin(), orphans.end(), [](const glm::ivec3& a, const glm::ivec3& b) {
        if (a.x != b.x) return a.x < b.x;
        if (a.y != b.y) return a.y < b.y;
        return a.z < b.z;
    });
    return orphans;
}

} // namespace Core
} // namespace Phyxel
