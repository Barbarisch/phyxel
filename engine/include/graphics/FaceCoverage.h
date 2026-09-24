#pragma once

// FaceCoverage — decode a chunk's emitted face instances into the UNIT FACES they cover, at
// microcube resolution (1/9 of a cube). docs/GlassTransparency.md §7.
//
// WHY MICRO RESOLUTION. Cube faces are always greedy-merged and sub/micro faces are merged when fine
// merge is on, so a quad count can go DOWN when a face is added (two runs rejoin). Counting quads
// cannot answer "which faces are drawn". Expanding every instance (any scale, any merge extent) to
// the micro-sized squares it covers gives one merge-independent, resolution-independent answer:
// a cube face = 81 unit faces, a subcube face = 9, a microcube face = 1. The unit tests and the
// live debug route use this same decoder, so they cannot count differently.
//
// A unit face is identified by the MICRO CELL THAT OWNS IT (the solid micro cell on the inside of
// the face, chunk-local, 0..287 per axis) plus its face direction.

#include "core/Types.h"

#include <cstdint>
#include <vector>

namespace Phyxel {
namespace Graphics {

struct CoveredUnitFace {
    int16_t  x, y, z;       // owning micro cell, chunk-local (0..287)
    uint8_t  faceID;        // 0=+Z 1=-Z 2=+X 3=-X 4=+Y 5=-Y
    uint8_t  transparent;   // InstanceData.reserved bit 1
    uint16_t textureIndex;  // identifies the material's face texture

    bool operator<(const CoveredUnitFace& o) const {
        if (x != o.x) return x < o.x;
        if (y != o.y) return y < o.y;
        if (z != o.z) return z < o.z;
        return faceID < o.faceID;
    }
    bool operator==(const CoveredUnitFace& o) const {
        return x == o.x && y == o.y && z == o.z && faceID == o.faceID;
    }
};

// In-plane axes of a face (MUST match the meshers' merge axes and static_voxel.vert):
//   +Z/-Z: u=x v=y    +X/-X: u=z v=y    +Y/-Y: u=x v=z
inline void faceCoverageAxes(int faceID, int& uAxis, int& vAxis, int& dAxis) {
    if (faceID <= 1)      { uAxis = 0; vAxis = 1; dAxis = 2; }
    else if (faceID <= 3) { uAxis = 2; vAxis = 1; dAxis = 0; }
    else                  { uAxis = 0; vAxis = 2; dAxis = 1; }
}

// Expand every instance into its covered unit faces. Merge extents: cubes carry them in
// packedData bits 20-25 / 26-31 (packCubeFaceDataSized); subcube/microcube faces in light bits
// 16-23 / 24-31 (fine merge). Both are (extent - 1).
inline std::vector<CoveredUnitFace> expandCoveredUnitFaces(const std::vector<InstanceData>& faces) {
    std::vector<CoveredUnitFace> out;
    for (const auto& f : faces) {
        const uint32_t p = f.packedData;
        const int px = static_cast<int>(p & 0x1Fu);
        const int py = static_cast<int>((p >> 5) & 0x1Fu);
        const int pz = static_cast<int>((p >> 10) & 0x1Fu);
        const int faceID = static_cast<int>((p >> 15) & 0x7u);
        const int level  = static_cast<int>((p >> 18) & 0x3u);
        if (faceID > 5) continue;

        int cellSize;       // size of this instance's cell, in micro units
        int origin[3];      // min micro corner of the instance's origin cell
        int eu, ev;         // merge extents, in cells of this instance's own size
        if (level == 0) {
            cellSize = 9;
            origin[0] = px * 9; origin[1] = py * 9; origin[2] = pz * 9;
            eu = static_cast<int>((p >> 20) & 0x3Fu) + 1;
            ev = static_cast<int>((p >> 26) & 0x3Fu) + 1;
        } else {
            const uint32_t se = (p >> 20) & 0x3Fu;
            const int sx = static_cast<int>(se % 3), sy = static_cast<int>((se / 3) % 3),
                      sz = static_cast<int>(se / 9);
            eu = static_cast<int>((f.light >> 16) & 0xFFu) + 1;
            ev = static_cast<int>((f.light >> 24) & 0xFFu) + 1;
            if (level == 1) {
                cellSize = 3;
                origin[0] = px * 9 + sx * 3; origin[1] = py * 9 + sy * 3; origin[2] = pz * 9 + sz * 3;
            } else {
                const uint32_t me = (p >> 26) & 0x3Fu;
                const int mx = static_cast<int>(me % 3), my = static_cast<int>((me / 3) % 3),
                          mz = static_cast<int>(me / 9);
                cellSize = 1;
                origin[0] = px * 9 + sx * 3 + mx;
                origin[1] = py * 9 + sy * 3 + my;
                origin[2] = pz * 9 + sz * 3 + mz;
            }
        }

        int uA, vA, dA;
        faceCoverageAxes(faceID, uA, vA, dA);
        const bool positive = (faceID == 0 || faceID == 2 || faceID == 4);
        const int depth = origin[dA] + (positive ? cellSize - 1 : 0);   // owning micro layer
        const int spanU = eu * cellSize, spanV = ev * cellSize;
        for (int i = 0; i < spanU; ++i) {
            for (int j = 0; j < spanV; ++j) {
                int c[3];
                c[uA] = origin[uA] + i;
                c[vA] = origin[vA] + j;
                c[dA] = depth;
                CoveredUnitFace u;
                u.x = static_cast<int16_t>(c[0]);
                u.y = static_cast<int16_t>(c[1]);
                u.z = static_cast<int16_t>(c[2]);
                u.faceID = static_cast<uint8_t>(faceID);
                u.transparent = static_cast<uint8_t>((f.reserved >> 1) & 1u);
                u.textureIndex = f.textureIndex;
                out.push_back(u);
            }
        }
    }
    return out;
}

}  // namespace Graphics
}  // namespace Phyxel
