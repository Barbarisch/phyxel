#pragma once

// ============================================================================
// FurniturePlacer — the algorithm that decides furniture placement (Structure
// Generation v2). The point: NOTHING about furniture is hand-authored. Given a
// room's purpose + size + the door/window openings on its walls, this derives:
//   - WHAT furniture the room gets (by purpose),
//   - which WALL each piece backs onto (or the room centre),
//   - the FACING so the piece's front points INTO the room,
//   - CLEARANCE: never on a door wall / in a doorway, never overlapping, on the
//     floor (no floating).
//
// Engine rotation convention (front direction at each rotation):
//   rot 0   -> front +z      rot 90  -> front -x
//   rot 180 -> front -z      rot 270 -> front +x
// So a piece against the MIN-X (left) wall faces +x  => rot 270 (NOT "east").
// ============================================================================

#include <map>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/AssemblyPlan.h"      // AssemblyPlan (Claims Ledger: furnish from the plan)
#include "core/BuildingProgram.h"   // ProgStory, ProgRoom, ProgPortal, Rect

namespace Phyxel { class VoxelTemplate; }

namespace Phyxel {
namespace Core {

struct FurniturePlacement {
    std::string type;        ///< fixture type (fireplace, bed, table, counter, chest, bench, ...)
    glm::ivec3  worldPos{0}; ///< template origin in world space (CUBES), on the floor
    int         rotation = 0;///< 0/90/180/270 — front faces INTO the room
    std::string room;        ///< owning room id
    glm::ivec3  backDir{0};  ///< OUTWARD normal of the wall this piece backs onto (0 = centre/interior);
                             ///< the consumer insets the piece by the wall thickness along -backDir so
                             ///< it sits flush against the wall's interior face, not inside the wall.
    int         insetMicroX = -1; ///< KI-5b: PER-AXIS wall-band insets — exterior thickness on
    int         insetMicroZ = -1; ///< footprint-edge walls, the straddling band's half on interior
                             ///< partitions (a sconce inset 9 micro off a 2-micro partition floated
                             ///< ~0.8 m off the wall; a CORNER piece needs different insets per
                             ///< axis to be flush to both walls). -1 = legacy (extTMicro param).
};

/// A fixture's real footprint in CUBES (from the asset library's .metrics.json bounding box).
/// `width` = extent across the front (along the wall it backs onto); `depth` = front-to-back
/// (extends into the room). Default 1×1 keeps a piece a single cell (legacy / unknown asset).
struct Footprint {
    int width = 1;    ///< cubes along the wall
    int depth = 1;    ///< cubes into the room
    int microW = 0;   ///< template micro extent along x (max micro index; 0 = unknown -> width*9-1)
    int microD = 0;   ///< template micro extent along z
    int microH = 0;   ///< template micro HEIGHT (ceiling-hung pieces need it; 0 = unknown)
};

/// The set of world CUBES a micro-placed template ACTUALLY occupies — the single source of truth for
/// reservation (planner), registration (bbox), and render. It captures the "micro-spill": the anchor is
/// inset off the wall by `extTMicro` on each `backDir` axis, which pushes the render off the cube grid so
/// a zero-margin template spills +1 cube. `microW/microD` = the template's micro extents (max micro
/// index, min 0); they SWAP at 90/270. `baseCube` = the footprint MIN corner (world cubes). Inclusive.
struct CubeSpan {
    int minX = 0, minZ = 0, maxX = 0, maxZ = 0;
    int width()  const { return maxX - minX + 1; }
    int depth()  const { return maxZ - minZ + 1; }
};
CubeSpan placedCubeSpan(int microW, int microD, int rotation, const glm::ivec3& backDir,
                        int extTMicro, int baseCubeX, int baseCubeZ);

/// The semantic identity of a placed fixture — what's needed to address it later in a session
/// ("rotate the bed", "move the 2nd bedroom's bed"). Carried into the PlacedObject's metadata.
struct FixtureLabel {
    std::string room;            ///< owning room id
    std::string purpose;         ///< that room's purpose (e.g. "bedchamber", "kitchen")
    int         purposeIndex = 0;///< 0-based ordinal among rooms of the SAME purpose (story order):
                                 ///< "2nd bedroom" == purposeIndex 1
    std::string type;            ///< fixture type (e.g. "bed")
};

/// A recipe piece the placer could NOT fit in its room — surfaced (not silently dropped) so the
/// caller can report it ("taproom: no room for bar_stool") instead of a building quietly missing
/// half its furniture.
struct UnplacedFixture {
    std::string room;
    std::string type;
};

class FurniturePlacer {
public:
    /// Furnish every room in `story`. `origin` = structure world origin (room rects
    /// are local to it); `floorY` = world Y of the walkable floor (pieces sit here).
    /// `footprints` (type -> real cube footprint, from the asset library) makes placement
    /// FOOTPRINT-AWARE: a piece reserves all the cells it covers; pieces PACK along walls (multiple
    /// per wall) at the first free slot. A piece that fits NOWHERE is recorded in `*unplaced` (if
    /// given) — never silently dropped. Omitted/missing footprints default to 1×1.
    /// `extTMicro` = exterior-wall thickness in micro (the wall-inset applied at placement). When > 0,
    /// reservation uses the TRUE placed cube span (placedCubeSpan) — the actual render extent including
    /// the micro-spill — instead of the bare footprint, so reservation == render (no overlaps). 0 keeps
    /// the legacy footprint reservation.
    /// `wealthTier` ("humble" | "middling" | "high", from the typology's room_program
    /// wealth_tier) filters tiered recipe pieces; "" = no filtering (every piece).
    /// `reservedRects` (KI-5d): footprint-local rects no furniture may cover — the
    /// STAIR rects touching this story (departing base AND the arriving well cut) plus
    /// a 1-cell landing margin. Furniture used to be placed straight onto stair cells.
    static std::vector<FurniturePlacement> furnish(const ProgStory& story,
                                                   const glm::ivec3& origin, int floorY,
                                                   const std::map<std::string, Footprint>& footprints = {},
                                                   std::vector<UnplacedFixture>* unplaced = nullptr,
                                                   int extTMicro = 0,
                                                   const std::string& wealthTier = "",
                                                   const std::vector<Rect>& reservedRects = {},
                                                   int intTMicro = 2);

