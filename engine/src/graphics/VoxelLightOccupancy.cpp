#include "graphics/VoxelLightOccupancy.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "physics/VoxelOccupancyGrid.h"

namespace Phyxel {
namespace Graphics {

int ChunkLightOccupancy::mixedSlot(int cubeIdx) const {
    const auto it = std::lower_bound(mixedCubeIdx.begin(), mixedCubeIdx.end(),
                                     static_cast<uint32_t>(cubeIdx));
    if (it == mixedCubeIdx.end() || *it != static_cast<uint32_t>(cubeIdx)) return -1;
    return static_cast<int>(it - mixedCubeIdx.begin());
}

bool ChunkLightOccupancy::solidAtMicro(const glm::ivec3& microLocal) const {
    if (microLocal.x < 0 || microLocal.y < 0 || microLocal.z < 0) return false;
    if (microLocal.x >= kChunk * kMicroPerAxis || microLocal.y >= kChunk * kMicroPerAxis ||
        microLocal.z >= kChunk * kMicroPerAxis) return false;

    const glm::ivec3 cube{microLocal.x / kMicroPerAxis, microLocal.y / kMicroPerAxis,
                          microLocal.z / kMicroPerAxis};
    const int ci = cubeIndex(cube);

    // Fully solid cube: one bit test, and this is the common case for terrain.
    if (cubeIsSolid(ci)) return true;
    if (!cubeIsMixed(ci)) return false;

    const int slot = mixedSlot(ci);
    if (slot < 0) return false;                     // mixed bit with no detail = empty, not solid

    const glm::ivec3 inCube{microLocal.x % kMicroPerAxis, microLocal.y % kMicroPerAxis,
                            microLocal.z % kMicroPerAxis};
    const int bit = microIndex(inCube);
    const size_t base = static_cast<size_t>(slot) * kMicroWordsPerCube;
    return (microWords[base + static_cast<size_t>(bit >> 5)] >> (bit & 31)) & 1u;
}

ChunkLightOccupancy buildLightOccupancy(const Physics::VoxelOccupancyGrid& grid) {
    ChunkLightOccupancy out;

    auto setBit = [](std::array<uint32_t, ChunkLightOccupancy::kCubeWords>& words, int idx) {
        words[static_cast<size_t>(idx) >> 5] |= (1u << (idx & 31));
    };

    std::array<uint32_t, ChunkLightOccupancy::kMicroWordsPerCube> micro{};
    auto setMicro = [&micro](int bit) {
        micro[static_cast<size_t>(bit) >> 5] |= (1u << (bit & 31));
    };

    for (int x = 0; x < ChunkLightOccupancy::kChunk; ++x)
    for (int y = 0; y < ChunkLightOccupancy::kChunk; ++y)
    for (int z = 0; z < ChunkLightOccupancy::kChunk; ++z) {
        const glm::ivec3 lp{x, y, z};
        // The cube bit is "has content at all" — a SUBDIVIDED cube still has it set. Matching
        // queryAABB: `if (!m_cubes.test(cidx)) continue;` comes before the subdivision check.
        if (!grid.isCubeFilled(lp)) continue;
        const int ci = ChunkLightOccupancy::cubeIndex(lp);

        if (!grid.isSubdivided(lp)) {
            setBit(out.solid, ci);
            continue;
        }

        micro.fill(0u);
        bool any = false;
        for (int sx = 0; sx < 3; ++sx)
        for (int sy = 0; sy < 3; ++sy)
        for (int sz = 0; sz < 3; ++sz) {
            const glm::ivec3 sp{sx, sy, sz};
            if (!grid.isSubcubeFilled(lp, sp)) continue;

            if (!grid.isSubcubeSubdivided(lp, sp)) {
                // Whole subcube solid -> its 27 micro cells.
                for (int mx = 0; mx < 3; ++mx)
                for (int my = 0; my < 3; ++my)
                for (int mz = 0; mz < 3; ++mz) {
                    setMicro(ChunkLightOccupancy::microIndex(
                        {sx * 3 + mx, sy * 3 + my, sz * 3 + mz}));
                    any = true;
                }
                continue;
            }
            for (int mx = 0; mx < 3; ++mx)
            for (int my = 0; my < 3; ++my)
            for (int mz = 0; mz < 3; ++mz) {
                if (!grid.isMicrocubeFilled(lp, sp, {mx, my, mz})) continue;
                setMicro(ChunkLightOccupancy::microIndex(
                    {sx * 3 + mx, sy * 3 + my, sz * 3 + mz}));
                any = true;
            }
        }
        if (!any) continue;   // subdivided but empty — carry nothing rather than a zero block

        setBit(out.mixed, ci);
        out.mixedCubeIdx.push_back(static_cast<uint32_t>(ci));
        out.microWords.insert(out.microWords.end(), micro.begin(), micro.end());
    }

    // The x/y/z loop order above already produces ascending cube indices (z fastest, x slowest,
    // matching z + y*32 + x*1024), so mixedCubeIdx is sorted by construction — which the shader's
    // binary search depends on. Asserted in the tests rather than assumed here.
    return out;
}

// ---------------------------------------------------------------------------------------------
// Packed pool
// ---------------------------------------------------------------------------------------------

namespace {
/// Floor-divide, because world coordinates go negative and C++ integer division truncates toward
/// zero — which would fold -1 and 0 into the same chunk and silently corrupt the directory for
/// every chunk west or south of the origin.
int floorDiv(int a, int b) {
    const int q = a / b, r = a % b;
    return (r != 0 && ((r < 0) != (b < 0))) ? q - 1 : q;
}
}  // namespace

glm::ivec3 PackedOccupancyPool::boxMinChunkFor(const glm::vec3& centreWorld) {
    // Chunk containing the centre, then step back by half the box on each axis. floorDiv again,
    // because a camera west or below the origin has negative coordinates and truncation would fold
    // the two chunks either side of zero together.
    const glm::ivec3 c{floorDiv(static_cast<int>(std::floor(centreWorld.x)), ChunkLightOccupancy::kChunk),
                       floorDiv(static_cast<int>(std::floor(centreWorld.y)), ChunkLightOccupancy::kChunk),
                       floorDiv(static_cast<int>(std::floor(centreWorld.z)), ChunkLightOccupancy::kChunk)};
    return {c.x - kDirChunksX / 2, c.y - kDirChunksY / 2, c.z - kDirChunksZ / 2};
}

int PackedOccupancyPool::directoryIndex(const glm::ivec3& chunkWorldOrigin,
                                        const glm::ivec3& boxMinChunk) {
    const int cx = floorDiv(chunkWorldOrigin.x, ChunkLightOccupancy::kChunk) - boxMinChunk.x;
    const int cy = floorDiv(chunkWorldOrigin.y, ChunkLightOccupancy::kChunk) - boxMinChunk.y;
    const int cz = floorDiv(chunkWorldOrigin.z, ChunkLightOccupancy::kChunk) - boxMinChunk.z;
    if (cx < 0 || cx >= kDirChunksX || cy < 0 || cy >= kDirChunksY || cz < 0 || cz >= kDirChunksZ)
        return -1;
    return cx + cy * kDirChunksX + cz * kDirChunksX * kDirChunksY;
}

int PackedOccupancyPool::directoryIndexForMicro(const glm::ivec3& worldMicro,
                                                const glm::ivec3& boxMinChunk) {
    const int mpc = ChunkLightOccupancy::kChunk * ChunkLightOccupancy::kMicroPerAxis;   // 288
    const glm::ivec3 origin{floorDiv(worldMicro.x, mpc) * ChunkLightOccupancy::kChunk,
                            floorDiv(worldMicro.y, mpc) * ChunkLightOccupancy::kChunk,
                            floorDiv(worldMicro.z, mpc) * ChunkLightOccupancy::kChunk};
    return directoryIndex(origin, boxMinChunk);
}

glm::ivec3 PackedOccupancyPool::chunkOriginForSlot(int slot, const glm::ivec3& boxMinChunk) {
    const int cx = slot % kDirChunksX;
    const int cy = (slot / kDirChunksX) % kDirChunksY;
    const int cz = slot / (kDirChunksX * kDirChunksY);
    return {(boxMinChunk.x + cx) * ChunkLightOccupancy::kChunk,
            (boxMinChunk.y + cy) * ChunkLightOccupancy::kChunk,
            (boxMinChunk.z + cz) * ChunkLightOccupancy::kChunk};
}

PackedOccupancyPool packOccupancyPool(
    const std::vector<std::pair<glm::ivec3, ChunkLightOccupancy>>& chunks,
    const glm::ivec3& boxMinChunk) {
    PackedOccupancyPool out;
    out.boxMinChunk = boxMinChunk;
    out.directory.assign(PackedOccupancyPool::kDirEntries, PackedOccupancyPool::kNoChunk);

    for (const auto& [origin, blob] : chunks) {
        const int slot = PackedOccupancyPool::directoryIndex(origin, boxMinChunk);
        if (slot < 0) continue;   // outside the covered box — reports "not solid", never wrong data

        out.directory[static_cast<size_t>(slot)] = static_cast<uint32_t>(out.pool.size());

        // Blob layout, word for word — the shader reads it in this order:
        //   [0]                      mixed-cube count N
        //   [1 .. 1024]              solid bitfield
        //   [1025 .. 2048]           mixed bitfield
        //   [2049 .. 2049+N)         mixed cube indices, ASCENDING (binary-searched)
        //   [.. + N*23)              micro masks, 23 words per mixed cube
        out.pool.push_back(static_cast<uint32_t>(blob.mixedCubeIdx.size()));
        out.pool.insert(out.pool.end(), blob.solid.begin(), blob.solid.end());
        out.pool.insert(out.pool.end(), blob.mixed.begin(), blob.mixed.end());
        out.pool.insert(out.pool.end(), blob.mixedCubeIdx.begin(), blob.mixedCubeIdx.end());
        out.pool.insert(out.pool.end(), blob.microWords.begin(), blob.microWords.end());
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// THE TRAVERSAL. An Amanatides & Woo DDA in MICRO space: it visits every micro cell the segment
// passes through, in order, and cannot skip one.
//
// This replaced a fixed-step march, which was measured to be structurally wrong rather than merely
// mistuned (docs/UnifiedLightingPlan.md D0): a fixed step is only safe when it is smaller than the
// thinnest feature, the thinnest feature is 1/9 u, and sampling that finely over a 24 u sky ray
// costs ~432 samples per ray. The compromise that made it affordable — coarsening beyond 3 u — is
// exactly what made a 1-micro roof invisible, so a sealed room read 0.536 sky instead of 0.
//
// A DDA has no such trade: cost is the number of cells actually crossed, and correctness does not
// depend on feature thickness at all.
// ---------------------------------------------------------------------------------------------
namespace {

/// Walk micro cells from `from` to `to` (absolute world units). Returns true if any is solid.
/// `skipCells` ignores that many cells at the start — how a surface avoids shadowing itself.
bool ddaHitsSolid(const PackedOccupancyPool& packed, const glm::vec3& from, const glm::vec3& to,
                  int skipCells, int maxCells, int* cellsOut, glm::ivec3* firstHitOut) {
    const glm::vec3 a = from * 9.0f, b = to * 9.0f;          // micro space
    const glm::vec3 d = b - a;
    const float len = glm::length(d);
    if (cellsOut) *cellsOut = 0;
    if (len < 1e-6f) return false;
    const glm::vec3 dir = d / len;

    glm::ivec3 cell{static_cast<int>(std::floor(a.x)), static_cast<int>(std::floor(a.y)),
                    static_cast<int>(std::floor(a.z))};
    const glm::ivec3 last{static_cast<int>(std::floor(b.x)), static_cast<int>(std::floor(b.y)),
                          static_cast<int>(std::floor(b.z))};

    glm::ivec3 step;
    glm::vec3 tMax, tDelta;
    for (int i = 0; i < 3; ++i) {
        if (dir[i] > 1e-9f) {
            step[i] = 1;
            tMax[i] = (static_cast<float>(cell[i] + 1) - a[i]) / dir[i];
            tDelta[i] = 1.0f / dir[i];
        } else if (dir[i] < -1e-9f) {
            step[i] = -1;
            tMax[i] = (a[i] - static_cast<float>(cell[i])) / -dir[i];
            tDelta[i] = 1.0f / -dir[i];
        } else {
            step[i] = 0;
            tMax[i] = std::numeric_limits<float>::max();
            tDelta[i] = std::numeric_limits<float>::max();
        }
    }

    for (int n = 0; n < maxCells; ++n) {
        if (n >= skipCells && packedPoolSolidAt(packed, cell)) {
            if (cellsOut) *cellsOut = n;
            if (firstHitOut) *firstHitOut = cell;
            return true;
        }
        if (cell == last) { if (cellsOut) *cellsOut = n; return false; }

        // Advance across the nearest cell boundary.
        if (tMax.x < tMax.y) {
            if (tMax.x < tMax.z) { cell.x += step.x; tMax.x += tDelta.x; }
            else                 { cell.z += step.z; tMax.z += tDelta.z; }
        } else {
            if (tMax.y < tMax.z) { cell.y += step.y; tMax.y += tDelta.y; }
            else                 { cell.z += step.z; tMax.z += tDelta.z; }
        }
        if (tMax.x > len && tMax.y > len && tMax.z > len) {
            if (cellsOut) *cellsOut = n;
            return false;
        }
    }
    if (cellsOut) *cellsOut = maxCells;
    return false;
}

/// World-unit length of the contiguous SOLID run containing `lightWorld`, measured outward along
/// `dirOut`. 0.0 when the light sits in air. Mirrors occupancy.glsl's phxEmitterRunLength exactly —
/// see it for why the emitter's body is measured rather than assumed to be a fixed size.
float emitterRunLength(const PackedOccupancyPool& packed, const glm::vec3& lightWorld,
                       const glm::vec3& dirOut, int maxCells) {
    const glm::vec3 a = lightWorld * 9.0f;   // micro space
    glm::ivec3 cell{static_cast<int>(std::floor(a.x)), static_cast<int>(std::floor(a.y)),
                    static_cast<int>(std::floor(a.z))};

    glm::ivec3 step;
    glm::vec3 tMax, tDelta;
    for (int i = 0; i < 3; ++i) {
        if (dirOut[i] > 1e-9f) {
            step[i] = 1;
            tMax[i] = (static_cast<float>(cell[i] + 1) - a[i]) / dirOut[i];
            tDelta[i] = 1.0f / dirOut[i];
        } else if (dirOut[i] < -1e-9f) {
            step[i] = -1;
            tMax[i] = (a[i] - static_cast<float>(cell[i])) / -dirOut[i];
            tDelta[i] = 1.0f / -dirOut[i];
        } else {
            step[i] = 0;
            tMax[i] = std::numeric_limits<float>::max();
            tDelta[i] = std::numeric_limits<float>::max();
        }
    }

    float t = 0.0f;   // micro units travelled so far
    for (int n = 0; n < maxCells; ++n) {
        if (!packedPoolSolidAt(packed, cell)) return t / 9.0f;   // reached air: the run ends here
        if (tMax.x < tMax.y) {
            if (tMax.x < tMax.z) { t = tMax.x; cell.x += step.x; tMax.x += tDelta.x; }
            else                 { t = tMax.z; cell.z += step.z; tMax.z += tDelta.z; }
        } else {
            if (tMax.y < tMax.z) { t = tMax.y; cell.y += step.y; tMax.y += tDelta.y; }
            else                 { t = tMax.z; cell.z += step.z; tMax.z += tDelta.z; }
        }
    }
    return t / 9.0f;
}

}  // namespace

LightVisibility packedPoolLightVisibility(const PackedOccupancyPool& packed,
                                          const glm::vec3& surfaceWorld,
                                          const glm::vec3& geomNormal,
                                          const glm::vec3& lightWorld,
                                          int maxSteps) {
    LightVisibility out;

    const glm::vec3 start = surfaceWorld + geomNormal * (2.0f / 9.0f);
    const glm::vec3 delta = lightWorld - start;
    const float dist = glm::length(delta);
    if (dist < 1e-4f) return out;
    const glm::vec3 dir = delta / dist;

    // Stop short of the light by the MEASURED extent of the emitter's own body — must stay identical
    // to occupancy.glsl's phxLightVisibility, which carries the full reasoning. Briefly: an emissive
    // voxel is solid with its light at the cell centre, so it occludes its own light; stopping a flat
    // half voxel short fixed that and let ANY light within 0.5 u of a wall shine through the wall (a
    // hearth's flame sits in a firebox cut into masonry, and lit the lawn outside the house).
    // Measuring the run excludes exactly the emitter and nothing else.
    const float runEnd = emitterRunLength(packed, lightWorld, -dir, /*maxCells=*/32);
    const glm::vec3 target = start + dir * std::max(dist - runEnd - (0.1f / 9.0f), 0.0f);

    int cells = 0;
    glm::ivec3 hit{0};
    const bool blocked = ddaHitsSolid(packed, start, target, /*skipCells=*/0, maxSteps,
                                      &cells, &hit);
    out.steps = cells;
    out.cappedOut = cells >= maxSteps;
    if (blocked) {
        out.visible = false;
        out.firstHitMicro = hit;
    }
    return out;
}

namespace {
/// Fixed hemisphere directions in TANGENT space (z = surface normal): one straight up, then two
/// rings. Deterministic and shared with the shader — a random or per-frame-jittered set would make
/// the result noisy and impossible to compare between captures, which is exactly what the M3 gates
/// need to do.
const glm::vec3 kSkyDirsTangent[9] = {
    { 0.000f,  0.000f, 1.000f},
    { 0.500f,  0.000f, 0.866f}, {-0.500f,  0.000f, 0.866f},
    { 0.000f,  0.500f, 0.866f}, { 0.000f, -0.500f, 0.866f},
    { 0.612f,  0.612f, 0.500f}, {-0.612f,  0.612f, 0.500f},
    { 0.612f, -0.612f, 0.500f}, {-0.612f, -0.612f, 0.500f},
};
}  // namespace

float packedPoolSkyVisibility(const PackedOccupancyPool& packed, const glm::vec3& surfaceWorld,
                              const glm::vec3& geomNormal, float reach, int rays) {
    const glm::vec3 Ng = glm::normalize(geomNormal);
    // Stable tangent basis. Voxel faces are axis-aligned, so this is exact rather than approximate.
    const glm::vec3 up = (std::abs(Ng.y) < 0.999f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    const glm::vec3 T = glm::normalize(glm::cross(up, Ng));
    const glm::vec3 B = glm::cross(Ng, T);

    const glm::vec3 start = surfaceWorld + Ng * (2.0f / 9.0f);
    const int n = std::min(rays, 9);

    // TWO-RATE MARCH. Measured why: at a uniform 1/9 step a 96-step ray reaches only ~10.7 u, and
    // a diagonal ray inside a 9x7x9 SEALED room then ran out of reach without hitting anything and
    // was counted as sky — a sealed room reading 0.077 instead of 0. Reach, not resolution, is what
    // decides whether a room reads as enclosed, so the march keeps micro resolution close in (where
    // 2-micro walls and ledges live) and coarsens beyond it to buy the range a room needs.
    // DDA per ray — exact regardless of how thin the roof is. The previous two-rate march coarsened
    // beyond 3 u and therefore stepped over 1-micro roofs, which is what made a sealed room read
    // 0.536 sky instead of 0 (D0). Cost is now cells-crossed, capped.
    const int kMaxCells = static_cast<int>(reach * 9.0f) * 2;

    float lit = 0.0f, total = 0.0f;
    for (int r = 0; r < n; ++r) {
        const glm::vec3 d = kSkyDirsTangent[r];
        const glm::vec3 dir = glm::normalize(T * d.x + B * d.y + Ng * d.z);
        const float w = std::max(0.0f, glm::dot(dir, Ng));   // cosine weight
        total += w;
        if (!ddaHitsSolid(packed, start, start + dir * reach, 0, kMaxCells, nullptr, nullptr))
            lit += w;
    }
    return total > 0.0f ? lit / total : 1.0f;
}

size_t blobWords(const ChunkLightOccupancy& blob) {
    // 1 count word + solid bitfield + mixed bitfield + one index per mixed cube + its micro masks.
    // Kept beside packOccupancyPool so the two cannot drift; a smaller number here would let a
    // blob be admitted that does not fit, and the tail would be truncated.
    return 1 + static_cast<size_t>(ChunkLightOccupancy::kCubeWords) * 2
             + blob.mixedCubeIdx.size() + blob.microWords.size();
}

std::vector<std::pair<glm::ivec3, ChunkLightOccupancy>> selectChunksThatFit(
    const std::vector<std::pair<glm::ivec3, ChunkLightOccupancy>>& chunks,
    size_t capacityWords, size_t& droppedOut) {
    std::vector<std::pair<glm::ivec3, ChunkLightOccupancy>> kept;
    kept.reserve(chunks.size());
    size_t used = 0;
    droppedOut = 0;
    for (const auto& entry : chunks) {
        const size_t w = blobWords(entry.second);
        // Strictly whole chunks: if it does not fit, skip it and keep trying the rest — a later
        // chunk may be small enough. Never admit a partial blob.
        if (used + w > capacityWords) { ++droppedOut; continue; }
        used += w;
        kept.push_back(entry);
    }
    return kept;
}

bool packedPoolSolidAt(const PackedOccupancyPool& packed, const glm::ivec3& worldMicro) {
    if (packed.directory.empty()) return false;   // never packed — no occluders, not garbage
    const int slot = PackedOccupancyPool::directoryIndexForMicro(worldMicro, packed.boxMinChunk);
    if (slot < 0) return false;
    const uint32_t base = packed.directory[static_cast<size_t>(slot)];
    if (base == PackedOccupancyPool::kNoChunk) return false;

    constexpr int W = ChunkLightOccupancy::kCubeWords;              // 1024
    constexpr int MW = ChunkLightOccupancy::kMicroWordsPerCube;     // 23
    const int mpc = ChunkLightOccupancy::kChunk * ChunkLightOccupancy::kMicroPerAxis;

    // Chunk-local micro coords. Positive modulo, for the same reason floorDiv exists.
    auto mod = [mpc](int v) { const int m = v % mpc; return m < 0 ? m + mpc : m; };
    const glm::ivec3 local{mod(worldMicro.x), mod(worldMicro.y), mod(worldMicro.z)};

    const glm::ivec3 cube{local.x / ChunkLightOccupancy::kMicroPerAxis,
                          local.y / ChunkLightOccupancy::kMicroPerAxis,
                          local.z / ChunkLightOccupancy::kMicroPerAxis};
    const int ci = ChunkLightOccupancy::cubeIndex(cube);

    const size_t solidBase = base + 1;
    if ((packed.pool[solidBase + (static_cast<size_t>(ci) >> 5)] >> (ci & 31)) & 1u) return true;

    const size_t mixedBase = solidBase + W;
    if (!((packed.pool[mixedBase + (static_cast<size_t>(ci) >> 5)] >> (ci & 31)) & 1u)) return false;

    // Binary search the ascending mixed-cube index list.
    const uint32_t n = packed.pool[base];
    const size_t idxBase = mixedBase + W;
    uint32_t lo = 0, hi = n;
    while (lo < hi) {
        const uint32_t mid = (lo + hi) >> 1;
        const uint32_t v = packed.pool[idxBase + mid];
        if (v < static_cast<uint32_t>(ci)) lo = mid + 1;
        else hi = mid;
    }
    if (lo >= n || packed.pool[idxBase + lo] != static_cast<uint32_t>(ci)) return false;

    const glm::ivec3 inCube{local.x % ChunkLightOccupancy::kMicroPerAxis,
                            local.y % ChunkLightOccupancy::kMicroPerAxis,
                            local.z % ChunkLightOccupancy::kMicroPerAxis};
    const int bit = ChunkLightOccupancy::microIndex(inCube);
    const size_t microBase = idxBase + n + static_cast<size_t>(lo) * MW;
    return (packed.pool[microBase + static_cast<size_t>(bit >> 5)] >> (bit & 31)) & 1u;
}

CubeOccupancy packedPoolCubeOccupancy(const PackedOccupancyPool& packed,
                                      const glm::ivec3& worldCube) {
    if (packed.directory.empty()) return CubeOccupancy::Empty;   // never packed — no occluders

    // Address through the micro path so there is ONE addressing implementation to be wrong.
    // A cube's min corner in micro units lands inside that cube by construction.
    const glm::ivec3 worldMicro = worldCube * ChunkLightOccupancy::kMicroPerAxis;
    const int slot = PackedOccupancyPool::directoryIndexForMicro(worldMicro, packed.boxMinChunk);
    if (slot < 0) return CubeOccupancy::Empty;                   // outside the resident box
    const uint32_t base = packed.directory[static_cast<size_t>(slot)];
    if (base == PackedOccupancyPool::kNoChunk) return CubeOccupancy::Empty;

    constexpr int W = ChunkLightOccupancy::kCubeWords;
    const int mpc = ChunkLightOccupancy::kChunk * ChunkLightOccupancy::kMicroPerAxis;
    auto mod = [mpc](int v) { const int m = v % mpc; return m < 0 ? m + mpc : m; };
    const glm::ivec3 local{mod(worldMicro.x), mod(worldMicro.y), mod(worldMicro.z)};
    const glm::ivec3 cube{local.x / ChunkLightOccupancy::kMicroPerAxis,
                          local.y / ChunkLightOccupancy::kMicroPerAxis,
                          local.z / ChunkLightOccupancy::kMicroPerAxis};
    const int ci = ChunkLightOccupancy::cubeIndex(cube);

    const size_t solidBase = base + 1;
    if ((packed.pool[solidBase + (static_cast<size_t>(ci) >> 5)] >> (ci & 31)) & 1u)
        return CubeOccupancy::Solid;

    const size_t mixedBase = solidBase + W;
    if ((packed.pool[mixedBase + (static_cast<size_t>(ci) >> 5)] >> (ci & 31)) & 1u)
        return CubeOccupancy::Mixed;

    return CubeOccupancy::Empty;
}

}  // namespace Graphics
}  // namespace Phyxel
