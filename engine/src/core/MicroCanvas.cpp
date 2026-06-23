#include "core/MicroCanvas.h"

#include <array>
#include <map>
#include <sstream>

namespace Phyxel {
namespace Core {

namespace {

// Floor division for b > 0 (handles negative micro coords correctly so an asset
// authored around the origin still coarsens cleanly).
inline int floorDiv(int a, int b) {
    int q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

// Decompose a micro coordinate into (cube, subcube, micro) indices.
inline void splitCoord(int g, int& c, int& s, int& m) {
    c = floorDiv(g, MicroCanvas::MICRO_PER_CUBE);
    int r = g - c * MicroCanvas::MICRO_PER_CUBE;        // r in [0,9)
    s = r / MicroCanvas::MICRO_PER_SUB;
    m = r % MicroCanvas::MICRO_PER_SUB;
}

} // namespace

std::string ResolutionReport::summary() const {
    std::ostringstream os;
    os << total() << " voxels (C=" << cubes << " S=" << subcubes << " M=" << microcubes
       << ") vs " << microCells() << " all-micro -> ";
    os.setf(std::ios::fixed);
    os.precision(1);
    os << savingsPercent() << "% fewer voxels";
    return os.str();
}

// --------------------------------------------------------------------------- painting

void MicroCanvas::setMicroCell(int gx, int gy, int gz, const std::string& mat) {
    glm::ivec3 key(gx, gy, gz);
    if (mat.empty()) {
        m_cells.erase(key);            // AIR -> carve
    } else {
        m_cells[key] = mat;
    }
}

void MicroCanvas::fillMicroBox(int gx, int gy, int gz, int gw, int gh, int gd,
                               const std::string& mat) {
    for (int x = gx; x < gx + gw; ++x)
        for (int y = gy; y < gy + gh; ++y)
            for (int z = gz; z < gz + gd; ++z)
                setMicroCell(x, y, z, mat);
}

void MicroCanvas::addCube(int cx, int cy, int cz, const std::string& mat) {
    fillMicroBox(cx * 9, cy * 9, cz * 9, 9, 9, 9, mat);
}

void MicroCanvas::addSubcube(int cx, int cy, int cz, int sx, int sy, int sz,
                             const std::string& mat) {
    fillMicroBox(cx * 9 + sx * 3, cy * 9 + sy * 3, cz * 9 + sz * 3, 3, 3, 3, mat);
}

void MicroCanvas::addMicro(int cx, int cy, int cz, int sx, int sy, int sz,
                           int mx, int my, int mz, const std::string& mat) {
    setMicroCell(cx * 9 + sx * 3 + mx, cy * 9 + sy * 3 + my, cz * 9 + sz * 3 + mz, mat);
}

void MicroCanvas::fillCubeBox(int cx, int cy, int cz, int w, int h, int d,
                              const std::string& mat) {
    fillMicroBox(cx * 9, cy * 9, cz * 9, w * 9, h * 9, d * 9, mat);
}

// --------------------------------------------------------------------------- detailers

void MicroCanvas::chamferEdge(int gx, int gy, int gz, int gw, int gh, int gd,
                              const std::string& axis, const std::string& corner, int depth) {
    (void)axis;  // axis documents intent; the wedge math is driven by `corner`.
    // Map a face token ("+y", "-x", ...) to (axis index, sign).
    auto face = [](const std::string& f, int& ax, int& sign) {
        ax = (f[1] == 'x') ? 0 : (f[1] == 'y') ? 1 : 2;
        sign = (f[0] == '+') ? +1 : -1;
    };
    if (corner.size() != 4) return;
    int aAx, aSign, bAx, bSign;
    face(corner.substr(0, 2), aAx, aSign);
    face(corner.substr(2, 2), bAx, bSign);

    const glm::ivec3 lo(gx, gy, gz);
    const glm::ivec3 hi(gx + gw - 1, gy + gh - 1, gz + gd - 1);
    for (int x = gx; x < gx + gw; ++x) {
        for (int y = gy; y < gy + gh; ++y) {
            for (int z = gz; z < gz + gd; ++z) {
                glm::ivec3 p(x, y, z);
                int da = (aSign < 0) ? (p[aAx] - lo[aAx]) : (hi[aAx] - p[aAx]);
                int db = (bSign < 0) ? (p[bAx] - lo[bAx]) : (hi[bAx] - p[bAx]);
                if (da + db < depth)                  // past the diagonal cut -> air
                    setMicroCell(x, y, z, "");
            }
        }
    }
}

// --------------------------------------------------------------------------- export

std::vector<CanvasVoxel> MicroCanvas::exportVoxels() const {
    // Bucket every micro cell by its owning cube, keyed by the within-cube micro
    // index (lx + ly*9 + lz*81, 0..728).
    std::map<glm::ivec3, std::map<int, std::string>,
             bool(*)(const glm::ivec3&, const glm::ivec3&)> byCube(
        [](const glm::ivec3& a, const glm::ivec3& b) {
            if (a.x != b.x) return a.x < b.x;
            if (a.y != b.y) return a.y < b.y;
            return a.z < b.z;
        });

    for (const auto& [g, mat] : m_cells) {
        int cx, sx, mx, cy, sy, my, cz, sz, mz;
        splitCoord(g.x, cx, sx, mx);
        splitCoord(g.y, cy, sy, my);
        splitCoord(g.z, cz, sz, mz);
        int lx = sx * 3 + mx, ly = sy * 3 + my, lz = sz * 3 + mz;
        byCube[glm::ivec3(cx, cy, cz)][lx + ly * 9 + lz * 81] = mat;
    }

    std::vector<CanvasVoxel> out;
    for (const auto& [cube, cells] : byCube) {
        // Whole cube uniform?
        if (cells.size() == 729) {
            const std::string& first = cells.begin()->second;
            bool uniform = true;
            for (const auto& [idx, m] : cells)
                if (m != first) { uniform = false; break; }
            if (uniform) {
                out.push_back({CanvasRes::Cube, cube, {0, 0, 0}, {0, 0, 0}, first});
                continue;
            }
        }
        // Walk the 27 subcubes; each is either uniform (-> subcube) or per-micro.
        for (int sz = 0; sz < 3; ++sz) {
            for (int sy = 0; sy < 3; ++sy) {
                for (int sx = 0; sx < 3; ++sx) {
                    // gather the 27 within-cube micro indices belonging to this subcube
                    std::array<const std::string*, 27> got{};
                    int present = 0;
                    bool uniform = true;
                    const std::string* first = nullptr;
                    int k = 0;
                    for (int mz = 0; mz < 3; ++mz)
                        for (int my = 0; my < 3; ++my)
                            for (int mx = 0; mx < 3; ++mx, ++k) {
                                int lx = sx * 3 + mx, ly = sy * 3 + my, lz = sz * 3 + mz;
                                auto it = cells.find(lx + ly * 9 + lz * 81);
                                if (it == cells.end()) { got[k] = nullptr; continue; }
                                got[k] = &it->second;
                                ++present;
                                if (!first) first = &it->second;
                                else if (it->second != *first) uniform = false;
                            }
                    if (present == 0) continue;
                    if (present == 27 && uniform) {
                        out.push_back({CanvasRes::Subcube, cube, {sx, sy, sz}, {0, 0, 0}, *first});
                    } else {
                        int kk = 0;
                        for (int mz = 0; mz < 3; ++mz)
                            for (int my = 0; my < 3; ++my)
                                for (int mx = 0; mx < 3; ++mx, ++kk)
                                    if (got[kk])
                                        out.push_back({CanvasRes::Microcube, cube,
                                                       {sx, sy, sz}, {mx, my, mz}, *got[kk]});
                    }
                }
            }
        }
    }
    return out;
}

ResolutionReport MicroCanvas::report() const {
    ResolutionReport r;
    for (const auto& v : exportVoxels()) {
        switch (v.res) {
            case CanvasRes::Cube:      ++r.cubes; break;
            case CanvasRes::Subcube:   ++r.subcubes; break;
            case CanvasRes::Microcube: ++r.microcubes; break;
        }
    }
    return r;
}

bool MicroCanvas::microBounds(glm::ivec3& lo, glm::ivec3& hi) const {
    if (m_cells.empty()) return false;
    bool first = true;
    for (const auto& [g, mat] : m_cells) {
        (void)mat;
        if (first) { lo = hi = g; first = false; }
        else { lo = glm::min(lo, g); hi = glm::max(hi, g); }
    }
    return true;
}

std::vector<glm::ivec3> MicroCanvas::occupiedCells() const {
    std::vector<glm::ivec3> out;
    out.reserve(m_cells.size());
    for (const auto& [g, mat] : m_cells) { (void)mat; out.push_back(g); }
    return out;
}

void MicroCanvas::toVoxelTemplate(VoxelTemplate& out) const {
    for (const auto& v : exportVoxels()) {
        switch (v.res) {
            case CanvasRes::Cube:
                out.addCube(v.cube, v.material);
                break;
            case CanvasRes::Subcube:
                out.addSubcube(v.cube, v.sub, v.material);
                break;
            case CanvasRes::Microcube:
                out.addMicrocube(v.cube, v.sub, v.micro, v.material);
                break;
        }
    }
}

} // namespace Core
} // namespace Phyxel
