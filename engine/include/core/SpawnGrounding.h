#pragma once

#include <functional>

namespace Phyxel {
namespace Core {

// ============================================================================
// Spawn grounding — keep entity/object placement sane by refusing to bury a
// spawn inside solid voxels.
//
// The engine must not trust a caller-supplied spawn position blindly: placing a
// character at a cell that is already solid buries it in terrain (observed:
// spawn at y=16 with the surface at y=16). Callers pass a solidity predicate
// `isSolid(x,y,z)` (typically `chunkManager->hasVoxelAt`) and this returns a Y
// that is NOT inside solid.
//
// Behavior:
//   - If (x,y,z) is empty -> return y unchanged (respects intentional airborne
//     spawns, e.g. a fall test; this only fixes the buried case).
//   - If (x,y,z) is solid -> climb to the first air cell above the contiguous
//     solid column and return that Y (a sane standable surface).
//   - `searchUp` caps the climb; if the column is solid all the way to the cap,
//     return the cap (best effort — caller should surface a warning).
//
// Pure and dependency-free so it is unit-testable without a ChunkManager.
// ============================================================================
inline int groundSpawnYIfInsideSolid(const std::function<bool(int, int, int)>& isSolid,
                                     int x, int y, int z, int searchUp = 128) {
    if (!isSolid || !isSolid(x, y, z)) return y;
    const int ceiling = y + searchUp;
    int cy = y;
    while (cy < ceiling && isSolid(x, cy, z)) ++cy;
    return cy;  // first air cell above the solid column (or the cap)
}

} // namespace Core
} // namespace Phyxel
