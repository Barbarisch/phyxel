#include "core/WaterOccupancy.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <algorithm>
#include <functional>
#include <queue>
#include <unordered_set>
#include <utility>

namespace Phyxel {

namespace {

// Packed column key for the visited set. World coords fit comfortably in 32 bits each.
inline uint64_t colKey(int x, int z) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) |
            static_cast<uint64_t>(static_cast<uint32_t>(z));
}

constexpr int kNX[4] = {1, -1, 0, 0};
constexpr int kNZ[4] = {0, 0, 1, -1};

}  // namespace

void floodBodiesOverGrid(int w, int d, const float* groundTop, const float* bakedLevel,
                         float* outLevel, int maxSteps) {
    if (w <= 0 || d <= 0 || maxSteps < 0 || !groundTop || !bakedLevel || !outLevel) return;
    const size_t n = static_cast<size_t>(w) * static_cast<size_t>(d);

    // Seed: every column the bake already calls wet is its own body, at its own level. Breadth-first
    // from all of them at once means the nearest body claims a contested column, matching the
    // per-column query's "nearest wins" without needing a separate search to find it.
    std::vector<int> frontier, next;
    frontier.reserve(n / 4);
    for (size_t i = 0; i < n; ++i) {
        const float bl = bakedLevel[i];
        if (bl > kNoBody * 0.5f) { outLevel[i] = bl; frontier.push_back(static_cast<int>(i)); }
        else                     { outLevel[i] = kNoBody; }
    }

    // ⚑THE STEP BOUND IS WHAT MAKES THE ANSWER WINDOW-INDEPENDENT, and it is not optional.
    // Without it the flood spreads as far as the SAMPLED GRID happens to reach, so the same column
    // resolves differently depending on the size of the block it was computed in — chunk A and
    // chunk B would then disagree about their shared shoreline and tear at the seam. Bounding
    // propagation to `maxSteps` from a seed makes the result a property of (terrain, bake, maxSteps)
    // alone, which is the definition the design requires. Caught by the first equivalence run: a
    // column 17 steps from water came back wet against a 16-step budget.
    for (int step = 0; step < maxSteps && !frontier.empty(); ++step) {
        next.clear();
        for (const int idx : frontier) {
            const int x = idx % w, z = idx / w;
            const float level = outLevel[idx];
            for (int i = 0; i < 4; ++i) {
                const int nx = x + kNX[i], nz = z + kNZ[i];
                if (nx < 0 || nz < 0 || nx >= w || nz >= d) continue;   // grid edge: see the header
                const size_t ni = static_cast<size_t>(nz) * w + nx;
                if (outLevel[ni] > kNoBody * 0.5f) continue;            // already claimed
                // The submerged-path rule: ground breaking this body's surface blocks the spread,
                // which is what stops a valley beyond a ridge joining a lake at a similar altitude.
                if (groundTop[ni] >= level) continue;
                outLevel[ni] = level;
                next.push_back(static_cast<int>(ni));
            }
        }
        frontier.swap(next);
    }
}

bool fillBasinAt(int worldX, int worldZ, const ColumnTerrain& terrain, int maxSteps,
                 BasinFill& out) {
    if (!terrain.groundY || maxSteps <= 0) return false;

    const float seedGround = static_cast<float>(terrain.groundY(worldX, worldZ)) + 1.0f;
    out = BasinFill{};
    out.groundY = seedGround;

    // Priority-first search. The key of a column is the HIGHEST ground any path from the seed had
    // to cross to reach it — a barrier height. Popping the smallest key first means the first time
    // we get out of the search bound, that key is the LOWEST barrier separating the seed from the
    // outside: the spill. Water can rise to exactly there and no further.
    struct Node {
        float barrier;
        int x, z, steps;
        bool operator>(const Node& o) const { return barrier > o.barrier; }
    };
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> pq;
    std::unordered_set<uint64_t> visited;
    visited.reserve(static_cast<size_t>(maxSteps) * static_cast<size_t>(maxSteps));

    pq.push(Node{seedGround, worldX, worldZ, 0});
    visited.insert(colKey(worldX, worldZ));

    while (!pq.empty()) {
        const Node n = pq.top();
        pq.pop();

        // ESCAPED THE BUDGET — and this is the SUCCESS case, not a failure. Getting `maxSteps` away
        // means crossing whatever ridge separates the seed from the wider world, so `n.barrier` is
        // the cheapest such crossing: the spill height. Water rises to exactly there.
        if (n.steps >= maxSteps) {
            out.level = n.barrier;
            // ⚑A spill at (or barely above) the seed's own ground is NOT a container. Water would
            // simply run away. Flat ground and open slopes land here — they hold nothing, and no
            // caller can pretend otherwise, because there is no level parameter to pretend with.
            return out.level - seedGround >= kMinSpanDepth;
        }

        for (int i = 0; i < 4; ++i) {
            const int nx = n.x + kNX[i], nz = n.z + kNZ[i];
            if (!visited.insert(colKey(nx, nz)).second) continue;
            const float g = static_cast<float>(terrain.groundY(nx, nz)) + 1.0f;
            pq.push(Node{std::max(n.barrier, g), nx, nz, n.steps + 1});
        }
    }
    // The frontier ran dry without ever getting `maxSteps` away. With four neighbours per column
    // that cannot happen on real terrain, so this is a degenerate/stub-callback case: report it as
    // unresolved rather than inventing a level.
    out.unresolved = true;
    return false;
}

