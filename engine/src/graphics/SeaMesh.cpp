#include "graphics/SeaMesh.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace Phyxel {
namespace Graphics {

namespace {

// Vertex store with dedup on the FINEST lattice. Every level's spacing is a power-of-two multiple
// of SEA_CORE_SPACING, so every vertex any level wants lands exactly on that lattice and can be
// keyed by integer lattice coordinates. Two levels asking for the same position therefore get the
// same index — which is what makes the level boundaries watertight rather than merely coincident.
class VertexStore {
public:
    uint32_t at(float x, float z) {
        const Key k{lattice(x), lattice(z)};
        auto it = m_index.find(k);
        if (it != m_index.end()) return it->second;
        const uint32_t idx = static_cast<uint32_t>(m_verts.size());
        m_verts.emplace_back(x, 0.0f, z);
        m_index.emplace(k, idx);
        return idx;
    }

    // Does a vertex already exist here? Used to detect a finer neighbour's midpoint sitting in the
    // middle of a coarse edge, i.e. a T-junction about to happen.
    bool exists(float x, float z) const {
        return m_index.find(Key{lattice(x), lattice(z)}) != m_index.end();
    }

    // Unshared vertex, never deduplicated. The skirt rings sit at scaled radii that do NOT land on
    // the clipmap lattice, so keying them by rounded lattice coordinates would be answering a
    // question the key cannot represent — two genuinely different positions could collide onto one
    // index and silently deform the mesh. Skirt rings are built once and their indices kept, so
    // they never need lookup in the first place.
    uint32_t alloc(float x, float z) {
        const uint32_t idx = static_cast<uint32_t>(m_verts.size());
        m_verts.emplace_back(x, 0.0f, z);
        return idx;
    }

