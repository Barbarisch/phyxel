#include "core/water/WaterDroplets.h"

namespace Phyxel::Core::Water {

std::vector<DetachedRun> findDetachedRuns(const glm::ivec3& d, const float* f, const float* s, float film) {
    std::vector<DetachedRun> out;
    auto idx = [&](int x, int y, int z) { return static_cast<size_t>(x) + static_cast<size_t>(d.x) * (static_cast<size_t>(y) + static_cast<size_t>(d.y) * z); };
    auto air = [&](int x, int y, int z) {
        if (x < 0 || y < 0 || z < 0 || x >= d.x || y >= d.y || z >= d.z) return false;   // outside the grid is solid
        const size_t i = idx(x, y, z);
        return f[i] + (s ? s[i] : 0.0f) < film;
    };
    for (int z = 0; z < d.z; ++z) for (int x = 0; x < d.x; ++x) {
        for (int y = 1; y < d.y; ++y) {
            if (f[idx(x, y, z)] < film || !air(x, y - 1, z)) continue;
            DetachedRun r; r.bottom = {x, y, z};
            int top = y;
            while (top < d.y && f[idx(x, top, z)] >= film) { r.sumF += f[idx(x, top, z)]; ++top; }
            r.cells = top - y;
            bool iso = top >= d.y || air(x, top, z);
            for (int yy = y; yy < top && iso; ++yy)
                iso = air(x - 1, yy, z) && air(x + 1, yy, z) && air(x, yy, z - 1) && air(x, yy, z + 1);
            r.isolated = iso;
            out.push_back(r);
            y = top;
        }
    }
    return out;
}

}  // namespace Phyxel::Core::Water
