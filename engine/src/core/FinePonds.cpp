#include "core/FinePonds.h"

#include <algorithm>
#include <cmath>
#include <queue>

namespace Phyxel {

std::vector<FinePond> discoverFinePonds(const std::function<float(int, int)>& heightAt,
                                        int w, int h) {
    std::vector<FinePond> out;
    if (w < 3 || h < 3) return out;

    const size_t n = static_cast<size_t>(w) * h;
    std::vector<float> ht(n), fill(n);
    for (int z = 0; z < h; ++z)
        for (int x = 0; x < w; ++x) ht[static_cast<size_t>(z) * w + x] = heightAt(x, z);

    // Priority-Flood from the window border: fill[c] = the lowest "water level that can escape"
    // — max(height along the cheapest path out). Border cells escape at their own height.
    constexpr float kUnset = 3.0e38f;
    fill.assign(n, kUnset);
    using QE = std::pair<float, int>;   // (level, index), min-heap
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
    auto idx = [w](int x, int z) { return static_cast<size_t>(z) * w + x; };
    for (int x = 0; x < w; ++x) {
        pq.push({ht[idx(x, 0)], static_cast<int>(idx(x, 0))});
        pq.push({ht[idx(x, h - 1)], static_cast<int>(idx(x, h - 1))});
    }
    for (int z = 1; z < h - 1; ++z) {
        pq.push({ht[idx(0, z)], static_cast<int>(idx(0, z))});
        pq.push({ht[idx(w - 1, z)], static_cast<int>(idx(w - 1, z))});
    }
    static const int NX[4] = {1, -1, 0, 0}, NZ[4] = {0, 0, 1, -1};
    while (!pq.empty()) {
        const auto [lvl, ci] = pq.top();
        pq.pop();
        if (fill[ci] != kUnset) continue;
        fill[ci] = lvl;
        const int cx = ci % w, cz = ci / w;
        for (int k = 0; k < 4; ++k) {
            const int nx = cx + NX[k], nz = cz + NZ[k];
            if (nx < 0 || nz < 0 || nx >= w || nz >= h) continue;
            const size_t ni = idx(nx, nz);
            if (fill[ni] != kUnset) continue;
            pq.push({std::max(lvl, ht[ni]), static_cast<int>(ni)});
        }
    }

    // Wet components (fill > terrain) → candidate ponds; border-touching components discarded
    // (their basin escapes the window — a neighboring window will own or discard them the same
    // way, which is what keeps discovery deterministic across windows).
    std::vector<int32_t> comp(n, -1);
    std::vector<int> stack;
    for (int sz2 = 0; sz2 < h; ++sz2)
        for (int sx2 = 0; sx2 < w; ++sx2) {
            const size_t si = idx(sx2, sz2);
            if (comp[si] != -1 || fill[si] <= ht[si] + 1e-4f) continue;
            const int32_t id = static_cast<int32_t>(out.size());
            FinePond p;
            p.level = fill[si] - kPondFreeboard;
            p.bboxMin = p.bboxMax = glm::ivec2(sx2, sz2);
            float deepestH = 3.0e38f;
            bool touchesBorder = false;
            comp[si] = id;
            stack.assign(1, static_cast<int>(si));
            while (!stack.empty()) {
                const int ci = stack.back();
                stack.pop_back();
                const int cx = ci % w, cz = ci / w;
                p.columns.push_back((static_cast<uint32_t>(cx) << 16) | static_cast<uint32_t>(cz));
                p.bboxMin = glm::min(p.bboxMin, glm::ivec2(cx, cz));
                p.bboxMax = glm::max(p.bboxMax, glm::ivec2(cx, cz));
                if (cx == 0 || cz == 0 || cx == w - 1 || cz == h - 1) touchesBorder = true;
                if (ht[ci] < deepestH) {
                    deepestH = ht[ci];
                    p.deepest = glm::ivec2(cx, cz);
                }
                for (int k = 0; k < 4; ++k) {
                    const int nx = cx + NX[k], nz = cz + NZ[k];
                    if (nx < 0 || nz < 0 || nx >= w || nz >= h) continue;
                    const size_t ni = idx(nx, nz);
                    if (comp[ni] != -1 || fill[ni] <= ht[ni] + 1e-4f) continue;
                    // Same basin iff same spill level (a nested sub-basin shares its parent's).
                    if (std::fabs(fill[ni] - fill[si]) > 1e-3f) continue;
                    comp[ni] = id;
                    stack.push_back(static_cast<int>(ni));
                }
            }
            p.depth = (fill[si] - deepestH);
            const int area = static_cast<int>(p.columns.size());
            // Reject without emitting (the component labels remain as visited markers only —
            // they never need to match indices into `out`).
            if (touchesBorder || area < kPondMinArea || area > kPondMaxArea ||
                p.depth < kPondMinDepth)
                continue;
            std::sort(p.columns.begin(), p.columns.end());
            out.push_back(std::move(p));
        }
    return out;
}

}  // namespace Phyxel
