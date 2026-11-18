#pragma once
// docs/VoxelFoundationPlan.md §6 + §8 — the fracture lab (F0 skeleton; the model is built in F4).
//
// The model in one line (§6.3): a break CUTS BONDS along a crack pattern inside the damaged region;
// then whatever is no longer bonded to anything anchored comes loose as pieces.
//
// What exists:
//  - the rigs of §8, each with its written prediction (F0);
//  - an INDEPENDENT checker: labels pieces from the cut bonds by its own flood fill, decides anchoring,
//    and measures the cuts (between vs within parts, by direction). The reference must agree with it (F0);
//  - fractureReference() (F4.1): stress falloff from the hit, bond strengths (parts = mortar, grain, damage),
//    crack patterns (nested Voronoi levels for brittle / fibrous, polar for glass, small parts move whole),
//    pieces by union-find, anchoring, bodies vs crumbs, damage. Model + predictions:
//    docs/evidence/voxfound_f41_prediction.txt; plan §9c.
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
    float density = 1000.0f;                  // kg / m^3 (F5.1: body mass, centre, inertia)
};

struct Hit {
    glm::vec3 point{0.0f};                    // in voxel units of the grid
    glm::vec3 dir{0.0f, 0.0f, 1.0f};          // unit, into the material
    float energy = 0.0f;                      // lab units, compared with toughness
    uint64_t seed = 1;                        // the event's seed (determinism, §6.9)
    float impulse = 0.0f;                     // N s along dir, shared over the loose bodies (F5.1)
};

enum CutBit : uint8_t { CutPX = 1, CutPY = 2, CutPZ = 4 };

enum class PieceKind : uint8_t { Anchored = 0, Body = 1, Crumb = 2 };

struct FractureResult {
    bool implemented = false;
    std::vector<uint8_t> cut;                 // per voxel, CutBit flags
    std::vector<uint8_t> damage;              // per voxel, accumulated damage after the hit (§6.4)
    std::vector<uint32_t> piece;              // per voxel: piece id, 0 = air; pieces numbered from 1
    std::vector<uint8_t> pieceAnchored;       // indexed by piece id (entry 0 unused)
    std::vector<uint32_t> pieceSize;          // voxels per piece id (entry 0 unused)
    std::vector<PieceKind> pieceKind;         // per piece id (entry 0 unused)
    size_t candidates = 0;                    // bonds on a weak plane that the stress reached
    size_t bondsTested = 0;
};

/// Tier knobs (§6.9): they change how loose matter is simulated, never which bonds break.
struct FractureKnobs {
    uint32_t crumbVoxels = 4;                 // loose pieces of fewer voxels (of the grid's own size) are crumbs
};

/// The model's constants (fixed in voxfound_f41_prediction.txt before the first run).
struct FractureModel {
    static constexpr float kStressLengthM = 0.25f;   // l: s(d) = E / (1 + (d / l)^2)
    static constexpr float kLevel0M = 1.0f;          // pattern level 0 spacing
    static constexpr float kCellM = 0.5f;            // h(d) = kCellM (1 + d / l) / cbrt(e)
    static constexpr float kGrainStretch = 4.0f;     // fibrous cells this much longer along the grain (same volume)
    static constexpr float kGrainBond = 4.0f;        // a bond across the fibres is this much stronger
    static constexpr float kDamageWeaken = 0.5f;     // full damage halves a bond
    static constexpr float kGlassRing = 1.7f;        // glass ring radius ratio
};