    /// Claims Ledger increment 3: furnish FROM THE PLAN. Derives every geometric
    /// side-channel from the realized AssemblyPlan — exterior/interior wall thickness
    /// (micro) from the recorded WallSegments, this story's reserved stair rects from
    /// plan.stairs — then runs the same core placer. Consumers hand over the anatomy;
    /// they no longer re-derive numbers the realizer already knows. Placements are
    /// IDENTICAL to the legacy side-channel call for the same shell
    /// (FurnishPlanEquivalenceTest pins it field-by-field).
    static std::vector<FurniturePlacement> furnishFromPlan(
        const ProgStory& story, int storyIndex,
        const glm::ivec3& origin, int floorY,
        const AssemblyPlan& plan,
        const std::map<std::string, Footprint>& footprints = {},
        std::vector<UnplacedFixture>* unplaced = nullptr,
        const std::string& wealthTier = "");

    /// Plan-derivation helpers (exposed for equivalence tests + other consumers).
    /// Thickness comes from the FIRST wall segment of the given type — the realizer
    /// paints every segment of a type at one style thickness — through the same
    /// clamped converter the realizer built with (StructureRealizer::thicknessMicro).
    static int planExteriorThicknessMicro(const AssemblyPlan& plan);
    static int planInteriorThicknessMicro(const AssemblyPlan& plan);
    /// Stair well rects touching `storyIndex` (departing base OR arriving well).
    static std::vector<Rect> planStairRects(const AssemblyPlan& plan, int storyIndex);

    /// Scatter small CLUTTER (mugs, bottles) ON a surface (a table top / shelf). `surface` = the
    /// surface footprint in WORLD cells; `topY` = world Y of the surface top (items sit here, not on
    /// the floor); `items` = clutter types to place. Each item gets a DISTINCT cell inside the surface
    /// (no overlap), chosen deterministically from `seed`. If there are more items than cells, only as
    /// many as fit are placed (no overflow). This is the surface-placement path furnish() lacks.
    static std::vector<FurniturePlacement> placeSurfaceClutter(
        const std::string& room, const Rect& surface, int topY,
        const std::vector<std::string>& items, unsigned seed);

