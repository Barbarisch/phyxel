#pragma once
// docs/VoxelFoundationPlan.md §6.5 step 3 + §6.7 -- bodies (F5).
//
// F5.1 extraction (CPU reference; voxfound_f51_prediction.txt): after a break, every voxel of a loose piece leaves
// the world grid. A Body piece becomes a small grid (its bounding box) with mass, centre of mass and inertia
// computed from its voxels; a Crumb piece's voxels become crumbs (counted, never deleted; F6 makes them powder).
// The hit's momentum is shared over the bodies with the stress falloff of §6.4, applied at each body's voxel
// nearest the hit. Anchored pieces stay in the world untouched.

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "core/voxfound/FractureLab.h"
#include "core/voxfound/VoxelGrid.h"

namespace Phyxel::Core::VoxFound {

struct Body {
    uint32_t pieceId = 0;                     // the FractureResult piece it came from
    VoxelGrid grid;                           // the piece's bounding box; fixed is all 0; damage always present
    glm::ivec3 origin{0};                     // world voxel coordinates of grid voxel (0, 0, 0)
    uint32_t voxels = 0;
    double mass = 0.0;                        // kg
    glm::dvec3 com{0.0};                      // centre of mass, world voxel units (voxel (i,j,k) spans [i, i+1))
    glm::dmat3 inertia{0.0};                  // kg m^2, about com, world axes
    glm::dvec3 vel{0.0};                      // m / s
    glm::dvec3 angVel{0.0};                   // rad / s, world axes
    glm::ivec3 contact{0};                    // world voxel where the hit's share was applied
};

struct Crumb {
    glm::ivec3 pos{0};                        // world voxel
    uint8_t mat = 0;
    uint16_t part = 0;
    uint8_t damage = 0;
};

struct Extraction {
    bool implemented = false;
    std::vector<Body> bodies;                 // in ascending piece id
    std::vector<Crumb> crumbs;                // in ascending world voxel index
};

/// Removes every Body / Crumb voxel of `r` from `world` and returns them. `world` must be the grid `r` was
/// computed on; the hit's damage is taken from r.damage (the caller's world gets it applied for the voxels
/// that stay). Deterministic.
Extraction extractBodies(VoxelGrid& world, const FractureResult& r, const Hit& hit,
                         const std::vector<FractureMaterial>& mats);

/// The mass properties of a body's voxels by brute force (double, per voxel cube); the independent check.
void bodyMassBruteForce(const Body& b, const std::vector<FractureMaterial>& mats, double& mass, glm::dvec3& com,
                        glm::dmat3& inertia);

}  // namespace Phyxel::Core::VoxFound