/// F4.3: the per-hit scalars and tables, computed once on the host and shared by the CPU reference and the GPU
/// port. Everything per bond / per voxel is then integer arithmetic (docs/evidence/voxfound_f43_prediction.txt).
/// Fixed-point units: positions in 1/16 voxel ("q16"); the Voronoi in 1/1024 voxel.
struct FracturePlan {
    bool valid = false;                       // a solid voxel was hit
    uint8_t hitMat = 0;
    double eOverT = 0.0;
    int64_t eq = 0;                           // E in 1/16
    int32_t cq = 0;                           // round(256 cbrt(E/T))
    int32_t L = 0;                            // the stress length (0.25 m) in q16
    int32_t hit16[3] = {0, 0, 0};             // the hit point in q16
    int kmax = 0;                             // finest pattern level (spacing >= one voxel)
    uint64_t seed = 1;
    std::vector<int64_t> cellC;               // per level k: [2k] plain, [2k + 1] fibrous Voronoi spacing (1/1024 voxel)
    std::vector<int32_t> tq;                  // per material: toughness in 1/16 (>= 1)
    std::vector<int32_t> partQ;               // per material: part bond scale in 1/1000
    std::vector<int8_t> grainAxis;            // per material: -1 or the grain axis
    std::vector<uint8_t> glass;               // per material
    std::vector<uint8_t> partWhole;           // per part id (dense): the part moves as a unit
    int glassAxis = 2, glassU = 0, glassV = 1;  // the glass plane: across the hit's dominant axis
    int sectors = 4;
    std::vector<int64_t> ringR2;              // ring = count of entries <= rho^2 (q16 squared)
    std::vector<int32_t> bounds;              // ring r >= 1: sectors x (x, y) at (r - 1) * sectors * 2, sorted by angle
};
FracturePlan planFracture(const VoxelGrid& g, const Hit& hit, const std::vector<FractureMaterial>& mats);

/// F4. The CPU reference: the source of truth the GPU port must equal.
FractureResult fractureReference(const VoxelGrid& grid, const Hit& hit,
                                 const std::vector<FractureMaterial>& mats,
                                 const FractureKnobs& knobs = {});

/// Number pieces from a parent forest (par[root] == root) in order of each piece's first voxel and fill piece,
/// pieceAnchored, pieceSize, pieceKind. Shared by the CPU reference and the GPU port.
void finishPieces(const VoxelGrid& g, FractureResult& r, std::vector<uint32_t>& par, const FractureKnobs& knobs);

/// Write a result's damage into the grid (progressive damage: the next hit sees it).
void applyDamage(VoxelGrid& grid, const FractureResult& r);

// ---- F4.2 anchoring at scale (§3.5a point 6): block graph first, voxels only near the cut ---------------

/// What the store keeps per block (8^3 voxels): the solid voxels' components inside the block and which of
/// them hold a fixed voxel.
struct BlockSummary {
    static constexpr int kB = 8;
    glm::ivec3 nb{0};                         // blocks per axis
    std::vector<uint16_t> local;              // per voxel: 1-based component inside its block, 0 = air
    std::vector<uint32_t> compBase;           // per block: index of its first component in compFixed
    std::vector<uint8_t> compFixed;           // per component (flat): holds a fixed voxel
    size_t components = 0;
    size_t blockIndex(glm::ivec3 b) const { return size_t(b.z) + size_t(nb.z) * (size_t(b.y) + size_t(nb.y) * size_t(b.x)); }
};
BlockSummary summarizeBlocks(const VoxelGrid& g);

struct AnchorStats {
    size_t searches = 0;
    size_t voxelNodes = 0;                    // voxels visited one by one (dirty blocks)
    size_t compNodes = 0;                     // (block, component) nodes visited
    size_t faceScans = 0;                     // voxel pairs examined on block faces
    size_t dirtyBlocks = 0;
    size_t looseVoxels = 0;
    size_t loosePieces = 0;
};

/// The pieces a new cut set frees: per voxel a loose piece id (1-based), 0 = anchored or not touched by the cut.
/// Exact on a grid that was fully anchored before the cut.
std::vector<uint32_t> findLoose(const VoxelGrid& g, const BlockSummary& s, const std::vector<uint8_t>& cut,
                                AnchorStats* stats = nullptr);

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
