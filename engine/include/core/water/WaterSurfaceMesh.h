#pragma once

#include "core/water/WaterCore.h"

#include <cstdint>
#include <glm/glm.hpp>
#include "core/water/WaterLook.h"
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
/// 19.7: a part-full cell UNDER water is an air pocket the projection keeps (a liquid cell is projected as
/// full, so nothing closes it) - the plain height function (19.5) drew each one as a 16 cm dip, with the
/// displaced water piled +3.5 cm elsewhere. A run's surface = its bottom + the water in it + each cell's
/// missing water x pocketWeight(fill of the cell directly ABOVE it): 0 up to kSurfacePocketA0 (a film or a
/// thin sheet over a part-full cell - that cell IS the surface, or a sheet climbing a wall), 1 from
/// kSurfacePocketA1 (a liquid cell sits on it - it is a pocket), linear between, so the surface stays
/// continuous in time (a hard 'highest cell >= 0.5' rule jumped 23 cm; weighting by ALL the water above
/// drew wall sheets as +12 cm bumps). Same rule in wc_surface.comp and WaterGrid::surfaceWorldY.
constexpr float kSurfacePocketA0 = 0.2f, kSurfacePocketA1 = 0.7f;
inline float surfacePocketWeight(float fillAbove) {
    const float t = (fillAbove - kSurfacePocketA0) / (kSurfacePocketA1 - kSurfacePocketA0);
    return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}

/// One sub-column of the field. Matches wc_surface.comp's layout exactly (16 floats = 64 B).
/// Phase G2: foam (0..1) and the surface velocity (m/s) ride along - the band fills both from its
/// solver; the 3-D volumes export their top cell's horizontal velocity (E2, WaterCore.md 19) and
/// foam 0.
struct SurfaceColumn {
    float top[kSurfaceMaxRuns] = {0, 0, 0, 0};      ///< world Y of each run's surface (bottom-up)
    float bottom[kSurfaceMaxRuns] = {0, 0, 0, 0};   ///< world Y of each run's base
    float solidTopY = -1e30f;                        ///< top face of the highest solid cell in the column (-1e30 = none)
    float runs = 0.0f;                               ///< run count (float for the GPU layout)
    float foam = 0.0f;                               ///< 0..1 whitewater on the top run (G2)
    float u = 0.0f, w = 0.0f;                        ///< surface velocity, x and z (m/s) (G2)
    float pad0 = 0.0f, pad1 = 0.0f, pad2 = 0.0f;
};
static_assert(sizeof(SurfaceColumn) == 64, "wc_surface.comp writes 16 floats per column");

struct WaterSurfaceField {
    glm::ivec3 origin{0};     ///< grid origin in cells (world = origin * h)
    int   nx = 0, nz = 0;
    float h = 1.0f;
    std::vector<SurfaceColumn> cols;   ///< nx * nz, index x + nx * z
    WaterLookPacked look;              ///< G3: the body's look, resolved at the field's centre by its producer
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
    float     foam;      ///< 0..1 whitewater (G2)
    glm::vec2 flow;      ///< surface velocity, x and z (m/s) (G2)
    float     pad = 0.0f;
};
static_assert(sizeof(WaterSurfaceVertex) == 48, "water_surface.vert expects 48-byte vertices");

/// G3: one draw per field, so each body's look reaches the shader in the push block.
struct WaterSurfaceRange { uint32_t firstIndex = 0, indexCount = 0; WaterLookPacked look; };

struct WaterSurfaceMesh {
    std::vector<WaterSurfaceVertex> vertices;
    std::vector<uint32_t> indices;
    long topQuads = 0, sideQuads = 0;
    std::vector<WaterSurfaceRange> ranges;   ///< G3: one per appended field (its look)
    void clear() { vertices.clear(); indices.clear(); ranges.clear(); topQuads = sideQuads = 0; }
};

/// Pure. Appends the volume's mesh to `out`.
void buildWaterSurfaceMesh(const WaterSurfaceField& field, WaterSurfaceMesh& out);
/// G3: appends the field's mesh AND a draw range carrying the field's look (empty fields add no range).
void appendFieldToMesh(const WaterSurfaceField& field, WaterSurfaceMesh& out);

/// Phase F render mode of the simulated water (docs/WaterCore.md 17.2 key 4).
enum class WaterCoreRenderMode : int { Off = 0, Cells = 1, Mesh = 2 };
constexpr WaterCoreRenderMode kWaterCoreRenderModeDefault = WaterCoreRenderMode::Mesh;
inline const char* waterCoreRenderModeName(WaterCoreRenderMode m) { return m == WaterCoreRenderMode::Mesh ? "mesh" : (m == WaterCoreRenderMode::Cells ? "cells" : "off"); }

}  // namespace Phyxel::Core::Water