    std::vector<glm::vec3> take() { return std::move(m_verts); }

private:
    using Key = std::pair<int64_t, int64_t>;
    // ⚑The lattice must be as fine as the FINEST position ever queried, which is HALF the core
    // spacing — edge midpoints, not just cell corners. Keying on SEA_CORE_SPACING itself made a
    // level-0 midpoint at 2.0 round to the same key as a corner at 4.0, so every level-0 cell
    // believed a finer neighbour had subdivided its edges and centre-fanned itself. That produced
    // 12,282 edges shared by more than two triangles (caught by SeaMeshTest).
    static int64_t lattice(float v) {
        return static_cast<int64_t>(std::llround(v / (SEA_CORE_SPACING * 0.5f)));
    }
    std::vector<glm::vec3>   m_verts;
    std::map<Key, uint32_t>  m_index;
};

// Emits one cell of a level. Any edge whose midpoint already exists (because a finer level built it)
// is subdivided, so no neighbouring fine vertex is left dangling in the middle of a coarse edge.
//
// A cell with no subdivided edge is the usual two triangles. A cell with one or more is fanned from
// its CENTRE instead: the centre is interior to the cell so it can never introduce a crack, and a
// centre fan handles the corner case of two subdivided edges without any special-casing. Fanning
// from a corner would generate degenerate slivers, because a midpoint is collinear with the corners
// of the edge it sits on.
void emitCell(VertexStore& vs, std::vector<uint32_t>& idx,
              float x0, float z0, float s) {
    const float x1 = x0 + s, z1 = z0 + s;
    const float xm = x0 + s * 0.5f, zm = z0 + s * 0.5f;

    // Corners, counter-clockwise viewed from +Y, and the midpoint of the edge that follows each.
    const float cx[4] = {x0, x1, x1, x0};
    const float cz[4] = {z0, z0, z1, z1};
    const float mx[4] = {xm, x1, xm, x0};
    const float mz[4] = {z0, zm, z1, zm};

    bool split[4];
    bool any = false;
    for (int e = 0; e < 4; ++e) {
        split[e] = vs.exists(mx[e], mz[e]);
        any = any || split[e];
    }

    if (!any) {
        const uint32_t a = vs.at(x0, z0), b = vs.at(x1, z0);
        const uint32_t c = vs.at(x0, z1), d = vs.at(x1, z1);
        idx.push_back(a); idx.push_back(c); idx.push_back(b);
        idx.push_back(b); idx.push_back(c); idx.push_back(d);
        return;
    }

    // Boundary polygon: each corner, followed by that edge's midpoint when it is subdivided.
    std::vector<uint32_t> ring;
    ring.reserve(8);
    for (int e = 0; e < 4; ++e) {
        ring.push_back(vs.at(cx[e], cz[e]));
        if (split[e]) ring.push_back(vs.at(mx[e], mz[e]));
    }
    const uint32_t centre = vs.at(xm, zm);
    for (size_t i = 0; i < ring.size(); ++i) {
        const uint32_t a = ring[i];
        const uint32_t b = ring[(i + 1) % ring.size()];
        idx.push_back(centre); idx.push_back(a); idx.push_back(b);
    }
}

}  // namespace

SeaMesh buildSeaClipmap(float waveRadius) {
    SeaMesh mesh;
    VertexStore vs;
    std::vector<uint32_t> idx;

    // How many levels to reach waveRadius. Level k has half-extent SEA_CORE_HALF * 2^k.
    int levels = 1;
    while (levels < SEA_MAX_LEVELS &&
           SEA_CORE_HALF * static_cast<float>(1 << (levels - 1)) < waveRadius) {
        ++levels;
    }

    // Fine to coarse — emitCell's T-junction detection depends on the finer neighbour's vertices
    // already being in the store.
    for (int k = 0; k < levels; ++k) {
        const float s = SEA_CORE_SPACING * static_cast<float>(1 << k);
        const float h = SEA_CORE_HALF   * static_cast<float>(1 << k);
        const float hole = (k == 0) ? 0.0f : h * 0.5f;   // level k-1's extent
        const int   n = SEA_CELLS;
        for (int i = 0; i < n; ++i) {
            const float x0 = -h + s * static_cast<float>(i);
            for (int j = 0; j < n; ++j) {
                const float z0 = -h + s * static_cast<float>(j);
                // Skip cells wholly inside the finer level that already covers this area.
                if (hole > 0.0f &&
                    x0 >= -hole && x0 + s <= hole &&
                    z0 >= -hole && z0 + s <= hole) {
                    continue;
                }
                emitCell(vs, idx, x0, z0, s);
            }
        }
    }

    const float outer = SEA_CORE_HALF * static_cast<float>(1 << (levels - 1));

    // SKIRT. Flat coverage from the outermost level to well past any far plane, so the horizon is
    // never a visible edge. Built by scaling the outermost square OUTLINE outward, which keeps a 1:1
    // vertex correspondence between consecutive rings — no stitching needed, and its coarseness
    // costs nothing because the swell has long since faded to flat out there.
    {
        std::vector<glm::vec3> outline;
        const float s = SEA_CORE_SPACING * static_cast<float>(1 << (levels - 1));
        const int   n = SEA_CELLS;
        for (int i = 0; i < n; ++i) outline.emplace_back(-outer + s * i, 0.0f, -outer);
        for (int i = 0; i < n; ++i) outline.emplace_back( outer, 0.0f, -outer + s * i);
        for (int i = 0; i < n; ++i) outline.emplace_back( outer - s * i, 0.0f,  outer);
        for (int i = 0; i < n; ++i) outline.emplace_back(-outer, 0.0f,  outer - s * i);

        const int kRings = 3;
        std::vector<uint32_t> prev;
        prev.reserve(outline.size());
        for (const auto& p : outline) prev.push_back(vs.at(p.x, p.z));

        for (int r = 1; r <= kRings; ++r) {
            const float t = static_cast<float>(r) / static_cast<float>(kRings);
            const float scale = std::pow(SEA_SKIRT_RADIUS / outer, t);
            std::vector<uint32_t> cur;
            cur.reserve(outline.size());
            for (const auto& p : outline) cur.push_back(vs.alloc(p.x * scale, p.z * scale));
            for (size_t i = 0; i < outline.size(); ++i) {
                const size_t j = (i + 1) % outline.size();
                idx.push_back(prev[i]); idx.push_back(cur[i]);  idx.push_back(prev[j]);
                idx.push_back(prev[j]); idx.push_back(cur[i]);  idx.push_back(cur[j]);
            }
            prev = std::move(cur);
        }
    }

    mesh.vertices    = vs.take();
    mesh.indices     = std::move(idx);
    mesh.levels      = levels;
    mesh.outerExtent = outer;
    return mesh;
}

}  // namespace Graphics
}  // namespace Phyxel
