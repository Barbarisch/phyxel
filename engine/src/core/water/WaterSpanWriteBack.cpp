#include "core/water/WaterSpanWriteBack.h"

#include <algorithm>
#include <cmath>

namespace Phyxel::Core::Water {

namespace {
constexpr float kMinDepth = WaterGrid::kSurfaceMinDepth;   // a run thinner than a millimetre is not stored

bool spanLess(const WorldSpan& a, const WorldSpan& b) {
    if (a.x != b.x) return a.x < b.x;
    if (a.z != b.z) return a.z < b.z;
    return a.bottomY < b.bottomY;
}
}  // namespace

bool clipSpanToChunk(const WorldSpan& s, int cy, Chunk::WaterSpanLocal& out) {
    const float base = static_cast<float>(cy) * 32.0f;
    const float lo = std::max(s.bottomY, base), hi = std::min(s.topY, base + 32.0f);
    if (hi <= lo) return false;
    out.x = static_cast<uint8_t>(s.x - (static_cast<int>(std::floor(s.x / 32.0f)) * 32));
    out.z = static_cast<uint8_t>(s.z - (static_cast<int>(std::floor(s.z / 32.0f)) * 32));
    out.bottom = lo - base;
    out.top = hi - base;
    return true;
}

std::vector<WorldSpan> assembleColumnSpans(const std::map<int, const std::vector<Chunk::WaterSpanLocal>*>& chunks, int cx, int cz) {
    std::vector<WorldSpan> out;
    for (const auto& [cy, spans] : chunks) {
        if (!spans) continue;
        const float base = static_cast<float>(cy) * 32.0f;
        for (const auto& s : *spans) {
            WorldSpan w;
            w.x = cx * 32 + s.x; w.z = cz * 32 + s.z;
            w.bottomY = base + s.bottom; w.topY = base + s.top;
            out.push_back(w);
        }
    }
    std::sort(out.begin(), out.end(), spanLess);
    // join clips that meet exactly at a chunk border (the storage contract for a run that crosses it)
    std::vector<WorldSpan> joined;
    for (const auto& w : out) {
        if (!joined.empty()) {
            WorldSpan& p = joined.back();
            if (p.x == w.x && p.z == w.z && std::abs(p.topY - w.bottomY) < 1e-5f && std::fmod(w.bottomY, 32.0f) == 0.0f) { p.topY = w.topY; continue; }
        }
        joined.push_back(w);
    }
    return joined;
}

std::vector<WorldSpan> mergeColumnRuns(const std::vector<WorldSpan>& existing, int x, int z, float yLo, float yHi, const std::vector<ColumnRun>& runs) {
    std::vector<WorldSpan> out;
    for (const auto& e : existing) {
        if (e.x != x || e.z != z) continue;
        // keep what lies outside [yLo, yHi)
        if (e.bottomY < yLo) { WorldSpan k = e; k.topY = std::min(e.topY, yLo); if (k.topY - k.bottomY >= kMinDepth) out.push_back(k); }
        if (e.topY > yHi)    { WorldSpan k = e; k.bottomY = std::max(e.bottomY, yHi); if (k.topY - k.bottomY >= kMinDepth) out.push_back(k); }
    }
    for (const auto& r : runs) {
        WorldSpan w; w.x = x; w.z = z;
        w.bottomY = std::max(r.bottomY, yLo); w.topY = std::min(r.topY, yHi);
        if (w.topY - w.bottomY >= kMinDepth) out.push_back(w);
    }
    std::sort(out.begin(), out.end(), spanLess);
    // a kept lower part that touches a new run exactly (a volume whose floor is mid-column) joins it
    std::vector<WorldSpan> joined;
    for (const auto& w : out) {
        if (!joined.empty() && std::abs(joined.back().topY - w.bottomY) < 1e-5f) { joined.back().topY = w.topY; continue; }
        joined.push_back(w);
    }
    return joined;
}

void clipSpansToChunks(const std::vector<WorldSpan>& spans, int cx, int cz, int cyLo, int cyHi,
                       std::map<int, std::vector<Chunk::WaterSpanLocal>>& out) {
    for (int cy = cyLo; cy <= cyHi; ++cy) out[cy];   // present, possibly empty (a drained chunk is cleared)
    for (const auto& s : spans) {
        const int cyA = static_cast<int>(std::floor(s.bottomY / 32.0f));
        const int cyB = static_cast<int>(std::floor((s.topY - 1e-4f) / 32.0f));
        for (int cy = cyA; cy <= cyB; ++cy) {
            Chunk::WaterSpanLocal l;
            if (!clipSpanToChunk(s, cy, l)) continue;
            (void)cx; (void)cz;
            out[cy].push_back(l);
        }
    }
    for (auto& [cy, list] : out)
        std::sort(list.begin(), list.end(), [](const Chunk::WaterSpanLocal& a, const Chunk::WaterSpanLocal& b) {
            const uint32_t ka = (uint32_t(a.x) << 8) | a.z, kb = (uint32_t(b.x) << 8) | b.z;
            return ka != kb ? ka < kb : a.bottom < b.bottom;
        });
}

void spansToColumnRuns(const std::vector<WorldSpan>& spans, float yLo, float yHi, std::vector<ColumnRuns>& out) {
    out.clear();
    for (const auto& s : spans) {
        const float lo = std::max(s.bottomY, yLo), hi = std::min(s.topY, yHi);
        if (hi - lo < kMinDepth) continue;
        if (out.empty() || out.back().x != s.x || out.back().z != s.z) { ColumnRuns c; c.x = s.x; c.z = s.z; out.push_back(c); }
        ColumnRun r; r.bottomY = lo; r.topY = hi; r.mass = static_cast<double>(hi) - lo;
        out.back().runs.push_back(r);
    }
}

}  // namespace Phyxel::Core::Water
