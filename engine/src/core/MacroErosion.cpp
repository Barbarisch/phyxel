#include "core/MacroErosion.h"

#include <algorithm>
#include <cmath>

#include "core/PriorityFlood.h"

namespace Phyxel {

namespace {

// Braun & Willett 2013 stack: every base-level cell first, then its donors depth-first, so a
// cell is always visited AFTER its receiver (the implicit update reads h_r^{t+1}).
std::vector<int> buildStack(const std::vector<int>& receiver, int n) {
    std::vector<int> donorCount(n, 0);
    for (int i = 0; i < n; ++i)
        if (receiver[i] != i) ++donorCount[receiver[i]];
    std::vector<int> donorStart(n + 1, 0);
    for (int i = 0; i < n; ++i) donorStart[i + 1] = donorStart[i] + donorCount[i];
    std::vector<int> donors(static_cast<size_t>(std::max(donorStart[n], 1)));
    std::vector<int> fillPos(donorStart.begin(), donorStart.end() - 1);
    for (int i = 0; i < n; ++i)
        if (receiver[i] != i) donors[fillPos[receiver[i]]++] = i;   // ascending i: deterministic

    std::vector<int> stack;
    stack.reserve(n);
    std::vector<int> work;
    for (int i = 0; i < n; ++i) {
        if (receiver[i] != i) continue;   // base level roots, in index order
        work.push_back(i);
        while (!work.empty()) {
            const int c = work.back();
            work.pop_back();
            stack.push_back(c);
            for (int d = donorStart[c + 1] - 1; d >= donorStart[c]; --d) work.push_back(donors[d]);
        }
    }
    return stack;
}

}  // namespace

MacroErosionResult runMacroErosion(const std::vector<float>& height, int w, int h,
                                   const std::vector<float>& uplift, const MacroErosionParams& p) {
    MacroErosionResult out;
    const int n = w * h;
    out.height = height;
    if (n == 0) return out;
    const float cellArea = p.cellU * p.cellU;
    const float diag = p.cellU * 1.41421356f;

    double sum = 0.0;
    for (float v : height) sum += v;
    out.meanHeightBefore = static_cast<float>(sum / n);

    const int steps = std::max(0, p.steps);
    for (int step = 0; step <= steps; ++step) {
        // Routing on the FILLED surface (pits are lakes that spill), erosion on the real one.
        const PriorityFlood::FlowResult flow = PriorityFlood::fillWithFlow(out.height, w, h, p.seaLevel);
        out.receiver = flow.downstream;
        out.stack = buildStack(out.receiver, n);
        out.drainageArea.assign(n, cellArea);
        for (int s = n - 1; s >= 0; --s) {   // reverse stack: donors before receivers
            const int i = out.stack[s];
            const int r = out.receiver[i];
            if (r != i) out.drainageArea[r] += out.drainageArea[i];
        }
        out.baseLevelCells = 0;
        for (int i = 0; i < n; ++i) if (out.receiver[i] == i) ++out.baseLevelCells;
        if (step == steps) {
            // The last pass refreshes the drainage structure on the FINAL heights; the talus
            // ceiling is re-applied along that final routing (the per-step ceiling holds against the
            // step's receivers, and re-routing can leave a cell above its NEW receiver by more
            // than the ceiling: 16 of 9216 rig cells did, MacroErosionTest red 2026-10-15).
            if (steps > 0 && p.maxSlope > 0.0f) {
                for (int s = 0; s < n; ++s) {
                    const int i = out.stack[s];
                    const int r = out.receiver[i];
                    if (r == i) continue;
                    const int ix = i % w, iz = i / w, rx = r % w, rz = r / w;
                    const float dist = (ix != rx && iz != rz) ? diag : p.cellU;
                    const float cap = out.height[r] + p.maxSlope * dist;
                    if (out.height[i] > cap) out.height[i] = cap;
                }
            }
            break;
        }

        // Implicit stream-power update along the stack (receivers already updated).
        for (int s = 0; s < n; ++s) {
            const int i = out.stack[s];
            const int r = out.receiver[i];
            if (r == i || out.height[i] <= p.seaLevel) continue;   // base level: fixed
            const float u = uplift.empty() ? 0.0f : p.upliftMax * uplift[i];
            const int ix = i % w, iz = i / w, rx = r % w, rz = r / w;
            const float dist = (ix != rx && iz != rz) ? diag : p.cellU;
            const float f = p.k * p.dt * std::pow(out.drainageArea[i], p.m) / dist;
            const float hr = out.height[r];
            float hn = (out.height[i] + p.dt * u + f * hr) / (1.0f + f);
            if (hn < hr) hn = hr;   // never erode below the receiver (no new pits from the solve)
            // Cordonnier §5: "if the result of the stream power equation leads to slopes higher
            // than 30°, we reduce the change of elevation so as to keep slopes in the prescribed
            // range" — the talus ceiling between a cell and its receiver.
            if (p.maxSlope > 0.0f && hn > hr + p.maxSlope * dist) hn = hr + p.maxSlope * dist;
            out.height[i] = hn;
        }
    }

    sum = 0.0;
    for (float v : out.height) sum += v;
    out.meanHeightAfter = static_cast<float>(sum / n);
    return out;
}

float macroValleyDensity(const std::vector<float>& height, int w, int h, float cellU) {
    if (w < 3 || h < 3 || height.size() != static_cast<size_t>(w) * h) return 0.0f;
    int troughs = 0;
    for (int z = 1; z < h - 1; ++z)
        for (int x = 1; x < w - 1; ++x) {
            const float c = height[static_cast<size_t>(z) * w + x];
            const bool ew = c < height[static_cast<size_t>(z) * w + x - 1] && c < height[static_cast<size_t>(z) * w + x + 1];
            const bool ns = c < height[static_cast<size_t>(z - 1) * w + x] && c < height[static_cast<size_t>(z + 1) * w + x];
            if (ew || ns) ++troughs;
        }
    const double areaKm2 = static_cast<double>(w) * h * cellU * cellU * 1e-6;
    return static_cast<float>(troughs / areaKm2);
}

float macroDrainageDensity(const MacroErosionResult& r, int w, int h, float cellU, float thresholdArea) {
    const int n = w * h;
    if (n == 0 || r.receiver.size() != static_cast<size_t>(n)) return 0.0f;
    const float diag = cellU * 1.41421356f;
    double lengthM = 0.0;
    for (int i = 0; i < n; ++i) {
        const int rc = r.receiver[i];
        if (rc == i || r.drainageArea[i] < thresholdArea) continue;
        const int ix = i % w, iz = i / w, rx = rc % w, rz = rc / w;
        lengthM += (ix != rx && iz != rz) ? diag : cellU;
    }
    const double areaKm2 = static_cast<double>(n) * cellU * cellU * 1e-6;
    return static_cast<float>((lengthM * 1e-3) / areaKm2);
}

} // namespace Phyxel
