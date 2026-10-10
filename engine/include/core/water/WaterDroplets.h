#pragma once
// docs/WaterCore.md 21.2 - the droplet crown. This file is the parcel finder: the water the grid cannot
// hold. A DETACHED run is a vertical run of cells holding water (f >= film) in one column whose cell below
// is air (f + s < film; outside the grid is solid, so a run on the grid floor is not detached). It is
// ISOLATED when the cell above its top and the four lateral neighbours of every one of its cells are air
// as well - a scrap flying free (21.2: the 19.7 pockets and a crown rim are connected sideways or below and
// never qualify; a film on a wall has a solid neighbour). A pure function of the fill and body fields, so
// it is the same on the CPU grid and on a GPU volume's read-back.

#include <glm/glm.hpp>
#include <vector>

namespace Phyxel::Core::Water {

struct DetachedRun {
    glm::ivec3 bottom{0};   ///< grid-local cell of the run's lowest cell
    int cells = 0;          ///< run length (cells, upward from `bottom`)
    float sumF = 0.0f;      ///< the run's water in cell volumes (sum of f)
    bool isolated = false;  ///< 21.2: nothing but air around it
};

/// Every detached run in a grid of `dims` (x fastest, then y, then z - WaterGrid::idx). `s` may be null (no bodies).
std::vector<DetachedRun> findDetachedRuns(const glm::ivec3& dims, const float* f, const float* s, float film);

}  // namespace Phyxel::Core::Water
