#pragma once

#include "core/water/WaterCore.h"

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

// WaterCore Phase F (docs/WaterCore.md 17): the simulated water's own surface, at the volume's own
// resolution, every frame.
//
//   grid cells  --extractSurfaceField-->  WaterSurfaceField  --buildWaterSurfaceMesh-->  mesh
//
// The FIELD is one record per sub-column (the volume's own (x, z) lattice): up to kMaxRuns runs of
// water [bottomY, topY) in world units, bottom-up, plus the top face of the highest solid in the
// column (what a neighbour's lateral face stops at). The CPU extracts it from the grid; the GPU
// kernel wc_surface.comp writes the same layout into a buffer that is read back each step - never
// the whole grid. The MESH is a pure function of the field: a top quad per run with corner heights
// averaged over the neighbouring sub-columns that hold an overlapping run (C0-continuous inside the
// volume), and a lateral face wherever a run meets a neighbour that holds no overlapping run and is
// not solid up to the run's top (a step, an overhang, the lip of a fall). Beside a solid the top edge
// ends at the solid: the voxel is the wall.
namespace Phyxel::Core::Water {

constexpr int kSurfaceMaxRuns = 4;

/// One sub-column of the field. Matches wc_surface.comp's layout exactly (12 floats = 48 B).
struct SurfaceColumn {
    float top[kSurfaceMaxRuns] = {0, 0, 0, 0};      ///< world Y of each run's surface (bottom-up)
    float bottom[kSurfaceMaxRuns] = {0, 0, 0, 0};   ///< world Y of each run's base
    float solidTopY = -1e30f;                        ///< top face of the highest solid cell in the column (-1e30 = none)
    float runs = 0.0f;                               ///< run count (float for the GPU layout)
    float pad0 = 0.0f, pad1 = 0.0f;
};
static_assert(sizeof(SurfaceColumn) == 48, "wc_surface.comp writes 12 floats per column");

struct WaterSurfaceField {
    glm::ivec3 origin{0};     ///< grid origin in cells (world = origin * h)
    int   nx = 0, nz = 0;
    float h = 1.0f;
    std::vector<SurfaceColumn> cols;   ///< nx * nz, index x + nx * z
    const SurfaceColumn& at(int x, int z) const { return cols[static_cast<size_t>(x) + static_cast<size_t>(nx) * z]; }
};

/// CPU extraction from the grid (runs of layers with f * h >= 1 mm; the top of a run is its highest
/// wet cell's bottom + fill * h; solidTopY from the grid's occupancy).
void extractSurfaceField(const WaterGrid& grid, WaterSurfaceField& out);

/// 32 B, the vertex layout of water_surface.vert.
struct WaterSurfaceVertex {
    glm::vec3 pos;       ///< world
    float     depth;     ///< the run's thickness (m), the shader's thickness floor
    glm::vec3 normal;    ///< world
    float     side;      ///< 0 = top face, 1 = lateral face
};
static_assert(sizeof(WaterSurfaceVertex) == 32, "water_surface.vert expects 32-byte vertices");

struct WaterSurfaceMesh {
    std::vector<WaterSurfaceVertex> vertices;
    std::vector<uint32_t> indices;
    long topQuads = 0, sideQuads = 0;
    void clear() { vertices.clear(); indices.clear(); topQuads = sideQuads = 0; }
};

/// Pure. Appends the volume's mesh to `out`.
void buildWaterSurfaceMesh(const WaterSurfaceField& field, WaterSurfaceMesh& out);

/// Phase F render mode of the simulated water (docs/WaterCore.md 17.2 key 4).
enum class WaterCoreRenderMode : int { Off = 0, Cells = 1, Mesh = 2 };
constexpr WaterCoreRenderMode kWaterCoreRenderModeDefault = WaterCoreRenderMode::Mesh;
inline const char* waterCoreRenderModeName(WaterCoreRenderMode m) { return m == WaterCoreRenderMode::Mesh ? "mesh" : (m == WaterCoreRenderMode::Cells ? "cells" : "off"); }

}  // namespace Phyxel::Core::Water
