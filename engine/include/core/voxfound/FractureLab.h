#pragma once
// docs/VoxelFoundationPlan.md §6 + §8 — the fracture lab (F0 skeleton; the model is built in F4).
//
// The model in one line (§6.3): a break CUTS BONDS along a crack pattern inside the damaged region;
// then whatever is no longer bonded to anything anchored comes loose as pieces.
//
// What exists now (F0):
//  - the rigs of §8, each with its written prediction;
//  - an INDEPENDENT checker: labels pieces from the cut bonds by its own flood fill, decides anchoring,
//    and measures the cuts (between vs within parts, by direction). F4's result must agree with it;
//  - fractureReference(), declared with its final signature but NOT IMPLEMENTED: it returns
//    implemented = false. Every fracture-lab test is therefore red until F4 (red before green).
//
// Bonds: voxel p's bond to p+x / p+y / p+z is stored on p as bit CutPX / CutPY / CutPZ.

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/voxfound/VoxelGrid.h"

namespace Phyxel::Core::VoxFound {

enum class FractureClass : uint8_t { Brittle, Glass, Fibrous };

struct FractureMaterial {
    std::string name;                         // index 0 of a table is air
    FractureClass cls = FractureClass::Brittle;
    float toughness = 1.0f;                   // energy needed to start cutting (lab units)
    glm::vec3 grain{0.0f};                    // fibrous materials: the long axis (unit), else 0
    float partBondScale = 0.3f;               // a bond between two parts is this strong vs within a part
};

struct Hit {
    glm::vec3 point{0.0f};                    // in voxel units of the grid
    glm::vec3 dir{0.0f, 0.0f, 1.0f};          // unit, into the material
    float energy = 0.0f;                      // lab units, compared with toughness
    uint64_t seed = 1;                        // the event's seed (determinism, §6.9)
};

enum CutBit : uint8_t { CutPX = 1, CutPY = 2, CutPZ = 4 };

struct FractureResult {
    bool implemented = false;
    std::vector<uint8_t> cut;                 // per voxel, CutBit flags
    std::vector<uint8_t> damage;              // per voxel, accumulated damage after the hit (§6.4)
    std::vector<uint32_t> piece;              // per voxel: piece id, 0 = air; pieces numbered from 1
    std::vector<uint8_t> pieceAnchored;       // indexed by piece id (entry 0 unused)
};

/// F4. The CPU reference: the source of truth the GPU port must equal. NOT IMPLEMENTED in F0.
FractureResult fractureReference(const VoxelGrid& grid, const Hit& hit,
                                 const std::vector<FractureMaterial>& mats);

// ---- the independent checker -------------------------------------------------------------

struct PieceLabels {
    std::vector<uint32_t> label;              // per voxel, 0 = air
    std::vector<uint32_t> size;               // per piece id (entry 0 unused)
    std::vector<uint8_t> anchored;            // per piece id (entry 0 unused)
    std::vector<glm::vec3> centroid;          // per piece id (entry 0 unused), voxel units
    uint32_t pieces() const { return size.empty() ? 0u : uint32_t(size.size() - 1); }
};

/// Flood fill over face neighbours that are both solid and whose bond is not cut.
PieceLabels labelPieces(const VoxelGrid& grid, const std::vector<uint8_t>& cut);

struct CutStats {
    size_t cut = 0;
    size_t betweenParts = 0;                  // the two voxels have different part ids (both non-zero)
    size_t withinPart = 0;
    size_t alongX = 0, alongY = 0, alongZ = 0;  // the cut bond's direction
};
CutStats cutStats(const VoxelGrid& grid, const std::vector<uint8_t>& cut);

/// True when the result's piece partition and anchoring equal the checker's (up to renumbering).
bool resultMatchesChecker(const VoxelGrid& grid, const FractureResult& r, std::string* why = nullptr);

// ---- the rigs (§8) -----------------------------------------------------------------------
// Each is small and fits inside one chunk. Voxel size is stated per rig. Provenance: built by these
// functions from simple shapes (hand-specified rigs, not generator output) — stated as such.

struct Rig {
    std::string name;
    float voxelM = 0.125f;                    // world voxels 1/8 m; items 1/64 m
    VoxelGrid grid;
    std::vector<FractureMaterial> mats;       // mats[0] = air
    Hit hit;
    std::string prediction;
};

std::vector<FractureMaterial> labMaterials();  // air, Stone, Bricks, Wood, Glass, WoodItem
enum LabMat : uint8_t { kAir = 0, kStone = 1, kBricks = 2, kWood = 3, kGlass = 4, kWoodItem = 5 };

Rig rigStoneWall(glm::vec3 hitPoint, float energyOverToughness);  // 3 x 2 x 0.375 m, base fixed
Rig rigBrickWall(float energyOverToughness);   // 4 x 2 x 0.25 m, 4x2x2-voxel bricks, running bond
Rig rigBeam(float energyOverToughness);        // 4 m x 25 cm x 25 cm, grain along x, ends fixed
Rig rigGlassPane(float energyOverToughness);   // 3 x 3 m x 1 voxel, frame fixed
Rig rigColumnLintel(float energyOverToughness);// one column under a lintel, base fixed
Rig rigChair(float energyOverToughness);       // an item at 1/64 m: seat, 4 legs, back as parts

}  // namespace Phyxel::Core::VoxFound
