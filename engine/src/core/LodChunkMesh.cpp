#include "core/LodChunkMesh.h"

#include <algorithm>
#include <map>

#include "core/Chunk.h"
#include "core/Cube.h"
#include "core/Subcube.h"
#include "core/Microcube.h"
#include "core/MaterialRegistry.h"

namespace Phyxel {
namespace Core {

namespace {
constexpr int kChunk = 32;

/// faceID order used everywhere in this engine: 0=+Z 1=-Z 2=+X 3=-X 4=+Y 5=-Y.
constexpr int kFaceOffsets[6][3] = {
    { 0, 0, 1}, { 0, 0,-1}, { 1, 0, 0}, {-1, 0, 0}, { 0, 1, 0}, { 0,-1, 0}
};
} // namespace

LodVolume LodChunkMesh::volumeFromChunk(const Chunk& chunk, std::vector<std::string>* outPalette) {
    LodVolume v(glm::ivec3(kChunk, kChunk, kChunk), 0);
    std::vector<std::string> palette;
    palette.push_back("");                     // id 0 == air

    auto idFor = [&palette](const std::string& m) -> uint16_t {
        for (size_t i = 0; i < palette.size(); ++i)
            if (palette[i] == m) return static_cast<uint16_t>(i);
        palette.push_back(m);
        return static_cast<uint16_t>(palette.size() - 1);
    };

    Chunk& mutableChunk = const_cast<Chunk&>(chunk);   // getCubeAt is non-const (materializes)
    for (int x = 0; x < kChunk; ++x)
    for (int y = 0; y < kChunk; ++y)
    for (int z = 0; z < kChunk; ++z) {
        const glm::ivec3 cp(x, y, z);
        LodCell& cell = v.at(x, y, z);

        // FULL CUBE: the whole 9^3 of microcubes.
        if (const Cube* c = mutableChunk.getCubeAt(cp)) {
            if (c->isVisible()) {
                cell.coverage = LodVolume::kFullCoverage;
                cell.bulkMaterial = cell.skinMaterial = idFor(c->getMaterialName());
                continue;
            }
        }

    }

    // SUB/MICROCUBE CONTENT — plan §2.1: sub/micro detail is not a rung on the ladder, it is the
    // cube's APPEARANCE, carried as `coverage`. Reading only getCubeAt() made every sub/micro-only
    // cell read EMPTY, so thin structure walls (timber_cottage interior_wall is ONE microcube)
    // vanished entirely from the coarse mesh.
    //   1 subcube = 3^3 microcubes = 27   |   1 microcube = 1
    //
    // Iterate the chunk's OWN sub/micro containers rather than probing all 32768 cells x 756
    // slots: the probing version was correct but took 79 s for ten unit tests, which would make
    // a real LOD build unusable. This is O(actual sub/micro voxels).
    {
        std::map<size_t, std::map<uint16_t, uint32_t>> weights;   // cell index -> material weights
        auto cellIndex = [&](const glm::ivec3& p) -> size_t {
            return size_t(p.z) + size_t(p.y) * kChunk + size_t(p.x) * kChunk * kChunk;
        };
        auto addTo = [&](const glm::ivec3& cp, uint64_t amount, const std::string& mat) {
            if (cp.x < 0 || cp.y < 0 || cp.z < 0 ||
                cp.x >= kChunk || cp.y >= kChunk || cp.z >= kChunk) return;
            LodCell& cell = v.at(cp.x, cp.y, cp.z);
            if (cell.coverage >= LodVolume::kFullCoverage) return;   // already a full cube
            cell.coverage += amount;
            weights[cellIndex(cp)][idFor(mat)] += static_cast<uint32_t>(amount);
        };
        for (const auto& sc : chunk.getStaticSubcubes())
            if (sc) addTo(sc->getLocalPosition(), 27, sc->getMaterialName());
        for (const auto& mc : chunk.getStaticMicrocubes())
            if (mc) addTo(mc->getParentCubePosition(), 1, mc->getMaterialName());

        for (const auto& kv : weights) {
            const size_t idx = kv.first;
            const int x = int(idx / (kChunk * kChunk));
            const int y = int((idx / kChunk) % kChunk);
            const int z = int(idx % kChunk);
            uint16_t best = 0; uint32_t bw = 0;
            for (const auto& mw : kv.second) if (mw.second > bw) { bw = mw.second; best = mw.first; }
            LodCell& cell = v.at(x, y, z);
            if (cell.bulkMaterial == 0) cell.bulkMaterial = cell.skinMaterial = best;
        }
    }

    if (outPalette) *outPalette = std::move(palette);
    return v;
}

void LodChunkMesh::emitFaces(const LodVolume& volume,
                             const std::vector<std::string>& palette,
                             std::vector<InstanceData>& out) {
    if (volume.empty()) return;
    const int level = volume.level();
    const int cell = volume.cellSizeInCubes();
    const glm::ivec3 d = volume.dim();
    auto& materials = MaterialRegistry::instance();

    for (int x = 0; x < d.x; ++x)
    for (int y = 0; y < d.y; ++y)
    for (int z = 0; z < d.z; ++z) {
        const LodCell& c = volume.at(x, y, z);
        if (!c.solid()) continue;
        // A cell carved by an authored opening is not drawn: the C4 renderer's use of the
        // sub-brick mask (plan §2.3) starts here -- an opening cell is a hole, not a wall.
        // A SOLID cell must always render. Dropping it because its material failed to resolve
        // produces a see-through hole, which is never a valid coarsening -- so fall back
        // (skin -> bulk -> the engine's "Default" indicator) instead of `continue`.
        // Pinned by LodChunkMeshTest.Solid{,Sub}CellWithUnresolvedMaterialStillEmitsFaces.
        auto lookup = [&palette](uint16_t id) -> std::string {
            return (id < palette.size()) ? palette[id] : std::string();
        };
        std::string mat = lookup(c.skinMaterial);
        if (mat.empty()) mat = lookup(c.bulkMaterial);
        if (mat.empty()) mat = "Default";

        for (int f = 0; f < 6; ++f) {
            const int nx = x + kFaceOffsets[f][0];
            const int ny = y + kFaceOffsets[f][1];
            const int nz = z + kFaceOffsets[f][2];
            // Out-of-volume reads as empty -> chunk-boundary faces are emitted, matching the
            // fine mesher's conservative treatment of neighbours it cannot see.
            if (volume.atClamped(nx, ny, nz).solid()) continue;

            InstanceData inst{};
            // Cell origin in CUBE coords (the shader scales the quad by 2^level from here).
            inst.packedData = Phyxel::InstanceDataUtils::packLodCellData(
                static_cast<uint32_t>(x * cell), static_cast<uint32_t>(y * cell),
                static_cast<uint32_t>(z * cell), static_cast<uint32_t>(f),
                static_cast<uint32_t>(level));
            inst.textureIndex = materials.getTextureIndex(mat, f);
            inst.reserved = 0;
            // Flat full-bright lighting: the coarse path has no baked per-corner light yet, and
            // inventing one would be worse than being visibly uniform. Tracked as C4 follow-up.
            inst.light = 0xFFFFu;
            inst.light2 = 0;
            inst.light3 = 0;
            inst.tint = 0xFFFFFFu;
            out.push_back(inst);
        }
    }
}

void LodChunkMesh::buildForLevel(const Chunk& chunk, int level, const SquashConfig& cfg,
                                 std::vector<InstanceData>& out) {
    std::vector<std::string> palette;
    LodVolume v = volumeFromChunk(chunk, &palette);
    for (int i = 0; i < level; ++i) {
        if (v.dim().x <= 1 && v.dim().y <= 1 && v.dim().z <= 1) break;
        v = squash(v, cfg);
    }
    emitFaces(v, palette, out);
}

size_t LodChunkMesh::fineFaceCount(const Chunk& chunk) {
    std::vector<std::string> palette;
    LodVolume v = volumeFromChunk(chunk, &palette);
    std::vector<InstanceData> tmp;
    emitFaces(v, palette, tmp);
    return tmp.size();
}

} // namespace Core
} // namespace Phyxel
