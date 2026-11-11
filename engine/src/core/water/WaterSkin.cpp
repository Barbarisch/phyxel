#include "core/water/WaterSkin.h"

#include "core/water/WaterLocality.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <climits>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <tuple>

namespace Phyxel::Core::Water {

namespace {

long floorDivL(long a, long b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
uint64_t columnKey(int cx, int cz) { return (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) | static_cast<uint32_t>(cz); }

// a column's water as the summary sees it: wet when its runs hold at least 1 mm (the surface extraction's floor) in all; its top the
// highest run's, its bottom the lowest's. Summed over EVERY entry of the column, not per run or per entry: a span the chunks store in
// two pieces (cut at a vertical chunk's face) and a reader that answers chunk by chunk give two entries for one column - counted
// once, they read the same as the whole span (equality (ii): counted per entry, a lake's cells read twice as wet)
struct ColumnAcc {
    float depth = 0.0f, top = -1e30f, bottom = 1e30f;
    void add(const ColumnRuns& c) {
        for (const ColumnRun& r : c.runs) {
            if (r.topY <= r.bottomY) continue;
            depth += r.topY - r.bottomY; top = std::max(top, r.topY); bottom = std::min(bottom, r.bottomY);
        }
    }
    bool wet() const { return depth >= 1e-3f; }
};

void addColumn(SkinCell& cell, int x, int z, float top, float bottom) {
    if (cell.wet == 0) { cell.x0 = cell.x1 = x; cell.z0 = cell.z1 = z; }
    else { cell.x0 = std::min(cell.x0, x); cell.x1 = std::max(cell.x1, x); cell.z0 = std::min(cell.z0, z); cell.z1 = std::max(cell.z1, z); }
    ++cell.wet; cell.top = std::max(cell.top, top); cell.bottom = std::min(cell.bottom, bottom);
}

void merge(SkinCell& into, const SkinCell& c) {
    if (c.wet <= 0) return;
    if (into.wet == 0) { into.x0 = c.x0; into.x1 = c.x1; into.z0 = c.z0; into.z1 = c.z1; }
    else { into.x0 = std::min(into.x0, c.x0); into.x1 = std::max(into.x1, c.x1); into.z0 = std::min(into.z0, c.z0); into.z1 = std::max(into.z1, c.z1); }
    into.wet += c.wet; into.top = std::max(into.top, c.top); into.bottom = std::min(into.bottom, c.bottom);
}

}  // namespace

void summarizeSkinCells(const std::vector<ColumnRuns>& runs, std::map<std::pair<int, int>, SkinCell>& out) {
    std::map<std::pair<int, int>, ColumnAcc> cols;
    for (const ColumnRuns& c : runs) cols[{c.x, c.z}].add(c);
    for (const auto& [xz, a] : cols) {
        if (!a.wet()) continue;
        addColumn(out[{static_cast<int>(floorDivL(xz.first, kSkinCellM)), static_cast<int>(floorDivL(xz.second, kSkinCellM))}], xz.first, xz.second, a.top, a.bottom);
    }
}

// 51.4 step 2: the generator worker - one thread, a queue of chunk columns (nearest first as asked), results handed back
struct WaterSkin::Worker {
    std::vector<GeneratorSource> fns;   // one per thread
    std::mutex mu;
    std::condition_variable cv;
    std::deque<uint64_t> queue;
    int inFlight = 0;
    std::vector<std::pair<uint64_t, std::vector<ColumnRuns>>> done;
    bool stop = false;
    std::vector<std::thread> th;
    void run(int i) {
        for (;;) {
            uint64_t key;
            {
                std::unique_lock<std::mutex> lk(mu);
                cv.wait(lk, [&] { return stop || !queue.empty(); });
                if (stop) return;
                key = queue.front(); queue.pop_front(); ++inFlight;
            }
            std::vector<ColumnRuns> runs;
            fns[static_cast<size_t>(i)](glm::ivec2(static_cast<int>(static_cast<int32_t>(key >> 32)), static_cast<int>(static_cast<int32_t>(key & 0xffffffffull))), runs);
            std::lock_guard<std::mutex> lk(mu);
            --inFlight;
            done.emplace_back(key, std::move(runs));
        }
    }
};

WaterSkin::WaterSkin() = default;

WaterSkin::~WaterSkin() { setGeneratorSource(nullptr); }

void WaterSkin::setGeneratorSource(GeneratorFactory factory, int threads) {
    if (m_worker) {
        { std::lock_guard<std::mutex> lk(m_worker->mu); m_worker->stop = true; }
        m_worker->cv.notify_all();
        for (std::thread& t : m_worker->th) if (t.joinable()) t.join();
        m_worker.reset();
    }
    // the old source's summaries go (another world's generator must never answer); the record's stay
    for (auto it = m_sum.begin(); it != m_sum.end();) { if (it->second.fromGen) it = m_sum.erase(it); else { it->second.requested = false; ++it; } }
    ++m_sumVersion; m_scanValid = false; ++m_genEpoch;
    if (!factory) return;
    m_worker = std::make_unique<Worker>();
    const int n = std::clamp(threads, 1, 8);
    for (int i = 0; i < n; ++i) m_worker->fns.push_back(factory());   // made here, each used by its own thread alone
    Worker* w = m_worker.get();
    for (int i = 0; i < n; ++i) m_worker->th.emplace_back([w, i] { w->run(i); });
}

void WaterSkin::clear() {
    m_sum.clear(); m_fieldMap.clear(); m_out.clear(); m_levelKeys.clear(); m_keyValid = false; m_resSeen = ~0ull; m_scanValid = false; m_lastPending = 0;
    if (m_worker) { std::lock_guard<std::mutex> lk(m_worker->mu); m_worker->queue.clear(); m_worker->done.clear(); }
}

void WaterSkin::buildSummary(int ccx, int ccz, Summary& s, const std::vector<ColumnRuns>& runs) {
    SkinCell cells[85];
    ColumnAcc cols[32 * 32];
    for (const ColumnRuns& c : runs) {
        const int lx = c.x - ccx * 32, lz = c.z - ccz * 32;
        if (lx < 0 || lz < 0 || lx >= 32 || lz >= 32) continue;   // a reader that answers outside the box: not this column's
        cols[lx + 32 * lz].add(c);
    }
    for (int lz = 0; lz < 32; ++lz) for (int lx = 0; lx < 32; ++lx) {   // world order within the column: x fastest (any order gives the same cells)
        const ColumnAcc& a = cols[lx + 32 * lz];
        if (!a.wet()) continue;
        addColumn(cells[(lx / kSkinCellM) + kSkinCellsPerChunk * (lz / kSkinCellM)], ccx * 32 + lx, ccz * 32 + lz, a.top, a.bottom);
    }
    // the 8, 16 and 32 m aggregates (a parent = its four children)
    for (int p = 1; p < 4; ++p) {
        const int per = kSkinCellsPerChunk >> p, childPer = per * 2;
        for (int j = 0; j < per; ++j) for (int i = 0; i < per; ++i) {
            SkinCell& parent = cells[kPyrOffset[p] + i + per * j];
            for (int dj = 0; dj < 2; ++dj) for (int di = 0; di < 2; ++di)
                merge(parent, cells[kPyrOffset[p - 1] + (2 * i + di) + childPer * (2 * j + dj)]);
        }
    }
    bool changed = !s.built;
    for (int k = 0; k < 85 && !changed; ++k) changed = !(cells[k] == s.cells[k]);
    if (changed) { std::copy(std::begin(cells), std::end(cells), std::begin(s.cells)); s.version = ++m_sumVersion; }
    s.built = true;
}

bool WaterSkin::aggregate(int /*level*/, int sizeM, long gx, long gz, SkinCell& out) const {
    out = SkinCell{};
    const long wx = gx * sizeM, wz = gz * sizeM;
    if (sizeM <= 32) {
        const int p = sizeM == 4 ? 0 : (sizeM == 8 ? 1 : (sizeM == 16 ? 2 : 3));
        const long ccx = floorDivL(wx, 32), ccz = floorDivL(wz, 32);
        const auto it = m_sum.find(columnKey(static_cast<int>(ccx), static_cast<int>(ccz)));
        if (it == m_sum.end() || !it->second.built) return false;
        const int per = kSkinCellsPerChunk >> p;
        const int li = static_cast<int>((wx - ccx * 32) / sizeM), lj = static_cast<int>((wz - ccz * 32) / sizeM);
        out = it->second.cells[kPyrOffset[p] + li + per * lj];
        return true;
    }
    bool any = false;
    const long n = sizeM / 32, ccx0 = floorDivL(wx, 32), ccz0 = floorDivL(wz, 32);
    for (long j = 0; j < n; ++j) for (long i = 0; i < n; ++i) {
        const auto it = m_sum.find(columnKey(static_cast<int>(ccx0 + i), static_cast<int>(ccz0 + j)));
        if (it == m_sum.end() || !it->second.built) continue;
        any = true; merge(out, it->second.cells[kPyrOffset[3]]);
    }
    return any;
}

void WaterSkin::update(const Settings& s, const Inputs& in) {
    const auto t0 = std::chrono::steady_clock::now();
    m_stats.summariesBuiltLastCall = 0; m_stats.fieldsRebuiltLastCall = 0; m_stats.pending = 0;
    if (!s.on || !in.reader || !*in.reader) {
        if (!m_out.empty() || !m_sum.empty() || !m_fieldMap.empty()) clear();
        m_stats.levels = m_stats.fields = m_stats.wetCells = m_stats.summaries = m_stats.ringCells = 0;
        return;
    }
    // the rings. base: a level-0 cell inside one 8 m record tile (4 or 8), or the seam would cut a cell. N: a multiple of 4 (each
    // level's square then sits on the next one's lattice), and level 0 reaches past the record tiles (no coarse cell meets them)
    const int base = s.baseCellM <= 4 ? 4 : 8;
    int n = std::max(8, (s.ringCells + 3) / 4 * 4);
    const float drawM = std::max(0.0f, in.drawM);
    while (static_cast<float>(n * base) * 0.5f < drawM + static_cast<float>(in.recordTileM) + 16.0f) n += 4;
    const float farM = std::max(0.0f, s.farM);
    int levels = 1;
    while (levels < kMaxLevels && static_cast<float>(n * (base << (levels - 1))) * 0.5f < farM) ++levels;
    const int sMax = base << (levels - 1);
    m_stats.levels = levels; m_stats.ringCells = n;

    // step 2: the generator's summaries that came back (a column made resident since keeps the record's)
    int genDrained = 0;
    auto resident = [&](int cx, int cz) { return !m_worker || !in.residentColumn || !*in.residentColumn || (*in.residentColumn)(cx, cz) != 0; };
    if (m_worker) {
        std::vector<std::pair<uint64_t, std::vector<ColumnRuns>>> done;
        { std::lock_guard<std::mutex> lk(m_worker->mu); done.swap(m_worker->done); }
        genDrained = static_cast<int>(done.size());
        for (auto& [key, runs] : done) {
            const auto it = m_sum.find(key);
            if (it == m_sum.end() || !it->second.requested) continue;   // forgotten meanwhile
            Summary& sm = it->second;
            sm.requested = false;
            const int cx = static_cast<int>(static_cast<int32_t>(key >> 32)), cz = static_cast<int>(static_cast<int32_t>(key & 0xffffffffull));
            if (resident(cx, cz)) { m_scanValid = false; continue; }   // its chunks loaded while it was queued: the record answers
            buildSummary(cx, cz, sm, runs);
            sm.fromGen = true; sm.spanRev = in.spanRevision; sm.res = 0;
            ++m_stats.genBuilt;
        }
    }
    // the summaries: chunk columns within reach of a drawn cell, nearest first, a budget per call
    if (in.residentColumn && *in.residentColumn && in.residentGlobal != m_resSeen) {
        m_resSeen = in.residentGlobal;
        bool moved = false;   // a re-scan only when one of OUR columns' chunks came or went (streaming moves the set nearly every frame)
        for (auto& [key, sm] : m_sum) {
            const int cx = static_cast<int>(static_cast<int32_t>(key >> 32)), cz = static_cast<int>(static_cast<int32_t>(key & 0xffffffffull));
            const uint64_t r = (*in.residentColumn)(cx, cz);
            if (sm.built ? r != sm.res : r != 0) { sm.spanRev = ~0ull; moved = true; }   // its chunks came or went: re-read
        }
        if (moved) ++m_resGen;
    }
    // the reach is measured from the viewer's chunk column's centre (+ one column), so the set moves only with that column: a camera
    // moving inside one column scans nothing
    const float reach = farM + static_cast<float>(sMax) * 1.5f + 32.0f;
    const long vcx = floorDivL(static_cast<long>(std::floor(in.view.x)), 32), vcz = floorDivL(static_cast<long>(std::floor(in.view.z)), 32);
    const glm::vec3 vc(static_cast<float>(vcx) * 32.0f + 16.0f, in.view.y, static_cast<float>(vcz) * 32.0f + 16.0f);
    const bool scan = !(m_scanValid && m_scanCx == vcx && m_scanCz == vcz && m_scanSpanRev == in.spanRevision && m_scanReach == reach
                        && m_scanResGen == m_resGen && m_lastPending == 0);
    m_scanValid = true; m_scanCx = vcx; m_scanCz = vcz; m_scanSpanRev = in.spanRevision; m_scanReach = reach; m_scanResGen = m_resGen;
    const long cx0 = floorDivL(static_cast<long>(std::floor(vc.x - reach)), 32), cx1 = floorDivL(static_cast<long>(std::floor(vc.x + reach)), 32);
    const long cz0 = floorDivL(static_cast<long>(std::floor(vc.z - reach)), 32), cz1 = floorDivL(static_cast<long>(std::floor(vc.z + reach)), 32);
    std::vector<std::pair<float, uint64_t>> stale, genAsk;
    if (scan) for (long cz = cz0; cz <= cz1; ++cz) for (long cx = cx0; cx <= cx1; ++cx) {
        const glm::vec2 lo(static_cast<float>(cx * 32), static_cast<float>(cz * 32)), hi = lo + glm::vec2(32.0f);
        const float d2 = WaterLocality::horizontalDistanceSqToBox(vc, lo, hi);
        if (d2 > reach * reach) continue;
        const uint64_t key = columnKey(static_cast<int>(cx), static_cast<int>(cz));
        const auto it = m_sum.find(key);
        if (!resident(static_cast<int>(cx), static_cast<int>(cz))) {
            // step 2: the generator's - asked once (it never goes stale on the span revision: the record is not its source)
            if (it == m_sum.end() || ((!it->second.built || !it->second.fromGen) && !it->second.requested)) genAsk.push_back({d2, key});
            continue;
        }
        if (it == m_sum.end() || !it->second.built || it->second.fromGen || it->second.spanRev != in.spanRevision) stale.push_back({d2, key});
    }
    // forget what is well out of reach (bounded memory)
    const float keep = reach + 64.0f;
    if (scan) for (auto it = m_sum.begin(); it != m_sum.end();) {
        const int cx = static_cast<int>(static_cast<int32_t>(it->first >> 32)), cz = static_cast<int>(static_cast<int32_t>(it->first & 0xffffffffull));
        const glm::vec2 lo(static_cast<float>(cx) * 32.0f, static_cast<float>(cz) * 32.0f);
        if (WaterLocality::horizontalDistanceSqToBox(vc, lo, lo + glm::vec2(32.0f)) > keep * keep) it = m_sum.erase(it); else ++it;   // no cell a level draws reads it: nothing to rebuild
    }
    std::sort(stale.begin(), stale.end());
    const int budget = std::max(1, s.summariesPerCall);
    for (const auto& [d2, key] : stale) {
        if (m_stats.summariesBuiltLastCall >= budget) { ++m_stats.pending; continue; }
        Summary& sm = m_sum[key];
        const int cx = static_cast<int>(static_cast<int32_t>(key >> 32)), cz = static_cast<int>(static_cast<int32_t>(key & 0xffffffffull));
        std::vector<ColumnRuns> runs;
        (*in.reader)(glm::ivec3(cx * 32, kYLo, cz * 32), glm::ivec3(cx * 32 + 31, kYHi, cz * 32 + 31), runs);
        buildSummary(cx, cz, sm, runs);
        sm.spanRev = in.spanRevision; sm.fromGen = false; sm.requested = false;
        sm.res = (in.residentColumn && *in.residentColumn) ? (*in.residentColumn)(cx, cz) : 0;
        ++m_stats.summariesBuiltLastCall; ++m_stats.summariesBuilt;
    }
    if (m_worker && !genAsk.empty()) {
        std::sort(genAsk.begin(), genAsk.end());
        {
            std::lock_guard<std::mutex> lk(m_worker->mu);
            for (const auto& [d2, key] : genAsk) { m_sum[key].requested = true; m_worker->queue.push_back(key); }
        }
        m_worker->cv.notify_all();
    }
    m_stats.summaries = static_cast<int>(m_sum.size());
    m_lastPending = m_stats.pending;
    m_stats.genPending = 0;
    if (m_worker) { std::lock_guard<std::mutex> lk(m_worker->mu); m_stats.genPending = static_cast<int>(m_worker->queue.size()) + m_worker->inFlight; }
    // counted only on a call that changed the summaries: a walk over thousands of 2.4 KB entries is ~0.2 ms of cache misses (live, 51.4c)
    if (scan || m_stats.summariesBuiltLastCall > 0 || genDrained > 0) { m_stats.genHeld = 0; for (const auto& [key, sm] : m_sum) if (sm.fromGen) ++m_stats.genHeld; }

    // the fields: a level is rebuilt only when what it reads moved - its snapped square, the summaries, the volumes, the settings,
    // and for level 0 the set of record tiles in range (a still camera over a still record rebuilds nothing; a moving one rebuilds
    // a level only when it crosses that level's snap step or, level 0, a record tile enters or leaves the radius)
    Key key{glm::vec3(0.0f), drawM, farM, base, n, in.recordTilesOn, m_sumVersion, in.volumeBoxesRevision};
    const bool globalSame = m_keyValid && key == m_key && static_cast<int>(m_levelKeys.size()) == levels;
    m_key = key; m_keyValid = true;
    m_levelKeys.resize(static_cast<size_t>(levels));
    const float rt = static_cast<float>(in.recordTileM);
    auto tileInRange = [&](long tx, long tz) {   // refreshRecordTiles' own test
        const glm::vec2 lo(static_cast<float>(tx) * rt, static_cast<float>(tz) * rt);
        return WaterLocality::horizontalDistanceSqToBox(in.view, lo, lo + glm::vec2(rt)) <= drawM * drawM;
    };
    // the record tiles in range, row by row (tz, first tx, last tx) - level 0's mask, hashed
    std::vector<long> maskRows;
    uint64_t maskHash = 1469598103934665603ull;
    if (in.recordTilesOn) {
        const long rtz0 = floorDivL(static_cast<long>(std::floor(in.view.z - drawM)), in.recordTileM) - 1;
        const long rtz1 = floorDivL(static_cast<long>(std::floor(in.view.z + drawM)), in.recordTileM) + 1;
        const long rtx0 = floorDivL(static_cast<long>(std::floor(in.view.x - drawM)), in.recordTileM) - 1;
        const long rtx1 = floorDivL(static_cast<long>(std::floor(in.view.x + drawM)), in.recordTileM) + 1;
        for (long tz = rtz0; tz <= rtz1; ++tz) {
            long a = LONG_MAX, b = LONG_MIN;
            for (long tx = rtx0; tx <= rtx1; ++tx) if (tileInRange(tx, tz)) { a = std::min(a, tx); b = std::max(b, tx); }
            if (a > b) continue;
            maskRows.push_back(tz); maskRows.push_back(a); maskRows.push_back(b);
            for (long v : {tz, a, b}) { maskHash ^= static_cast<uint64_t>(v); maskHash *= 1099511628211ull; }
        }
    }
    auto recordMasked = [&](long wx, long wz) {   // the record tile holding this world point is in range (a disc: rows are runs)
        const long tx = floorDivL(wx, in.recordTileM), tz = floorDivL(wz, in.recordTileM);
        for (size_t i = 0; i + 2 < maskRows.size(); i += 3)
            if (maskRows[i] == tz) return tx >= maskRows[i + 1] && tx <= maskRows[i + 2];
        return false;
    };
    auto inVolume = [&](float x, float z) {
        if (!in.volumeBoxes) return false;
        for (const auto& b : *in.volumeBoxes)
            if (x >= static_cast<float>(b.first.x) && x < static_cast<float>(b.second.x + 1) && z >= static_cast<float>(b.first.z) && z < static_cast<float>(b.second.z + 1)) return true;
        return false;
    };
    long prevX0 = 0, prevZ0 = 0;   // the finer level's square origin, in its own cells
    bool anyLevel = false;
    for (int k = 0; k < levels; ++k) {
        const int sz = base << k;
        const float szf = static_cast<float>(sz);
        // the square: N cells, its origin on twice the cell (so the finer square sits on this lattice)
        const long gx0 = static_cast<long>(std::floor(in.view.x / (2.0f * szf))) * 2 - n / 2;
        const long gz0 = static_cast<long>(std::floor(in.view.z / (2.0f * szf))) * 2 - n / 2;
        const long hx0 = prevX0 / 2, hz0 = prevZ0 / 2, hn = n / 2;   // the finer square, in this level's cells (k > 0)
        prevX0 = gx0; prevZ0 = gz0;
        LevelKey lk{gx0, gz0, hx0, hz0, k == 0 ? maskHash : 0ull, true};
        const LevelKey old = m_levelKeys[static_cast<size_t>(k)];
        if (globalSame && old == lk) continue;
        // only the record mask moved (level 0, the square where it was): the tiles under the old and the new mask's boxes
        const bool maskOnly = globalSame && k == 0 && old.valid && old.gx0 == gx0 && old.gz0 == gz0 && old.hx0 == hx0 && old.hz0 == hz0;
        long mtx0 = LONG_MIN, mtx1 = LONG_MAX, mtz0 = LONG_MIN, mtz1 = LONG_MAX;
        if (k == 0) {
            long bx0 = LONG_MAX, bx1 = LONG_MIN, bz0 = LONG_MAX, bz1 = LONG_MIN;
            for (size_t i = 0; i + 2 < maskRows.size(); i += 3) {
                bz0 = std::min(bz0, maskRows[i]); bz1 = std::max(bz1, maskRows[i]); bx0 = std::min(bx0, maskRows[i + 1]); bx1 = std::max(bx1, maskRows[i + 2]);
            }
            const long per = static_cast<long>(kSurfaceTileCols) * sz;   // a level-0 field's width (m)
            const long nb0x = bx0 <= bx1 ? floorDivL(bx0 * in.recordTileM, per) : LONG_MAX, nb1x = bx0 <= bx1 ? floorDivL(bx1 * in.recordTileM + in.recordTileM - 1, per) : LONG_MIN;
            const long nb0z = bz0 <= bz1 ? floorDivL(bz0 * in.recordTileM, per) : LONG_MAX, nb1z = bz0 <= bz1 ? floorDivL(bz1 * in.recordTileM + in.recordTileM - 1, per) : LONG_MIN;
            if (maskOnly) {
                mtx0 = std::min(nb0x, m_maskTiles[0]); mtx1 = std::max(nb1x, m_maskTiles[1]);
                mtz0 = std::min(nb0z, m_maskTiles[2]); mtz1 = std::max(nb1z, m_maskTiles[3]);
            }
            m_maskTiles[0] = nb0x; m_maskTiles[1] = nb1x; m_maskTiles[2] = nb0z; m_maskTiles[3] = nb1z;
        }
        m_levelKeys[static_cast<size_t>(k)] = lk;
        anyLevel = true;
        // the far cut, from the square's own (snapped) centre: which cells a level draws moves only with its square
        const float fcx = (static_cast<float>(gx0) + static_cast<float>(n) * 0.5f) * szf, fcz = (static_cast<float>(gz0) + static_cast<float>(n) * 0.5f) * szf;
        const long tx0 = floorDivL(gx0, kSurfaceTileCols), tx1 = floorDivL(gx0 + n - 1, kSurfaceTileCols);
        const long tz0 = floorDivL(gz0, kSurfaceTileCols), tz1 = floorDivL(gz0 + n - 1, kSurfaceTileCols);
        // this level's tiles: rebuilt; the ones it no longer covers go
        for (auto it = m_fieldMap.begin(); it != m_fieldMap.end();) {
            const auto& [lv, ftx, ftz] = it->first;
            if (lv == k && (ftx < tx0 || ftx > tx1 || ftz < tz0 || ftz > tz1)) it = m_fieldMap.erase(it); else ++it;
        }
        for (long tz = tz0; tz <= tz1; ++tz) for (long tx = tx0; tx <= tx1; ++tx) {
            if (tx < mtx0 || tx > mtx1 || tz < mtz0 || tz > mtz1) continue;   // maskOnly: the mask never reached this tile
            const auto fk = std::make_tuple(k, static_cast<int>(tx), static_cast<int>(tz));
            std::vector<SurfaceColumn> cols(static_cast<size_t>(kSurfaceTileCols) * kSurfaceTileCols);
            bool wetAny = false; bool lookSet = false; WaterLookPacked look{};
            for (int j = 0; j < kSurfaceTileCols; ++j) for (int i = 0; i < kSurfaceTileCols; ++i) {
                const long gx = tx * kSurfaceTileCols + i, gz = tz * kSurfaceTileCols + j;
                SurfaceColumn& c = cols[static_cast<size_t>(i) + static_cast<size_t>(kSurfaceTileCols) * j];
                SkinCell a;
                const bool known = aggregate(k, sz, gx, gz, a);
                // wet: at least half the wet columns' rectangle is wet (the red control: half the cell's area)
                const long rect = a.wet > 0 ? static_cast<long>(a.x1 - a.x0 + 1) * (a.z1 - a.z0 + 1) : 0;
                const bool wet = known && a.wet > 0 && 2 * static_cast<long>(a.wet) >= (s_wholeCellsForTest ? static_cast<long>(sz) * sz : rect);
                const float cxw = (static_cast<float>(gx) + 0.5f) * szf, czw = (static_cast<float>(gz) + 0.5f) * szf;
                bool drawn = gx >= gx0 && gx < gx0 + n && gz >= gz0 && gz < gz0 + n;
                if (drawn && k > 0 && gx >= hx0 && gx < hx0 + hn && gz >= hz0 && gz < hz0 + hn) drawn = false;   // the finer level draws it
                if (drawn) { const float dx = cxw - fcx, dz = czw - fcz; if (dx * dx + dz * dz > farM * farM) drawn = false; }
                if (drawn && k == 0 && recordMasked(gx * sz, gz * sz)) drawn = false;   // the record tiles draw it
                if (drawn && inVolume(cxw, czw)) drawn = false;                           // a live volume draws it
                if (!drawn) {
                    // another drawer's column: an edge, no wall; its level (when wet) is what a corner here meets (27.15)
                    c.runs = -1.0f;
                    if (wet) { c.top[0] = a.top; c.bottom[0] = a.bottom; }
                    continue;
                }
                if (!wet) { c.runs = 0.0f; c.solidTopY = 1e30f; continue; }   // dry: no lateral face (a coarse shore is a flat edge)
                c.runs = 1.0f; c.top[0] = a.top; c.bottom[0] = a.bottom; c.solidTopY = a.bottom;
                if (!s_wholeCellsForTest) {   // the quad over the wet columns' rectangle only
                    const float wx0 = static_cast<float>(gx) * szf, wz0 = static_cast<float>(gz) * szf;
                    c.pad1 = skinInsetEncode((static_cast<float>(a.x0) - wx0) / szf, (static_cast<float>(a.x1 + 1) - wx0) / szf);
                    c.pad2 = skinInsetEncode((static_cast<float>(a.z0) - wz0) / szf, (static_cast<float>(a.z1 + 1) - wz0) / szf);
                }
                wetAny = true;
                if (!lookSet && in.look) { look = in.look(static_cast<int>(std::floor(cxw)), static_cast<int>(std::floor(czw)), a.top); lookSet = true; }
            }
            FieldEntry& e = m_fieldMap[fk];
            WaterSurfaceField& f = e.field;
            const glm::ivec3 origin(static_cast<int>(tx * kSurfaceTileCols), 0, static_cast<int>(tz * kSurfaceTileCols));
            const bool same = f.cols.size() == cols.size() && f.origin == origin && f.h == szf && f.version != 0
                              && std::memcmp(f.cols.data(), cols.data(), cols.size() * sizeof(SurfaceColumn)) == 0
                              && std::memcmp(&f.look, &look, sizeof(look)) == 0;
            e.wetAny = wetAny;
            if (same) continue;
            f.origin = origin; f.nx = f.nz = kSurfaceTileCols; f.h = szf; f.cols = std::move(cols); f.look = look;
            f.rippleLayer = -1; f.gpuDrawn = false; f.insetCols = true;
            f.source = kSourceBit | (k << 26) | ((static_cast<int>(tx) & 0x1FFF) << 13) | (static_cast<int>(tz) & 0x1FFF);
            f.version = ++m_fieldVersion;
            ++m_stats.fieldsRebuiltLastCall;
        }
    }
    if (!globalSame) {   // fewer levels than before: theirs go
        for (auto it = m_fieldMap.begin(); it != m_fieldMap.end();) { if (std::get<0>(it->first) >= levels) it = m_fieldMap.erase(it); else ++it; }
    }
    if (anyLevel || !globalSame) {
        m_out.clear();
        m_stats.wetCells = 0;
        for (const auto& [fk, e] : m_fieldMap) {
            if (!e.wetAny) continue;
            m_out.push_back(&e.field);
            for (const SurfaceColumn& c : e.field.cols) if (c.runs > 0.5f) ++m_stats.wetCells;
        }
        m_stats.fields = static_cast<int>(m_out.size());
    }
    m_stats.msLastCall = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    m_stats.msMax = std::max(m_stats.msMax, m_stats.msLastCall);
}

bool WaterSkin::drawnAt(float x, float z, float& top, int* claims) const {
    bool any = false; top = -1e30f; int n = 0;
    for (const WaterSurfaceField* fp : m_out) {
        const WaterSurfaceField& f = *fp;
        const int cx = static_cast<int>(std::floor(x / f.h)) - f.origin.x, cz = static_cast<int>(std::floor(z / f.h)) - f.origin.z;
        if (cx < 0 || cz < 0 || cx >= f.nx || cz >= f.nz) continue;
        const SurfaceColumn& c = f.at(cx, cz);
        if (c.runs < 0.5f) continue;
        if (f.insetCols) {   // the covered rectangle only
            float a, b;
            const float fx = x / f.h - std::floor(x / f.h), fz = z / f.h - std::floor(z / f.h);
            skinInsetDecode(c.pad1, a, b); if (fx < a || fx >= b) continue;
            skinInsetDecode(c.pad2, a, b); if (fz < a || fz >= b) continue;
        }
        any = true; ++n; top = std::max(top, c.top[0]);
    }
    if (claims) *claims = n;
    return any;
}

bool WaterSkin::cellAt(int cx, int cz, SkinCell& out) const {
    const long ccx = floorDivL(static_cast<long>(cx) * kSkinCellM, 32), ccz = floorDivL(static_cast<long>(cz) * kSkinCellM, 32);
    const auto it = m_sum.find(columnKey(static_cast<int>(ccx), static_cast<int>(ccz)));
    if (it == m_sum.end() || !it->second.built) return false;
    const int li = static_cast<int>(static_cast<long>(cx) * kSkinCellM - ccx * 32) / kSkinCellM, lj = static_cast<int>(static_cast<long>(cz) * kSkinCellM - ccz * 32) / kSkinCellM;
    out = it->second.cells[li + kSkinCellsPerChunk * lj];
    return true;
}

}  // namespace Phyxel::Core::Water
