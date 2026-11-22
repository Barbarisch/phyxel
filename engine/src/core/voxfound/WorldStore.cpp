// docs/VoxelFoundationPlan.md F7.2 -- see WorldStore.h.
#include "core/voxfound/WorldStore.h"

#include "core/MicroCanvas.h"

namespace Phyxel::Core::VoxFound {

namespace {
int wsDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
glm::ivec3 wsDiv(glm::ivec3 a, int b) { return {wsDiv(a.x, b), wsDiv(a.y, b), wsDiv(a.z, b)}; }
constexpr int kCV = WorldStore::kChunkVoxels;
}  // namespace

int WorldStore::materialId(const std::string& name) {
    if (name.empty() || name == "air") return 0;
    for (size_t i = 1; i < m_palette.size(); ++i)
        if (m_palette[i] == name) return int(i);
    if (m_palette.size() >= 256) return -1;
    m_palette.push_back(name);
    return int(m_palette.size() - 1);
}

BlockStore& WorldStore::storeFor(glm::ivec3 chunk) {
    auto it = m_chunks.find(chunk);
    if (it == m_chunks.end()) it = m_chunks.emplace(chunk, BlockStore(glm::ivec3(kCV))).first;
    return it->second;
}

bool WorldStore::fillBox(glm::ivec3 lo, glm::ivec3 hi, const std::string& name, std::string* err) {
    const int m = materialId(name);
    if (m < 0) {
        if (err) *err = "the palette is full (255 materials)";
        return false;
    }
    if (glm::any(glm::greaterThan(lo, hi))) return true;
    const glm::ivec3 c0 = wsDiv(lo, kCV), c1 = wsDiv(hi, kCV);
    for (int cx = c0.x; cx <= c1.x; ++cx)
        for (int cy = c0.y; cy <= c1.y; ++cy)
            for (int cz = c0.z; cz <= c1.z; ++cz) {
                const glm::ivec3 c(cx, cy, cz), base = c * kCV;
                const glm::ivec3 l = glm::max(lo, base) - base, h = glm::min(hi, base + kCV - 1) - base;   // chunk-local
                BlockStore& s = storeFor(c);
                for (int bx = l.x >> 3; bx <= h.x >> 3; ++bx)
                    for (int by = l.y >> 3; by <= h.y >> 3; ++by)
                        for (int bz = l.z >> 3; bz <= h.z >> 3; ++bz) {
                            const glm::ivec3 b(bx, by, bz), bl = b * 8, bh = bl + 7;
                            if (glm::all(glm::lessThanEqual(l, bl)) && glm::all(glm::greaterThanEqual(h, bh))) {
                                s.fillBlock(b, uint8_t(m));   // the whole block: one uniform entry
                                continue;
                            }
                            const glm::ivec3 pl = glm::max(l, bl), ph = glm::min(h, bh);
                            for (int x = pl.x; x <= ph.x; ++x)
                                for (int y = pl.y; y <= ph.y; ++y)
                                    for (int z = pl.z; z <= ph.z; ++z) s.set({x, y, z}, uint8_t(m));
                        }
            }
    return true;
}

bool WorldStore::paint(const std::vector<std::pair<glm::ivec3, std::string>>& cells, std::string* err) {
    std::vector<int> ids;
    ids.reserve(cells.size());
    const std::vector<std::string> before = m_palette;
    for (const auto& [v, name] : cells) {
        const int m = materialId(name);
        if (m < 0) {
            m_palette = before;   // all or nothing
            if (err) *err = "the palette is full (255 materials)";
            return false;
        }
        ids.push_back(m);
    }
    for (size_t i = 0; i < cells.size(); ++i) {
        const glm::ivec3 c = wsDiv(cells[i].first, kCV);
        storeFor(c).set(cells[i].first - c * kCV, uint8_t(ids[i]));
    }
    return true;
}

bool WorldStore::paintCanvas(const MicroCanvas& canvas, glm::ivec3 originVoxel, std::string* err) {
    if (canvas.cellsPerM() != 8) {
        if (err) *err = "a canvas of " + std::to_string(canvas.cellsPerM()) +
                        " cells per metre: only 8 (the world's 1/8 m voxels) is written; 1/9 m content needs conversion";
        return false;
    }
    std::vector<std::pair<glm::ivec3, std::string>> cells;
    for (const glm::ivec3& c : canvas.occupiedCells()) cells.push_back({originVoxel + c, canvas.materialAt(c.x, c.y, c.z)});
    return paint(cells, err);
}

bool WorldStore::fromRegion(WorldGenerator& gen, glm::ivec3 chunkLo, glm::ivec3 chunkHi, const std::string* floraDir,
                            std::string* err) {
    bool ok = true;
    forEachRegionChunk(gen, chunkLo, chunkHi, floraDir, [&](const RegionChunk& rc, const std::vector<std::string>& pal) {
        if (!ok) return;
        std::vector<uint8_t> map(256, 0);
        for (size_t i = 1; i < pal.size(); ++i) {
            const int m = materialId(pal[i]);
            if (m < 0) {
                ok = false;
                if (err) *err = "the palette is full (255 materials)";
                return;
            }
            map[i] = uint8_t(m);
        }
        BlockStore s = BlockStore::fromGrid(rc.grid);
        s.remapMaterials(map);
        m_chunks[rc.chunk] = std::move(s);
    });
    return ok;
}

uint8_t WorldStore::voxel(glm::ivec3 v) const {
    const glm::ivec3 c = wsDiv(v, kCV);
    auto it = m_chunks.find(c);
    return it == m_chunks.end() ? 0 : it->second.at(v - c * kCV);
}

std::string WorldStore::materialAt(glm::ivec3 v) const {
    const uint8_t m = voxel(v);
    return m ? m_palette[m] : std::string();
}

const BlockStore* WorldStore::chunkStore(glm::ivec3 chunk) const {
    auto it = m_chunks.find(chunk);
    return it == m_chunks.end() ? nullptr : &it->second;
}

std::vector<glm::ivec3> WorldStore::chunks() const {
    std::vector<glm::ivec3> out;
    for (const auto& kv : m_chunks) out.push_back(kv.first);
    return out;
}

WorldReads WorldStore::reads() const {
    return WorldReads([this](glm::ivec3 c) { return chunkStore(c); }, chunks());
}

void WorldStore::collapse() {
    for (auto& kv : m_chunks) kv.second.collapse();
}

}  // namespace Phyxel::Core::VoxFound
