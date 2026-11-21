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

namespace {
bool localLess(const Chunk::WaterSpanLocal& a, const Chunk::WaterSpanLocal& b) {
    const uint32_t ka = (uint32_t(a.x) << 8) | a.z, kb = (uint32_t(b.x) << 8) | b.z;
    return ka != kb ? ka < kb : a.bottom < b.bottom;
}
bool sameLists(const std::vector<Chunk::WaterSpanLocal>& a, const std::vector<Chunk::WaterSpanLocal>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].x != b[i].x || a[i].z != b[i].z || a[i].bottom != b[i].bottom || a[i].top != b[i].top) return false;
    return true;
}
}  // namespace

void writeRunsIntoChunkColumn(const std::map<int, const std::vector<Chunk::WaterSpanLocal>*>& have, int cx, int cz,
                              const std::vector<const ColumnRuns*>& cols, float yLo, float yHi,
                              std::map<int, std::vector<Chunk::WaterSpanLocal>>& out, ChunkColumnWriteStats* stats) {
    // 58 item 1: one pass over the chunk column's spans sorts out the written columns' (the rest are copied untouched); each
    // written column is assembled, merged and clipped on its own spans. Before: the whole chunk column assembled, each written
    // column merged against ALL of it (columns x spans: 88,725 visits for one 8 m tile in WaterSpanWriteBackTest), all re-clipped
    out.clear();
    ChunkColumnWriteStats st;
    if (have.empty()) { if (stats) *stats = st; return; }
    std::vector<int> colIdx(32 * 32, -1);
    for (size_t i = 0; i < cols.size(); ++i) {
        const int lx = cols[i]->x - cx * 32, lz = cols[i]->z - cz * 32;
        if (lx >= 0 && lx < 32 && lz >= 0 && lz < 32 && colIdx[lx * 32 + lz] < 0) colIdx[lx * 32 + lz] = static_cast<int>(i);
    }
    std::vector<std::vector<WorldSpan>> colSpans(cols.size());
    for (const auto& [cy, spans] : have) {
        if (!spans) continue;
        const float base = static_cast<float>(cy) * 32.0f;
        for (const auto& sp : *spans) {
            ++st.spansVisited;
            const int k = colIdx[sp.x * 32 + sp.z];
            if (k < 0) continue;
            WorldSpan w; w.x = cx * 32 + sp.x; w.z = cz * 32 + sp.z; w.bottomY = base + sp.bottom; w.topY = base + sp.top;   // assembleColumnSpans' own
            colSpans[static_cast<size_t>(k)].push_back(w);
        }
    }
    std::map<int, std::vector<Chunk::WaterSpanLocal>> fresh;
    for (size_t i = 0; i < cols.size(); ++i) {
        std::vector<WorldSpan>& cs = colSpans[i];
        st.spansVisited += static_cast<long>(cs.size());
        std::sort(cs.begin(), cs.end(), spanLess);
        std::vector<WorldSpan> joined;   // assembleColumnSpans' join: clips meeting exactly at a chunk border are one run
        for (const auto& w : cs) {
            if (!joined.empty()) {
                WorldSpan& p = joined.back();
                if (std::abs(p.topY - w.bottomY) < 1e-5f && std::fmod(w.bottomY, 32.0f) == 0.0f) { p.topY = w.topY; continue; }
            }
            joined.push_back(w);
        }
        for (const WorldSpan& m : mergeColumnRuns(joined, cols[i]->x, cols[i]->z, yLo, yHi, cols[i]->runs)) {
            const int cyA = static_cast<int>(std::floor(m.bottomY / 32.0f)), cyB = static_cast<int>(std::floor((m.topY - 1e-4f) / 32.0f));
            for (int cy = cyA; cy <= cyB; ++cy) {
                Chunk::WaterSpanLocal l;
                if (have.count(cy) && clipSpanToChunk(m, cy, l)) fresh[cy].push_back(l);   // not resident: it keeps what it had
            }
        }
    }
    for (const auto& [cy, spans] : have) {
        if (!spans) continue;
        std::vector<Chunk::WaterSpanLocal> list;
        list.reserve(spans->size() + 8);
        for (const auto& sp : *spans) if (colIdx[sp.x * 32 + sp.z] < 0) list.push_back(sp);
        if (const auto f = fresh.find(cy); f != fresh.end()) list.insert(list.end(), f->second.begin(), f->second.end());
        std::sort(list.begin(), list.end(), localLess);
        if (!sameLists(*spans, list)) out[cy] = std::move(list);
    }
    st.chunksChanged = static_cast<int>(out.size());
    if (stats) *stats = st;
}

}  // namespace Phyxel::Core::Water
