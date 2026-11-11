// docs/VoxelFoundationPlan.md §3 + §8 — storage lab (F0 skeleton). See StorageLab.h.

#include "core/voxfound/StorageLab.h"

#include <cmath>
#include <map>

#include "core/WorldGenerator.h"

namespace Phyxel::Core::VoxFound {

VoxelArea buildTerrainArea(WorldGenerator& gen, glm::ivec3 o, glm::ivec3 sz) {
    VoxelArea a;
    a.originVoxel = o * kVoxelsPerM;
    a.grid.resize(sz * kVoxelsPerM);
    a.palette = {"air"};
    std::map<std::string, uint8_t> ids;
    auto idOf = [&](const std::string& name) -> uint8_t {
        auto it = ids.find(name);
        if (it != ids.end()) return it->second;
        const uint8_t id = uint8_t(a.palette.size());
        a.palette.push_back(name);
        ids.emplace(name, id);
        return id;
    };

    // Metre samples with a one-column margin for the bilinear interpolation.
    const int nx = sz.x + 2, nz = sz.z + 2;
    std::vector<WorldGenerator::ColumnSample> cols(size_t(nx) * size_t(nz));
    for (int i = 0; i < nx; ++i)
        for (int k = 0; k < nz; ++k) cols[size_t(i) * nz + k] = gen.sampleSurface(o.x - 1 + i, o.z - 1 + k);
    auto col = [&](int i, int k) -> const WorldGenerator::ColumnSample& { return cols[size_t(i + 1) * nz + (k + 1)]; };

    // The continuous ground: bedHeight is where today's cube terrain rounds from (surfaceY = floor(bedHeight),
    // solid up to surfaceY + 1, i.e. on average bedHeight + 0.5). At 1/8 m the voxel whose centre lies below
    // bedHeight + 0.5 is solid: the same mean ground level as today, followed at 1/8 m steps instead of 1 m steps
    // (at a column centre it is within half a metre of today's cube top). Between metre column centres
    // (m + 0.5) the height is bilinear. Road grading (which moves surfaceY, not bedHeight) is not reproduced.
    const glm::ivec3 d = a.grid.dim;
    for (int vx = 0; vx < d.x; ++vx) {
        const float wx = float(o.x) + (vx + 0.5f) / kVoxelsPerM;
        const float fx = wx - 0.5f - float(o.x);
        const int ix = int(std::floor(fx));
        const float tx = fx - float(ix);
        for (int vz = 0; vz < d.z; ++vz) {
            const float wz = float(o.z) + (vz + 0.5f) / kVoxelsPerM;
            const float fz = wz - 0.5f - float(o.z);
            const int iz = int(std::floor(fz));
            const float tz = fz - float(iz);
            const float h00 = col(ix, iz).bedHeight, h10 = col(ix + 1, iz).bedHeight;
            const float h01 = col(ix, iz + 1).bedHeight, h11 = col(ix + 1, iz + 1).bedHeight;
            const float h = (h00 * (1 - tx) + h10 * tx) * (1 - tz) + (h01 * (1 - tx) + h11 * tx) * tz;
            const auto& c = col(int(std::floor(wx)) - o.x, int(std::floor(wz)) - o.z);  // materials: own metre
            for (int vy = 0; vy < d.y; ++vy) {
                const float wy = float(o.y) + (vy + 0.5f) / kVoxelsPerM;
                if (wy >= h + 0.5f) break;
                const int my = int(std::floor(wy));
                // material layers: depth below the column's own top metre, as today's generator counts it
                WorldGenerator::ColumnSample cc = c;
                cc.surfaceY = int(std::floor(h + 0.5f - 1e-4f));
                if (my > cc.surfaceY) cc.surfaceY = my;
                a.grid.set({vx, vy, vz}, idOf(gen.columnMaterialAt(my, cc)));
            }
        }
    }
    return a;
}

size_t BlockStats::estimatedBytes() const { return blocks * 8 + detailed * (512 + 64); }

BlockStats blockStats(const VoxelGrid& g) {
    BlockStats s;
    const glm::ivec3 nb = g.dim / kBlock;
    s.blockColumns = size_t(nb.x) * size_t(nb.z);
    for (int bx = 0; bx < nb.x; ++bx)
        for (int by = 0; by < nb.y; ++by)
            for (int bz = 0; bz < nb.z; ++bz) {
                ++s.blocks;
                const uint8_t first = g.mat[g.idx({bx * kBlock, by * kBlock, bz * kBlock})];
                bool uniform = true;
                for (int x = 0; x < kBlock && uniform; ++x)
                    for (int y = 0; y < kBlock && uniform; ++y)
                        for (int z = 0; z < kBlock; ++z)
                            if (g.mat[g.idx({bx * kBlock + x, by * kBlock + y, bz * kBlock + z})] != first) {
                                uniform = false;
                                break;
                            }
                if (!uniform) ++s.detailed;
                else if (first == 0) ++s.uniformAir;
                else ++s.uniformSolid;
            }
    return s;
}

// ---- BlockStore: F0 stub ----------------------------------------------------------------------

BlockStore::BlockStore(glm::ivec3 dimVoxels) : m_dim(dimVoxels) {}
void BlockStore::set(glm::ivec3, uint8_t) {}
uint8_t BlockStore::get(glm::ivec3) const { return 0; }
size_t BlockStore::uniformBlocks() const { return 0; }
size_t BlockStore::detailedBlocks() const { return 0; }
size_t BlockStore::memoryBytes() const { return 0; }

size_t compareToReference(const BlockStore& s, const VoxelGrid& ref, std::string* why) {
    size_t bad = 0;
    std::string msg;
    for (size_t i = 0; i < ref.size(); ++i) {
        const glm::ivec3 p = ref.pos(i);
        const uint8_t got = s.get(p);
        if (got != ref.mat[i]) {
            if (bad < 3)
                msg += "(" + std::to_string(p.x) + "," + std::to_string(p.y) + "," + std::to_string(p.z) +
                       ") store " + std::to_string(got) + " ref " + std::to_string(ref.mat[i]) + "; ";
            ++bad;
        }
    }
    if (why) *why = msg;
    return bad;
}

}  // namespace Phyxel::Core::VoxFound
