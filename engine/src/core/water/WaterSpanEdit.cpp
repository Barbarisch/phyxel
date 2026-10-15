#include "core/water/WaterSpanEdit.h"

#include <algorithm>
#include <cmath>

namespace Phyxel::Core::Water {

namespace {
constexpr float kMinDepth = WaterGrid::kSurfaceMinDepth;
}

SpanEditResult spanSolidPlaced(std::vector<WorldSpan>& spans, int x, int y, int z) {
    SpanEditResult r;
    const float lo = static_cast<float>(y), hi = static_cast<float>(y + 1);
    std::vector<WorldSpan> out;
    out.reserve(spans.size() + 1);
    for (const auto& s : spans) {
        if (s.x != x || s.z != z) { out.push_back(s); continue; }
        const float ov = std::min(s.topY, hi) - std::max(s.bottomY, lo);
        if (ov <= 0.0f) { out.push_back(s); continue; }
        r.changed = true;
        r.displaced += ov;   // 1 m^2 column: depth = volume
        WorldSpan below = s; below.topY = std::min(s.topY, lo);
        WorldSpan above = s; above.bottomY = std::max(s.bottomY, hi);
        if (below.topY - below.bottomY >= kMinDepth) out.push_back(below); else if (below.topY > below.bottomY) r.displaced += below.topY - below.bottomY;
        if (above.topY - above.bottomY >= kMinDepth) out.push_back(above); else if (above.topY > above.bottomY) r.displaced += above.topY - above.bottomY;
    }
    spans.swap(out);
    return r;
}

SpanEditResult spanSolidRemoved(std::vector<WorldSpan>& spans, int x, int y, int z) {
    SpanEditResult r;
    for (auto& s : spans) {
        if (s.x != x || s.z != z) continue;
        if (std::abs(s.bottomY - static_cast<float>(y + 1)) < 1e-5f) {   // rests exactly on the removed voxel
            s.bottomY -= 1.0f; s.topY -= 1.0f;
            r.changed = true;
        }
    }
    return r;
}

}  // namespace Phyxel::Core::Water