    /// Rotation so a piece backed against a wall faces INTO the room, given the
    /// INWARD normal (pointing from the wall toward the room centre).
    static int facingIntoRoom(int inwardDx, int inwardDz);

    // ---- MOUNTING (furniture quality B): sconces/racks hang on their wall at a grounded
    // height; a chandelier hangs from the ceiling. Everything else sits on the floor. ----
    enum class Mount { Floor, Wall, Ceiling };
    /// The mount kind for a fixture type (placer-owned data, like the recipes).
    static Mount mountFor(const std::string& type);
    /// A fixture's ABSOLUTE base micro-Y:
    ///   Floor   -> surfaceMicroY (unchanged).
    ///   Wall    -> surfaceMicroY + the type's grounded mount offset (wall_lantern: 14 micro
    ///              = the 60 in sconce mounting height; tool_rack: 9 micro ~ reach height).
    ///   Ceiling -> ceilingMicroY - templateMicroH - 1 (a 1-micro drop), never lower than
    ///              surfaceMicroY + 18 (head clearance: agent 16 micro + margin) — a low room
    ///              lifts the piece flush instead of braining the character.
    static int mountedMicroY(const std::string& type, int surfaceMicroY, int ceilingMicroY,
                             int templateMicroH);

    // ---- DATA RECIPES (furniture quality B): purpose -> pieces, tier-filtered. ----
    /// Load resources/furnishing_recipes.json (idempotent; safe to call per build). When a
    /// purpose has a data recipe it OVERRIDES the hardcoded map; unknown purposes fall back.
    static bool loadRecipesFromFile(const std::string& path);

    /// Per-purpose SURFACE ITEM set (furnishing_recipes.json "surface_items"):
    /// item ids scattered on table tops as pickable ITEM PROPS (static-first).
    /// Falls back to a tavern-ish default for purposes absent from the JSON.
    /// (docs/structure-generation/ItemPlacementPlan.md)
    static std::vector<std::string> surfaceItemsFor(const std::string& purpose);

    /// MEASURED top surface of a template in units — the max occupied Y over
    /// all voxel tiers. Replaces the floor+1-cube clutter guess (a tavern
    /// table is ~0.78 u tall; the guess floated items 0.22 u above the wood).
    static float templateTopUnits(const VoxelTemplate& tmpl);

    /// A pickable surface-item spot in continuous WORLD units (item BASE position).
    struct SurfaceItemSpot {
        std::string type;       ///< item id (items.json)
        glm::vec3 worldPos{0};  ///< base position: x/z ON the tabletop, y = measured top
        float yawDeg = 0.0f;
    };
    /// Deterministic item spots on the ACTUAL placed table/bar — not the plan cell.
    /// Rasterizes the template and finds EVERY upward-facing surface plane with
    /// bottle headroom (>= 3 micro or open sky) — so a shelving unit stocks ALL
    /// its shelves, not just the top board; slivers (a 1-micro rim, a leg's foot)
    /// are filtered by area/extent, micro-stepped tops merge into one plane, and
    /// at most the 4 highest planes are stocked. Each plane's rect gets the SAME
    /// 90°-step rotation convention as spawnTemplateMicro / computeMicroPlacedBounds
    /// (pivot = template micro-AABB mmax), is translated by the placed `worldMicro`
    /// (which INCLUDES the wall inset), rim-inset, and scattered with `items` on a
    /// seeded jittered min-spacing grid at the plane's measured Y + a 0.01 lift.
    /// Fixes items-on-the-table-edge / hovering-beside-the-table (the old path used
    /// the unrotated plan-time cube rect) and empty generated shelves.
    static std::vector<SurfaceItemSpot> placeSurfaceItems(
        const std::string& room, const VoxelTemplate& tableTmpl,
        const glm::ivec3& worldMicro, int rotationDeg,
        const std::vector<std::string>& items, unsigned seed);
    /// TESTING: drop any loaded data recipes (back to the hardcoded fallback).
    static void clearRecipes();

