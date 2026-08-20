#include "graphics/FarTerrainMesher.h"
#include "core/WorldForgePlan.h"
#include "graphics/TreeSpeciesTable.h"
#include "core/WorldGenerator.h"

#include <algorithm>
#include <unordered_map>

namespace Phyxel {
namespace Graphics {

FarTerrainMesher::FarTerrainMesher(std::unique_ptr<WorldGenerator> generator,
                                   FarMaterialResolver resolver)
    : m_generator(std::move(generator)), m_resolveTex(std::move(resolver)) {}

FarTerrainMesher::~FarTerrainMesher() = default;

int FarTerrainMesher::quantizeTop(int surfaceY, int step) {
    // Quantize the real surface plane (surfaceY + 1) DOWN to a multiple of step.
    const int plane = surfaceY + 1;
    const int d = (plane >= 0) ? (plane / step) : -((-plane + step - 1) / step);
    return d * step;
}

namespace {

// Emits quads into a FarTileMesh: 4 vertices + 6 indices each, CCW seen from the
// face normal side (matches the test contract: each index sextet references only
// its own quad's vertices).
struct QuadEmitter {
    FarTileMesh& mesh;
    bool any = false;

    explicit QuadEmitter(FarTileMesh& m) : mesh(m) {}

    void emit(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
              const glm::vec3& v3, uint32_t faceID, uint16_t tex) {
        const uint32_t base = uint32_t(mesh.vertices.size());
        const uint32_t packed = packFarVertex(tex, faceID);
        for (const glm::vec3& p : {v0, v1, v2, v3}) {
            mesh.vertices.push_back({p, packed});
            if (!any) { mesh.minY = mesh.maxY = p.y; any = true; }
            else { mesh.minY = std::min(mesh.minY, p.y); mesh.maxY = std::max(mesh.maxY, p.y); }
        }
        mesh.indices.insert(mesh.indices.end(),
                            {base, base + 1, base + 2, base, base + 2, base + 3});
    }

    // Vertical wall on an X plane (constant x), spanning z0..z1 * yLo..yHi.
    // faceID 2 = +X normal, 3 = -X.
    void wallX(float x, float z0, float z1, float yLo, float yHi, uint32_t faceID, uint16_t tex) {
        if (faceID == 2)
            emit({x, yLo, z0}, {x, yHi, z0}, {x, yHi, z1}, {x, yLo, z1}, faceID, tex);
        else
            emit({x, yLo, z1}, {x, yHi, z1}, {x, yHi, z0}, {x, yLo, z0}, faceID, tex);
    }

    // Vertical wall on a Z plane (constant z), spanning x0..x1 * yLo..yHi.
    // faceID 0 = +Z normal, 1 = -Z.
    void wallZ(float z, float x0, float x1, float yLo, float yHi, uint32_t faceID, uint16_t tex) {
        if (faceID == 0)
            emit({x1, yLo, z}, {x1, yHi, z}, {x0, yHi, z}, {x0, yLo, z}, faceID, tex);
        else
            emit({x0, yLo, z}, {x0, yHi, z}, {x1, yHi, z}, {x1, yLo, z}, faceID, tex);
    }

