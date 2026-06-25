#pragma once

// ============================================================================
// MicroCanvas — multi-resolution voxel painter with greedy coarsening.
//
// Structure Generation v2 (docs/structure-generation/StructureGenerationV2.md), level-5 realization.
//
// The idea: paint EVERYTHING into one fine grid — the microcube grid, 9x9x9
// micro-cells per cube (9 micro = 1 cube, 3 micro = 1 subcube) — then export()
// greedily coarsens each region to the LARGEST voxel that is uniformly filled
// with a single material:
//
//     uniform 9x9x9 (729 cells) -> 1 cube       (flat bulk is free)
//     uniform 3x3x3 (27 cells)  -> 1 subcube     (slabs/walls are cheap)
//     otherwise                  -> microcubes    (only where detail varies)
//
// So resolution tracks detail automatically: bulk costs cubes, walls/floors cost
// subcubes, ornament costs micro, and you never pay for fine voxels you don't
// need. This is the C++ home of the v2 wall-cost rule: a 1/3-cube-thick wall sits
// on subcube boundaries and coarsens to ~9 subcubes per linear cube (cheap); a
// 2-micro wall does NOT and stays raw micro (162 per cube). Detailers paint; the
// exporter handles efficiency.
//
// Coordinates are in MICRO-grid units (gx,gy,gz) unless a helper takes cube /
// subcube indices. AIR is the empty string "" — painting AIR erases a cell.
// ============================================================================

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "core/VoxelTemplate.h"

namespace Phyxel {
namespace Core {

/// Voxel resolution a coarsened cell exports at.
enum class CanvasRes { Cube, Subcube, Microcube };

/// One exported voxel (the optimal C/S/M mix). `sub` is valid for Subcube and
/// Microcube; `micro` only for Microcube.
struct CanvasVoxel {
    CanvasRes   res = CanvasRes::Cube;
    glm::ivec3  cube{0};    ///< cube coordinate
    glm::ivec3  sub{0};     ///< subcube within cube (0..2 per axis)
    glm::ivec3  micro{0};   ///< microcube within subcube (0..2 per axis)
    std::string material;
};

/// Voxel-count report. `microCells()` is what a naive all-microcube encoding
/// would have cost, so `summary()` can show how much coarsening saved.
struct ResolutionReport {
    int cubes = 0;
    int subcubes = 0;
    int microcubes = 0;

    int total() const { return cubes + subcubes + microcubes; }
    long long microCells() const {
        return static_cast<long long>(cubes) * 729 + static_cast<long long>(subcubes) * 27 + microcubes;
    }
    double savingsPercent() const {
        long long naive = microCells();
        return naive ? (1.0 - static_cast<double>(total()) / static_cast<double>(naive)) * 100.0 : 0.0;
    }
    std::string summary() const;
};

class MicroCanvas {
public:
    static constexpr int MICRO_PER_CUBE = 9;   ///< 9 micro-cells span one cube
    static constexpr int MICRO_PER_SUB  = 3;   ///< 3 micro-cells span one subcube

    // ----- painting in micro space -----
    /// Set (mat != "") or erase (mat == "", i.e. AIR) a single micro cell.
    void setMicroCell(int gx, int gy, int gz, const std::string& mat);
    /// Fill (or carve, if mat == "") a box in micro-grid units.
    void fillMicroBox(int gx, int gy, int gz, int gw, int gh, int gd, const std::string& mat);

    // ----- painting with coarse helpers -----
    void addCube(int cx, int cy, int cz, const std::string& mat);
    void addSubcube(int cx, int cy, int cz, int sx, int sy, int sz, const std::string& mat);
    void addMicro(int cx, int cy, int cz, int sx, int sy, int sz,
                  int mx, int my, int mz, const std::string& mat);
    /// Fill a w*h*d box of whole cubes (the cheap bulk path).
    void fillCubeBox(int cx, int cy, int cz, int w, int h, int d, const std::string& mat);

    // ----- detailers -----
    /// 45-degree chamfer of a box edge: carve a triangular wedge of micro cells.
    /// `axis` ("x"|"y"|"z") = the edge's running direction; `corner` names the two
    /// perpendicular faces meeting at the edge, e.g. "+y+z" (top-front), "-x+y".
    /// `depth` = chamfer size in micro cells. (course/frame detailers arrive with
    /// the P3 openings/finish pass.)
    void chamferEdge(int gx, int gy, int gz, int gw, int gh, int gd,
                     const std::string& axis, const std::string& corner, int depth);

    // ----- export (greedy coarsening) -----
    std::vector<CanvasVoxel> exportVoxels() const;
    ResolutionReport report() const;

    /// Populate a VoxelTemplate (cubes/subcubes/microcubes vectors) for the asset
    /// library path. Appends; does not clear `out`.
    void toVoxelTemplate(VoxelTemplate& out) const;

    bool empty() const { return m_cells.empty(); }
    size_t cellCount() const { return m_cells.size(); }

    // ----- read access (for the AssetValidator / geometry gates) -----
    bool occupiedMicro(int gx, int gy, int gz) const {
        return m_cells.find(glm::ivec3(gx, gy, gz)) != m_cells.end();
    }
    /// Micro-grid AABB of all occupied cells. Returns false if empty.
    bool microBounds(glm::ivec3& lo, glm::ivec3& hi) const;
    /// All occupied micro coordinates (small for assets; used for connectivity).
    std::vector<glm::ivec3> occupiedCells() const;

private:
    struct IVec3Hash {
        size_t operator()(const glm::ivec3& v) const {
            // 21 bits per axis, signed-safe via a large offset; structures are
            // local-frame and small, so this never collides in practice.
            uint64_t x = static_cast<uint64_t>(v.x + (1 << 20)) & 0x1fffff;
            uint64_t y = static_cast<uint64_t>(v.y + (1 << 20)) & 0x1fffff;
            uint64_t z = static_cast<uint64_t>(v.z + (1 << 20)) & 0x1fffff;
            return std::hash<uint64_t>()(x | (y << 21) | (z << 42));
        }
    };

    std::unordered_map<glm::ivec3, std::string, IVec3Hash> m_cells;
};

} // namespace Core
} // namespace Phyxel