    /// `as:"item"` recipe realization: the item id a furnishing TYPE realizes as
    /// (e.g. "rug" -> the pickable rug prop), or "" when the type stays a baked
    /// template. Placement/reservation is identical either way — only the
    /// consumer's realization differs.
    static std::string itemFormFor(const std::string& type);

private:
    /// Loaded "surface_items" recipes (purpose -> item ids). See surfaceItemsFor.
    static std::map<std::string, std::vector<std::string>>& surfaceItemRecipes();
    /// Loaded `as:"item"` mappings (furnishing type -> item id). See itemFormFor.
    static std::map<std::string, std::string>& itemFormRegistry();
public:

    /// Convert a (cube-cell) placement to a MICRO-PRECISE world position (cube*9 + micro) so the piece
    /// sits flush against the wall's interior face and on the walkable surface — never inside the wall
    /// or floor. X/Z: cube*9 inset by `extTMicro` along -backDir (toward the room). Y: the absolute
    /// walkable-surface micro-Y (`surfaceMicroY`), NOT the truncated cube `worldPos.y`. This is the
    /// fix for furniture clipping thin sub-cube walls / sinking into the mid-cube floor. Testable.
    static glm::ivec3 microWorldPos(const FurniturePlacement& p, int extTMicro, int surfaceMicroY);

    /// M4: the furnishing PASS rank of a fixture type — 0 heavy (built-in/vented
    /// fixtures that shape the room), 1 light (movable furniture), 2 lighting,
    /// 3 clutter. Pieces are placed in pass order, so a hearth claims its wall
    /// before a stool can take it. Per-TYPE engine data (like mountFor); a recipe
    /// entry may override it with "pass".
    static int passRank(const std::string& type);

    /// M4: true for fixtures that BURN and therefore require a flue to the outside
    /// (fireplace / forge_hearth / oven_bread) — the set place_chimney (#14) must
    /// serve, and the ONE definition the forge's chimney pass shares.
    static bool isVentedFixture(const std::string& type);

    /// The furniture a room of this purpose REQUIRES, as fixture-type names (the recipe that
    /// furnish() places). Exposed so the asset-coverage validator can assert each required type
    /// resolves to a real template; uses the SAME purpose-matching as furnish().
    static std::vector<std::string> requiredFurniture(const std::string& purpose);

    /// Canonical purposes the recipe distinguishes. Iterate these to enumerate the FULL furniture
    /// vocabulary (every type the placer can ever emit) — the coverage gate's demand side.
    static std::vector<std::string> knownPurposes();

    /// Label each placement (as returned by furnish(story,...)) with its room's purpose and the
    /// room's ordinal among same-purpose rooms (story order) — the semantic identity used to address
    /// the fixture later. Returned in the SAME order as `placements`.
    static std::vector<FixtureLabel> labelFixtures(const ProgStory& story,
                                                   const std::vector<FurniturePlacement>& placements);

    /// The result of re-placing a single fixture within its room (a session edit). Cell coords are
    /// in the SAME space as the `room` rect passed in (pass a WORLD-space rect -> world cells).
    struct FurnitureEdit {
        bool        ok = false;
        int         x = 0, z = 0;   ///< new cell for the fixture
        int         rotation = 0;   ///< new facing — points INTO the room for wall ops
        std::string error;
    };

    /// Compute a wall-relative move for a fixture currently at (curX,curZ) in `room`, reusing the
    /// same wall-cell + facing-into-room math as furnish() so the moved piece stays usable (backed
    /// onto the wall, facing inward). `op`:
    ///   "wall:north"|"south"|"east"|"west"  -> seat on that wall, centered, facing in
    ///   "opposite_wall"                     -> seat on the wall opposite the one it's currently on
    ///   "center"                            -> room centre
    ///   "rotate"                            -> keep the cell, set rotation = rotationArg
    /// Returns ok=false + error on an unknown op or a degenerate room.
    static FurnitureEdit planEdit(const Rect& room, int curX, int curZ,
                                  const std::string& op, int rotationArg = 0);
};

} // namespace Core
} // namespace Phyxel
