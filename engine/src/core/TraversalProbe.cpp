#include "core/TraversalProbe.h"

#include <climits>
#include <deque>
#include <unordered_set>

namespace Phyxel {
namespace Core {

namespace {
// Footprint sample columns: centre + 4 corners (a rough rectangle).
inline void footprint(int fx, int fz, int hw, int xs[5], int zs[5]) {
    xs[0] = fx;      zs[0] = fz;
    xs[1] = fx - hw; zs[1] = fz - hw;
    xs[2] = fx + hw; zs[2] = fz - hw;
    xs[3] = fx - hw; zs[3] = fz + hw;
    xs[4] = fx + hw; zs[4] = fz + hw;
}
}  // namespace

bool TraversalProbe::fits(int fx, int fy, int fz) const {
    const int hw = m_box.halfWidthMicro, h = m_box.heightMicro;
    int xs[5], zs[5];
    footprint(fx, fz, hw, xs, zs);
    for (int i = 0; i < 5; ++i)
        for (int y = fy; y < fy + h; ++y)
            if (m_occ(xs[i], y, zs[i])) return false;   // body or head hits solid
    return true;
}

bool TraversalProbe::supported(int fx, int fy, int fz) const {
    const int hw = m_box.halfWidthMicro;
    int xs[5], zs[5];
    footprint(fx, fz, hw, xs, zs);
    for (int i = 0; i < 5; ++i)
        if (m_occ(xs[i], fy - 1, zs[i])) return true;   // something under the feet
    return false;
}

int TraversalProbe::settle(int fx, int fy, int fz, int minY) const {
    int y = fy;
    if (!fits(fx, y, fz)) return INT_MIN;
    while (y >= minY) {
        if (supported(fx, y, fz)) return y;             // resting on a surface
        if (!fits(fx, y - 1, fz)) return y;             // blocked below (can't sink further)
        --y;                                            // free-fall one micro
    }
    return INT_MIN;                                     // fell out of bounds
}

bool TraversalProbe::reachable(glm::ivec3 start, glm::ivec3 goalLo, glm::ivec3 goalHi,
                               glm::ivec3 boundLo, glm::ivec3 boundHi) const {
    auto inGoal = [&](const glm::ivec3& p) {
        return p.x >= goalLo.x && p.x <= goalHi.x && p.y >= goalLo.y && p.y <= goalHi.y &&
               p.z >= goalLo.z && p.z <= goalHi.z;
    };
    auto inBounds = [&](int x, int y, int z) {
        return x >= boundLo.x && x <= boundHi.x && y >= boundLo.y && y <= boundHi.y &&
               z >= boundLo.z && z <= boundHi.z;
    };
    auto key = [](int x, int y, int z) {
        return ((long long)(x + 32768) << 34) | ((long long)(y + 32768) << 17) | (long long)(z + 32768);
    };

    int sy = settle(start.x, start.y, start.z, boundLo.y);
    if (sy == INT_MIN) return false;
    glm::ivec3 s(start.x, sy, start.z);

    std::deque<glm::ivec3> q;
    std::unordered_set<long long> seen;
    q.push_back(s);
    seen.insert(key(s.x, s.y, s.z));

    static const int dx[4] = {1, -1, 0, 0};
    static const int dz[4] = {0, 0, 1, -1};
    const int maxVisited = 4000000;   // safety cap

    while (!q.empty()) {
        glm::ivec3 c = q.front();
        q.pop_front();
        if (inGoal(c)) return true;
        if ((int)seen.size() > maxVisited) break;

        for (int d = 0; d < 4; ++d) {
            const int nx = c.x + dx[d], nz = c.z + dz[d];
            // step at the current level if it fits, else auto step-up to maxStepUp.
            int placedY = INT_MIN;
            for (int h = 0; h <= m_box.maxStepUpMicro; ++h) {
                const int ty = c.y + h;
                if (!inBounds(nx, ty, nz)) break;
                if (fits(nx, ty, nz)) { placedY = settle(nx, ty, nz, boundLo.y); break; }
            }
            if (placedY == INT_MIN || !inBounds(nx, placedY, nz)) continue;
            const long long k = key(nx, placedY, nz);
            if (seen.insert(k).second) q.push_back(glm::ivec3(nx, placedY, nz));
        }
    }
    return false;
}

}  // namespace Core
}  // namespace Phyxel