    // Horizontal top face (+Y, faceID 4) covering x0..x1 * z0..z1 at height y.
    void top(float x0, float x1, float z0, float z1, float y, uint16_t tex) {
        emit({x0, y, z0}, {x0, y, z1}, {x1, y, z1}, {x1, y, z0}, 4u, tex);
    }
};

} // namespace

FarTileMesh FarTerrainMesher::buildTile(const FarTileKey& key, int step) {
    const int N = kColumns;
    const int G = N + 2;  // sampled grid side: columns -1..N per axis

    FarTileMesh mesh;
    mesh.ring = key.ring;
    mesh.step = step;
    mesh.tileSize = N * step;
    mesh.originXZ = glm::ivec2(key.x * mesh.tileSize, key.z * mesh.tileSize);

    // ---- Sample the column grid (min-corner world coordinate per column). ----
    std::vector<int> qGrid(size_t(G) * G);
    std::vector<int> biomeGrid(size_t(G) * G);
    std::vector<std::string> topMat(size_t(N) * N);
    auto q = [&](int i, int j) -> int& { return qGrid[size_t(i + 1) + size_t(j + 1) * G]; };
    auto biomeAt = [&](int i, int j) -> int& { return biomeGrid[size_t(i + 1) + size_t(j + 1) * G]; };

    // WorldForge far roads (docs/WorldForge.md): the min-corner point sample misses a
    // 5-6 u road in a coarse cell (step 8 rendered dashes, step 16 mostly lost it —
    // FarTerrainMesherTest.RoadsShowInFarTiles, red-first). Widen the acceptance to
    // halfWidth + step/2 at the CELL CENTRE: any cell the road passes through reads as
    // road, so the far ribbon stays continuous at every ring (a 1-cell-wide line is the
    // correct far-map thickness). Near columns are untouched — this is far-tile-only.
    const WorldForgePlan* roadPlan = m_generator->worldForge();
    for (int j = -1; j <= N; ++j) {
        for (int i = -1; i <= N; ++i) {
            WorldGenerator::ColumnSample col =
                m_generator->sampleSurface(mesh.originXZ.x + i * step, mesh.originXZ.y + j * step);
            q(i, j) = quantizeTop(col.surfaceY, step);
            biomeAt(i, j) = col.biomeIndex;
            if (i >= 0 && i < N && j >= 0 && j < N) {
                if (roadPlan && col.roadClass == 0 && col.riverOrder == 0 &&
                    col.bridgeDeckY == INT_MIN) {
                    const auto hit = roadPlan->roadAt(
                        mesh.originXZ.x + i * step + step * 0.5f,
                        mesh.originXZ.y + j * step + step * 0.5f);
                    if (hit.cls > 0 &&
                        hit.dist <= WorldForgePlan::roadHalfWidth(hit.cls) + step * 0.5f)
                        col.surfaceMat = WorldForgePlan::roadMaterial(hit.cls);
                }
                topMat[size_t(i) + size_t(j) * N] = std::move(col.surfaceMat);
            }
        }
    }

    // ---- Texture resolution (memoized; a tile touches only a few materials). ----
    std::unordered_map<std::string, uint16_t> texMemo[6];
    auto tex = [&](const std::string& mat, int faceID) -> uint16_t {
        auto& memo = texMemo[faceID];
        auto it = memo.find(mat);
        if (it != memo.end()) return it->second;
        const uint16_t t = m_resolveTex ? m_resolveTex(mat, faceID) : 0;
        memo.emplace(mat, t);
        return t;
    };

    const std::vector<WorldGenerator::Biome>& biomes = m_generator->getBiomes();
    static const std::string kFallbackMat = "Stone";
    // Walls read the biome's below-surface materials. The old rule mirrored
    // materialForColumn (< 4 → subsurface, else deep), but far-tile walls are
    // QUANTIZATION steps — at ring step 4/8 every rolling hillside produced ≥4-tall
    // walls, painting the whole far field with the grey deep material while the same
    // terrain up close reads as tan dirt (user-reported jarring color band at the
    // real-chunk boundary). Quantization steps of ordinary relief keep the
    // subsurface tone; only genuinely tall cliff faces (> 12) read as deep rock.
    auto wallMatFor = [&](int biomeIdx, int height) -> const std::string& {
        if (biomes.empty()) return kFallbackMat;
        const WorldGenerator::Biome& b =
            biomes[size_t(std::clamp(biomeIdx, 0, int(biomes.size()) - 1))];
        return (height <= 12) ? b.subsurfaceMaterial : b.deepMaterial;
    };

    QuadEmitter out(mesh);
    const float S = float(step);
    const float tileSizeF = float(mesh.tileSize);

    // ---- 1) Top faces: 2D greedy merge over (height, texture). ----
    {
        std::vector<uint16_t> topTex(size_t(N) * N);
        for (int j = 0; j < N; ++j)
            for (int i = 0; i < N; ++i)
                topTex[size_t(i) + size_t(j) * N] = tex(topMat[size_t(i) + size_t(j) * N], 4);

        std::vector<bool> used(size_t(N) * N, false);
        for (int j = 0; j < N; ++j) {
            for (int i = 0; i < N; ++i) {
                if (used[size_t(i) + size_t(j) * N]) continue;
                const int h0 = q(i, j);
                const uint16_t t0 = topTex[size_t(i) + size_t(j) * N];
                // Grow width along +X.
                int w = 1;
                while (i + w < N && !used[size_t(i + w) + size_t(j) * N] &&
                       q(i + w, j) == h0 && topTex[size_t(i + w) + size_t(j) * N] == t0)
                    ++w;
                // Grow height along +Z while the whole row segment matches.
                int h = 1;
                for (; j + h < N; ++h) {
                    bool rowOK = true;
                    for (int k = 0; k < w; ++k) {
                        if (used[size_t(i + k) + size_t(j + h) * N] ||
                            q(i + k, j + h) != h0 ||
                            topTex[size_t(i + k) + size_t(j + h) * N] != t0) { rowOK = false; break; }
                    }
                    if (!rowOK) break;
                }
                for (int jj = 0; jj < h; ++jj)
                    for (int ii = 0; ii < w; ++ii)
                        used[size_t(i + ii) + size_t(j + jj) * N] = true;
                out.top(i * S, (i + w) * S, j * S, (j + h) * S, float(h0), t0);
            }
        }
    }

    // ---- 2) Side walls between adjacent columns of differing quantized height. ----
    // A wall's normal points at the LOWER column; its material comes from the HIGHER
    // column's biome. Interior planes plus the +X/+Z tile borders (using the sampled
    // neighbor column); -X/-Z border walls are owned by the neighbor tile.
    struct WallRun {
        int yLo, yHi;
        uint32_t faceID;
        uint16_t tex;
        bool valid = false;
        bool matches(int lo, int hi, uint32_t f, uint16_t t) const {
            return valid && lo == yLo && hi == yHi && f == faceID && t == tex;
        }
    };

    // X planes: between columns (i,j) and (i+1,j), plane at x=(i+1)*step, run along j.
    for (int i = 0; i < N; ++i) {
        WallRun run{};
        int runStart = 0;
        auto flush = [&](int endJ) {
            if (run.valid)
                out.wallX((i + 1) * S, runStart * S, endJ * S,
                          float(run.yLo), float(run.yHi), run.faceID, run.tex);
            run.valid = false;
        };
        for (int j = 0; j < N; ++j) {
            const int qa = q(i, j), qb = q(i + 1, j);
            if (qa == qb) { flush(j); continue; }
            const int yLo = std::min(qa, qb), yHi = std::max(qa, qb);
            const uint32_t faceID = (qa > qb) ? 2u : 3u;  // lower side gets the normal
            const int hiBiome = (qa > qb) ? biomeAt(i, j) : biomeAt(i + 1, j);
            const uint16_t t = tex(wallMatFor(hiBiome, yHi - yLo), int(faceID));
            if (!run.matches(yLo, yHi, faceID, t)) {
                flush(j);
                run = {yLo, yHi, faceID, t, true};
                runStart = j;
            }
        }
        flush(N);
    }

    // Z planes: between columns (i,j) and (i,j+1), plane at z=(j+1)*step, run along i.
    for (int j = 0; j < N; ++j) {
        WallRun run{};
        int runStart = 0;
        auto flush = [&](int endI) {
            if (run.valid)
                out.wallZ((j + 1) * S, runStart * S, endI * S,
                          float(run.yLo), float(run.yHi), run.faceID, run.tex);
            run.valid = false;
        };
        for (int i = 0; i < N; ++i) {
            const int qa = q(i, j), qb = q(i, j + 1);
            if (qa == qb) { flush(i); continue; }
            const int yLo = std::min(qa, qb), yHi = std::max(qa, qb);
            const uint32_t faceID = (qa > qb) ? 0u : 1u;  // lower at greater Z => +Z normal
            const int hiBiome = (qa > qb) ? biomeAt(i, j) : biomeAt(i, j + 1);
            const uint16_t t = tex(wallMatFor(hiBiome, yHi - yLo), int(faceID));
            if (!run.matches(yLo, yHi, faceID, t)) {
                flush(i);
                run = {yLo, yHi, faceID, t, true};
                runStart = i;
            }
        }
        flush(N);
    }

    // ---- 3) Border skirts: outward-facing, kSkirtSteps*step tall, hanging from
    // min(qHere, qNeighbor) so they sit below any exact border wall (no coplanar
    // overlap) and cover cross-ring quantization cracks. ----
    const int skirtDepth = kSkirtSteps * step;
    struct SkirtRun {
        int top;
        uint16_t tex;
        bool valid = false;
    };
    // edge: 0=-X (x=0, faceID 3), 1=+X (x=tileSize, faceID 2),
    //       2=-Z (z=0, faceID 1), 3=+Z (z=tileSize, faceID 0)
    for (int edge = 0; edge < 4; ++edge) {
        SkirtRun run{};
        int runStart = 0;
        auto skirtAt = [&](int k, int& top, uint16_t& t) {
            int hereQ, nbrQ, hereBiome;
            switch (edge) {
                case 0:  hereQ = q(0, k);     nbrQ = q(-1, k);    hereBiome = biomeAt(0, k);     break;
                case 1:  hereQ = q(N - 1, k); nbrQ = q(N, k);     hereBiome = biomeAt(N - 1, k); break;
                case 2:  hereQ = q(k, 0);     nbrQ = q(k, -1);    hereBiome = biomeAt(k, 0);     break;
                default: hereQ = q(k, N - 1); nbrQ = q(k, N);     hereBiome = biomeAt(k, N - 1); break;
            }
            top = std::min(hereQ, nbrQ);
            const uint32_t faceID = (edge == 0) ? 3u : (edge == 1) ? 2u : (edge == 2) ? 1u : 0u;
            t = tex(wallMatFor(hereBiome, skirtDepth), int(faceID));
        };
        auto flush = [&](int endK) {
            if (!run.valid) return;
            const float a = runStart * S, b = endK * S;
            const float yHi = float(run.top), yLo = float(run.top - skirtDepth);
            switch (edge) {
                case 0:  out.wallX(0.0f,      a, b, yLo, yHi, 3u, run.tex); break;
                case 1:  out.wallX(tileSizeF, a, b, yLo, yHi, 2u, run.tex); break;
                case 2:  out.wallZ(0.0f,      a, b, yLo, yHi, 1u, run.tex); break;
                default: out.wallZ(tileSizeF, a, b, yLo, yHi, 0u, run.tex); break;
            }
            run.valid = false;
        };
        for (int k = 0; k < N; ++k) {
            int top;
            uint16_t t;
            skirtAt(k, top, t);
            if (!(run.valid && run.top == top && run.tex == t)) {
                flush(k);
                run = {top, t, true};
                runStart = k;
            }
        }
        flush(N);
    }

    // Far tiles sit strictly BELOW the real surface (docs/FarRepresentationProviders.md,
    // compositing layer). Two components:
    //  - kBaseBelowSurface: a geometric push-down applied to EVERY ring (including the
    //    finest, ring 1, which used to get zero). `quantizeTop` floors the real surface
    //    plane to a step multiple, so at step-aligned columns the tile is exactly
    //    COPLANAR with the real chunk surface. The pipeline depth bias alone only
    //    reliably resolves that tie at grazing angles (slope-scaled); on flat terrain
    //    viewed top-down the coplanar tiles bled up THROUGH the real grass as the
    //    depth precision dropped with distance (user-reported "seam lines"). A real
    //    ~0.5-voxel vertical gap makes real chunks win at ALL angles/distances within
    //    the overlap zone. Invisible on far terrain (already ±step-quantized).
    //  - the per-ring increment: cross-RING ordering, so where two rings quantize a
    //    column to the same plane the finer ring wins cleanly.
    const float yBias = -kBelowSurfaceBias - 0.01f * float(std::max(0, key.ring - 1));
    for (FarVertex& v : mesh.vertices) v.pos.y += yBias;
    mesh.minY += yBias;
    mesh.maxY += yBias;

    // Far-tree impostors (world-look A1 rethink): ask the deterministic flora plan which
    // trees live on this tile. After the bias so anchors share the mesh's final surface.
    planTrees(mesh, yBias);

    return mesh;
}

namespace {

/// Deterministic per-tree hash in [0,1). World-coordinate keyed, so subsampling and jitter
/// are identical no matter which tile/ring evaluates the tree.
float treeHash01(int wx, int wz, uint32_t salt) {
    uint64_t h = (uint64_t(uint32_t(wx)) << 32) ^ uint64_t(uint32_t(wz)) ^ (uint64_t(salt) << 13);
    h ^= h >> 33; h *= 0xff51afd7ed558ccdULL; h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ULL; h ^= h >> 33;
    return float(h & 0xFFFFFFu) / float(0x1000000u);
}

} // namespace

void FarTerrainMesher::setTreeExclusions(std::vector<glm::vec4> rects) {
    std::lock_guard<std::mutex> lock(m_exclusionMutex);
    m_treeExclusions = std::move(rects);
}

void FarTerrainMesher::planTrees(FarTileMesh& mesh, float yBias) {
    if (mesh.step > kTreeMaxStep) return;

    // Subsample by ring coarseness: total instances per band stay bounded as area grows.
    // The kept set at a coarse step is a SUBSET of the fine step's set (same world-keyed
    // hash), so a tile crossing rings thins rather than reshuffles.
    const float keep = (mesh.step <= 2) ? 1.0f : (mesh.step <= 4 ? 0.55f : 0.30f);

    // Structure footprints: the plan is pristine, the world is edited. Any tree the plan
    // puts inside a placed structure's rect exists ONLY in the plan — the near field's
    // chunks were rebuilt by the structure — so the far tier must drop it or it renders
    // a phantom tree through a building (user-reported over the PG village).
    std::vector<glm::vec4> exclusions;
    {
        std::lock_guard<std::mutex> lock(m_exclusionMutex);
        exclusions = m_treeExclusions;
    }
    auto excluded = [&exclusions](int wx, int wz) {
        for (const auto& r : exclusions)
            if (float(wx) >= r.x && float(wx) <= r.z &&
                float(wz) >= r.y && float(wz) <= r.w) return true;
        return false;
    };

    const glm::ivec2 o = mesh.originXZ;
    const auto placements = m_generator->planFlora(o.x, o.y,
                                                   o.x + mesh.tileSize - 1,
                                                   o.y + mesh.tileSize - 1,
                                                   /*edgeInset=*/0);
    size_t speciesCount = 0;
    const TreeSpecies* species = treeSpeciesTable(speciesCount);

    mesh.trees.reserve(placements.size() / 2);
    for (const auto& p : placements) {
        const int sid = treeSpeciesFor(p.templateName);
        if (sid < 0) continue;
        if (treeHash01(p.worldX, p.worldZ, 0xA11CE) >= keep) continue;
        if (excluded(p.worldX, p.worldZ)) continue;
        const TreeSpecies& a = species[size_t(sid)];

        // Anchor on the tile's QUANTIZED, biased surface — the impostor stands on the far
        // mesh exactly, never floating above it (the defect this representation replaces).
        const float baseY = float(quantizeTop(p.surfaceY, mesh.step)) + yBias;

        // NO size jitter, deliberately: flora stamps templates unscaled, so a jittered card
        // or mesh is ±15% off the real tree it must dissolve into at the residency handoff
        // (user-reported correspondence break, 2026-08-02). Variety comes from the species
        // mix in the plan, not from lying about a tree's size.
        FarTreeInstance t;
        t.localX  = float(p.worldX - o.x) + 0.5f;
        t.localZ  = float(p.worldZ - o.y) + 0.5f;
        t.worldY  = baseY;
        t.height  = a.height;
        t.canopyR = a.canopyR;
        t.packed  = packFarTree(a.shapeClass, uint32_t(sid), a.r, a.g, a.b);
        mesh.trees.push_back(t);
    }

    // Sort by species (then deterministically by position) and record per-species runs —
    // the instanced-mesh tier draws each run with a single firstInstance offset.
    std::sort(mesh.trees.begin(), mesh.trees.end(),
              [](const FarTreeInstance& a, const FarTreeInstance& b) {
                  const uint32_t sa = farTreeSpecies(a.packed), sb = farTreeSpecies(b.packed);
                  if (sa != sb) return sa < sb;
                  if (a.localZ != b.localZ) return a.localZ < b.localZ;
                  return a.localX < b.localX;
              });
    for (size_t i = 0; i < mesh.trees.size();) {
        const uint32_t sid = farTreeSpecies(mesh.trees[i].packed);
        size_t j = i;
        while (j < mesh.trees.size() && farTreeSpecies(mesh.trees[j].packed) == sid) ++j;
        mesh.treeRanges.push_back({uint16_t(sid), uint32_t(i), uint32_t(j - i)});
        i = j;
    }
}

} // namespace Graphics
} // namespace Phyxel