float connectedBodyLevel(int worldX, int worldZ, const ColumnTerrain& terrain, int maxSteps) {
    if (!terrain.groundY || !terrain.bakedLevel || maxSteps < 0) return kNoBody;

    // A column the bake already calls wet needs no search — it IS the body.
    const float own = terrain.bakedLevel(worldX, worldZ);
    if (own > kNoBody * 0.5f) return own;

    // PASS 1 — find the candidate surface: the level of the nearest baked-wet column, searched
    // without regard to terrain. Nearest wins, so a column is banked against the body it actually
    // adjoins rather than a higher one further off. Ring-by-ring keeps "nearest" meaningful.
    float level = kNoBody;
    for (int r = 1; r <= maxSteps && level <= kNoBody * 0.5f; ++r) {
        for (int d = -r; d <= r && level <= kNoBody * 0.5f; ++d) {
            const int pts[4][2] = {{worldX + d, worldZ - r}, {worldX + d, worldZ + r},
                                   {worldX - r, worldZ + d}, {worldX + r, worldZ + d}};
            for (const auto& p : pts) {
                const float l = terrain.bakedLevel(p[0], p[1]);
                if (l > kNoBody * 0.5f) { level = l; break; }
            }
        }
    }
    if (level <= kNoBody * 0.5f) return kNoBody;          // no water within reach

    // This column must itself lie under that surface, or it is simply shore.
    if (static_cast<float>(terrain.groundY(worldX, worldZ)) + 1.0f >= level) return kNoBody;

    // PASS 2 — is there a SUBMERGED path from here to baked water? Every step must lie below the
    // same surface; a ridge breaking the surface blocks it, which is what stops a valley on the far
    // side of a divide from being flooded just because it happens to sit at a similar altitude.
    // ⚑A HASH SET, NOT A LINEAR SCAN. The visited test runs for every neighbour of every frontier
    // column, so scanning a vector made the flood O(visited^2) — at a 48-step budget that is
    // millions of comparisons for a single column, and this function is called PER COLUMN. The
    // terrain samples are meant to be the cost here; bookkeeping should not outweigh them.
    std::unordered_set<uint64_t> visited;
    visited.reserve(static_cast<size_t>(maxSteps) * static_cast<size_t>(maxSteps));
    std::vector<std::pair<int, int>> frontier{{worldX, worldZ}}, next;
    visited.insert(colKey(worldX, worldZ));

    for (int step = 0; step < maxSteps && !frontier.empty(); ++step) {
        next.clear();
        for (const auto& [cx, cz] : frontier)
            for (int i = 0; i < 4; ++i) {
                const int nx = cx + kNX[i], nz = cz + kNZ[i];
                if (!visited.insert(colKey(nx, nz)).second) continue;   // already seen
                // Reaching baked water through submerged ground: this column is part of that body.
                if (terrain.bakedLevel(nx, nz) > kNoBody * 0.5f) return level;
                if (static_cast<float>(terrain.groundY(nx, nz)) + 1.0f >= level) continue;  // breaks surface
                next.emplace_back(nx, nz);
            }
        frontier.swap(next);
    }
    return kNoBody;   // submerged, but not linked to any body within the bound
}

bool buildOpenWaterSpan(int surfaceY, float bodyLevel, WaterSpan& out) {
    // A non-finite level is a bad bake, not a lake. Reject rather than propagate a NaN into world
    // data, where it would poison every consumer downstream.
    if (!std::isfinite(bodyLevel)) return false;

    // THE containment test, and the only one there is: does the water surface stand above this
    // column's real ground? Terrain at or above the level means dry land — an island, a bank, a
    // ridge the coarse bake did not know about. It gets no water, whatever the bake claims.
    // ⚑OVERFLOW BOUND. `surfaceY + 1` below is signed-int arithmetic, and at INT_MAX it wraps to
    // INT_MIN — producing a "valid" span whose bottom sits billions of units BELOW the terrain it
    // claims to rest on. That is a genuine arithmetic path to floating water, found by sweeping the
    // integer extremes (solution-auditor, 2026-08-03). Unreachable from real terrain (peaks are
    // ~384 above sea level), but the invariant is stated absolutely, so it is enforced absolutely.
    if (surfaceY >= std::numeric_limits<int>::max() - 1) return false;

    const float ground = static_cast<float>(surfaceY) + 1.0f;   // top face of the solid voxel

    // ONE test, not two. An earlier version had a separate `bodyLevel <= ground` containment check
    // in front of this, which READ as two independent guards but was strictly redundant: depth
    // > kMinSpanDepth already implies bodyLevel > ground. Mutation testing proved it — deleting the
    // containment guard alone turned NOTHING red, because this line rejects every case it would
    // have. Two overlapping guards where only one is load-bearing is worse than one honest guard:
    // it invites the belief that containment is independently pinned when it is not.
    if (bodyLevel - ground < kMinSpanDepth) return false;

    // Bottom is DERIVED from the terrain, never passed in independently. This is what makes
    // floating water unrepresentable rather than merely detectable.
    out.bottomY = surfaceY + 1;
    out.topY    = bodyLevel;
    return true;
}

}  // namespace Phyxel
