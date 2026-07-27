#pragma once

// ============================================================================
// AssemblyPlan — the DERIVED physical anatomy (level 4 of Structure Generation
// v2; docs/structure-generation/StructureGenerationV2.md). "How it's built."
//
// Computed deterministically in-engine from a BuildingProgram + a StyleProfile +
// live terrain by the (future) StructureRealizer, then realized into voxels via
// the MicroCanvas. P0 defines the TYPES + round-trip serialization so the
// realizer and the post-build geometry gates share one contract; the realizer
// that populates it arrives in P1.
//
// Coordinates are world-space cubes unless noted. Thicknesses are in cubes.
// ============================================================================

#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

/// One footprint column of the foundation, with its terrain-adaptive depth.
/// `bearingY` is where the footing reaches solid ground (stepped on slopes);
/// `topY` is the top of the foundation (where the floor system sits).
struct FoundationColumn {
    int x = 0, z = 0;
    int bearingY = 0;
    int topY = 0;
    std::string material;

    static FoundationColumn fromJson(const nlohmann::json& j);
    nlohmann::json toJson() const;
};

/// A straight wall run (exterior | interior | foundation) from (x0,z0) to (x1,z1).
struct WallSegment {
    int x0 = 0, z0 = 0, x1 = 0, z1 = 0;
    int baseY = 0, height = 0;
    double thickness = 0.333;       ///< in cubes (subcube-thick default)
    std::string material;
    std::string type = "exterior";  ///< exterior | interior | foundation

    static WallSegment fromJson(const nlohmann::json& j);
    nlohmann::json toJson() const;
};

/// A floor/ceiling slab patch (subfloor, finish floor, inter-story floor, ceiling).
struct FloorPatch {
    int x = 0, z = 0, w = 0, d = 0;
    int y = 0;
    double thickness = 0.333;
    std::string material;
    std::string role = "floor";     ///< floor | ceiling | subfloor

    static FloorPatch fromJson(const nlohmann::json& j);
    nlohmann::json toJson() const;
};

/// One realized box of an opening's reveal, in structure-local MICRO coords —
/// recorded by the realizer at the moment it paints (Claims Ledger increment 2).
struct TrimBox {
    int x = 0, y = 0, z = 0;
    int w = 0, h = 0, d = 0;
    std::string role;               ///< clear (carved air) | jamb | lintel | sill | ledge | leaf
    std::string material;           ///< "" for carved air (role "clear")

    static TrimBox fromJson(const nlohmann::json& j);
    nlohmann::json toJson() const;
};

/// A cut opening (door | window | arch) to be carved + framed.
struct OpeningCut {
    int x = 0, y = 0, z = 0;
    int w = 0, h = 0, d = 0;
    std::string kind = "door";      ///< door | window | arch
    std::string infill = "open";    ///< open | glass | shutter | boarded
    std::vector<TrimBox> reveal;    ///< realized carve + trim boxes (micro; increment 2)

    static OpeningCut fromJson(const nlohmann::json& j);
    nlohmann::json toJson() const;
};

/// A quoined corner zone at a footprint corner — the vertical band of alternating
/// dressed corner blocks (realizer pass 4.5). Corner cell in footprint-local
/// cubes; leg lengths in micro (grounded block dims, TrimGrounding.md).
struct CornerZone {
    int x = 0, z = 0;               ///< corner cube (footprint-local)
    int dx = 1, dz = 1;             ///< inward direction along each facade
    int baseY = 0, topY = 0;        ///< cube y range [baseY, topY)
    int legLongMicro = 4, legShortMicro = 3;
    std::string material;

    static CornerZone fromJson(const nlohmann::json& j);
    nlohmann::json toJson() const;
};

/// A roof panel/surface (gable | hip slope | flat) over an outline region.
struct RoofPanel {
    int x0 = 0, z0 = 0, x1 = 0, z1 = 0;
    int eaveY = 0;
    double pitch = 0.8;
    std::string style = "gable";    ///< gable | hip | flat
    std::string material;

    static RoofPanel fromJson(const nlohmann::json& j);
    nlohmann::json toJson() const;
};

/// A realized stair flight connecting two ADJACENT stories — recorded by the
/// realizer's stair pass at the moment it builds the treads (Claims Ledger
/// increment 1; docs/structure-generation/ClaimsLedger.md). The well rect is
/// footprint-local cubes; the well-hole cut through the upper floor slab is in
/// structure-local MICRO coords (the hole is genuinely sub-cube).
struct StairRecord {
    int x = 0, z = 0, w = 0, d = 0;          ///< stair well rect (cubes)
    int fromStory = 0, toStory = 1;          ///< normalized: fromStory < toStory
    int baseY = 0;                           ///< first-tread cube Y (lower walkable)
    int topY = 0;                            ///< one PAST the emergence-slab cube (exclusive)
    int botWalkMicro = 0, topWalkMicro = 0;  ///< exact walkable surfaces (micro Y)
    std::string form = "switchback";         ///< StairPlanner form actually built
    int holeX = 0, holeZ = 0;                ///< upper-slab hole min corner (micro)
    int holeW = 0, holeD = 0;                ///< upper-slab hole size (micro)

    static StairRecord fromJson(const nlohmann::json& j);
    nlohmann::json toJson() const;
};

/// A fixture/furniture placement resolved to a world cell + rotation.
struct FixturePlacement {
    std::string archetype;          ///< DimensionCanon archetype (chair_dining, ...)
    std::string templateName;       ///< approved asset selected from the library
    glm::ivec3  worldPos{0};
    int rotation = 0;               ///< 0/90/180/270

    static FixturePlacement fromJson(const nlohmann::json& j);
    nlohmann::json toJson() const;
};

/// A point light co-located with a lamp fixture.
struct LightPlacement {
    glm::vec3 pos{0.0f};
    glm::vec3 color{1.0f, 0.8f, 0.5f};
    double radius = 7.0;

    static LightPlacement fromJson(const nlohmann::json& j);
    nlohmann::json toJson() const;
};

struct AssemblyPlan {
    std::vector<FoundationColumn> foundation;
    std::vector<WallSegment>      walls;
    std::vector<FloorPatch>       floors;
    std::vector<OpeningCut>       openings;
    std::vector<StairRecord>      stairs;
    std::vector<CornerZone>       corners;
    std::vector<RoofPanel>        roof;
    std::vector<FixturePlacement> fixtures;
    std::vector<LightPlacement>   lights;

    /// Structural-feature classifier: what part of the building occupies a LOCAL cube
    /// cell (plan coords are footprint-local cubes; callers subtract the placed world
    /// origin first). Returns "opening" | "quoin" | "wall" | "stair" | "floor" |
    /// "ceiling" | "foundation" | "roof" | "" (open interior/exterior space). A carved
    /// doorway/window cube answers "opening" (from the recorded clear reveal), a
    /// quoined corner cube "quoin" — both are more specific than the wall band they
    /// sit in. Consumers should ask the anatomy — never sniff voxel materials — so
    /// "is this a wall?" keeps working whatever the style palette.
    std::string featureAt(const glm::ivec3& cubePos) const;

    static AssemblyPlan fromJson(const nlohmann::json& j);
    nlohmann::json toJson() const;
};

} // namespace Core
} // namespace Phyxel
