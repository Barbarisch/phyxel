#pragma once

// ============================================================================
// HearthForge — the VENTED BUILT-IN: a hearth body and the chimney stack that
// carries its smoke out through every floor and the roof (placer #14,
// docs/structure-generation/placers/14_place_chimney.md; the plan for this
// change is docs/structure-generation/ChimneyForgePlan.md).
//
// WHY IT IS A FORGE, NOT FURNITURE. A hearth is structurally closer to a wall
// than to a stool: it is masonry, it is built with the shell, and it punches a
// flue through everything above it. Before this forge the stack was stamped
// into a FINISHED building as loose micro voxels, so it displaced ~600 cells
// of already-built structure per tavern. Here the body + stack are painted
// into the same MicroCanvas as the shell, so:
//   * the floor slabs and roof deck YIELD to the flue by construction (the
//     flue is canvas AIR carved after the masonry) — nothing is displaced;
//   * body and stack export through the same greedy coarsening as the shell;
//   * the canvas detectors (WallClosure / FloorIntegrity / ChimneyIntegrity)
//     can finally SEE the chimney, which they never could post-export.
//
// Coordinates are STRUCTURE-LOCAL MICRO (9 micro = 1 cube), matching
// StructureRealizer's canvas. Bodies are authored unrotated with the firebox
// opening toward +Z and the chimney breast at z = 0 (the wall side), then
// rotated about the same pivot PlacedObjectManager uses, so a sited hearth
// lands exactly where the furnish pass reserves it.
//
// GROUNDING (see the placer doc for citations):
//   * ridge clearance   6 micro (0.667 m) >= 2 ft, IRC R1003.9 "3-2-10 rule".
//   * flue void         3x3 micro = 0.111 m^2 >= the 0.044 m^2 minimum
//                       (1/10 of a 0.44 m^2 firebox opening, IRC R1003.15.1).
//   * stack             5x5 micro (0.56 m) square = flue + 1 micro of masonry.
//                       The 1-micro (0.11 m) wall is THIN for real masonry
//                       (>= 215 mm brick / 300 mm rubble) — flagged in
//                       GroundingGaps.md, unchanged by this forge.
//   * body dimensions   object_dimensions.json ('hearth' 1.5x1.2x0.6,
//                       'forge_hearth' 1.0 wide / work_top 0.8, 'oven_bread'
//                       1.6x1.4 / interior 0.9) — the same canon the furniture
//                       templates were generated against.
// ============================================================================

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/AssemblyPlan.h"      // HearthRecord
#include "core/BuildingProgram.h"   // ProgStory / ProgFixture / Rect
#include "core/FurniturePlacer.h"   // FurniturePlacement / Footprint (siting)
#include "core/MicroCanvas.h"
#include "core/StyleProfile.h"

namespace Phyxel {
namespace Core {

class HearthForge {
public:
    /// How far ABOVE the roof apex the stack must finish, in MICRO cells.
    /// 6 micro = 0.667 m > the 2 ft (0.610 m) IRC R1003.9 floor; 5 would FAIL it.
    static constexpr int kRidgeClearanceMicro = 6;
    /// Stack half-extent (5x5 masonry) and flue half-extent (3x3 void), in micro.
    static constexpr int kStackHalfMicro = 2;
    static constexpr int kFlueHalfMicro  = 1;
    /// Solid courses at the top of the stack (the pot — hides the flue from above).
    static constexpr int kCapRows = 1;
    /// A split billet is ~0.11 m across (1 micro) — see tools/gen_items.py
    /// `_billet`, grounded to stove-length firewood. The pile lays them out on
    /// this pitch: 2 micro leaves an air gap between billets, and a fire needs
    /// air between the wood.
    static constexpr int kBilletPitchMicro = 2;
    /// Hard cap on billets per hearth. Above this the pile stops reading as a
    /// fire and starts reading as a woodpile, and each billet is a prop + a
    /// draw; the ONE lit billet is what carries the flame, so more add nothing.
    static constexpr int kMaxFuelBillets = 4;

    /// Fixtures that BURN and therefore need a flue. ONE definition, shared with
    /// FurniturePlacer::isVentedFixture (which delegates here).
    static bool isVented(const std::string& type);

    /// A vented built-in's grounded body, authored unrotated: firebox opening
    /// toward +Z, chimney breast at z = 0. Extents are MICRO cells.
    struct Body {
        bool known = false;
        int w = 0, h = 0, d = 0;        ///< micro extents (h = mantel height)
        int flueCx = 0, flueCz = 0;     ///< flue axis (local micro, unrotated)
        int fireX = 0, fireY = 0, fireZ = 0;   ///< flame anchor (local micro, unrotated)
        std::string material;           ///< default masonry for the type
        // FUEL: what burns in here, and how much room it has. A hearth that
        // burns CORDWOOD names an item and gets a pile of billet props; one
        // that burns something else (the forge's charcoal, the oven's swept
        // embers) leaves `fuelItem` empty and keeps its painted glow bed until
        // its own grounded fuel lands.
        std::string fuelItem;           ///< "" = not cordwood
        int fuelSpanMicro = 0;          ///< firebox clear width the pile spreads across
        int fuelDepthMicro = 0;         ///< firebox clear DEPTH — how many billets lie
                                        ///< front-to-back, so it sets the pile size. Stated
                                        ///< by the preset rather than inferred from `d`:
                                        ///< inferring it got the fireplace wrong by one
                                        ///< billet (the void is z1..d-1, not z1..d-2) and
                                        ///< shipped a one-log "pile".
        int fuelFloorY = 0;             ///< firebox floor (local micro) the pile rests on
    };
    static Body bodyOf(const std::string& type);

    /// The body's footprint in CUBES — what the furnish pass reserves for it.
    /// Derived from the body itself (the forge owns the geometry), NOT from a
    /// template sidecar: nothing spawns a hearth template any more.
    static Footprint footprintOf(const std::string& type);

    /// Site every VENTED built-in this story's rooms call for, by running the SAME
    /// placement algorithm the furnish pass will run later (same recipes, same pass
    /// order, same reservations) and keeping only the vented pieces. Running it here
    /// — before realize — is the whole point: the shell cannot paint a flue through
    /// its floors until it knows where the hearths are. `origin`-relative cells are
    /// footprint-LOCAL (origin is implicitly 0).
    /// `footprints` MUST be the same map the furnish pass will use, or the heavy pass
    /// packs differently and the hearth moves between siting and furnishing (the
    /// vented entries are overridden with footprintOf — the forge owns those).
    static std::vector<FurniturePlacement> siteHearths(
        const ProgStory& story, const std::map<std::string, Footprint>& footprints,
        int extTMicro, int intTMicro, const std::vector<Rect>& reservedRects,
        const std::string& wealthTier);

    /// Turn sited placements into PROGRAM fixtures on the story (idempotent: any
    /// existing vented fixtures are replaced). Returns how many were sited.
    static int siteIntoProgram(ProgStory& story, const std::map<std::string, Footprint>& footprints,
                               int extTMicro, int intTMicro,
                               const std::vector<Rect>& reservedRects,
                               const std::string& wealthTier);

    /// Stair wells touching `storyIndex`, derived from the PROGRAM — the same rects
    /// FurniturePlacer::planStairRects reads off the realized plan, available before
    /// realize (which is when hearths must be sited). Mirrors the realizer's filters
    /// (adjacent stories, non-degenerate well, de-duplicated).
    static std::vector<Rect> stairRectsForStory(const BuildingProgram& program, int storyIndex);

    /// The resolved POSE of a sited hearth, in structure-local coords: the placed
    /// anchor (micro, wall-inset applied — the same one FurniturePlacer::microWorldPos
    /// computes), the rotated flue axis, and the cube column the stack will occupy.
    /// One definition, shared by the painter and by the reservation the furnish pass
    /// needs on the stories ABOVE (nothing may be placed inside a chimney breast).
    struct Pose {
        int anchorX = 0, anchorZ = 0;   ///< local micro
        int flueCx = 0, flueCz = 0;     ///< local micro (rotated flue axis)
        Rect stackCubes;                ///< the stack's cube column
        Rect bodyCubes;                 ///< the body's true placed cube span
    };
    static Pose poseOf(const ProgFixture& fx, const Rect& room, const Rect& footprint,
                       int extTMicro, int intTMicro);

    /// PAINT the hearth body into the canvas and return its record (base, mantel,
    /// flue rect, flame anchor — everything the stack pass and the lighting pass
    /// need). `walkMicroY` = this story's walkable surface; `room`/`footprint` give
    /// the wall the piece backs onto, so the body sits flush against the wall's
    /// interior face exactly like the furnish pass would have placed it.
    static HearthRecord paintBody(MicroCanvas& c, const ProgFixture& fx, const Rect& room,
                                  const Rect& footprint, int story, int walkMicroY,
                                  int extTMicro, int intTMicro, const StyleProfile& style);

    /// PAINT the stack from the mantel up to `topMicroY` (inclusive) and carve the
    /// flue as canvas AIR — through whatever the stack crosses, which is how the
    /// floor slabs and the roof deck yield without a single displaced voxel.
    /// Sets `rec.stackTopMicroY`.
    static void paintStack(MicroCanvas& c, HearthRecord& rec, int topMicroY);

    /// The stack's cube column (footprint-local) — reserved so furniture on the
    /// stories ABOVE the hearth is never placed inside the chimney breast.
    static Rect stackCubeRect(const HearthRecord& rec);

    /// One billet of the fuel pile, in structure-local MICRO (fractional: a
    /// billet is centred between cells, not snapped to one).
    struct FuelBillet {
        float x = 0, y = 0, z = 0;
        int   rotationDeg = 0;    ///< laid ACROSS the opening
        bool  lit = false;        ///< exactly one — it carries flame + firelight
    };

    /// Lay out the fuel pile recorded on `rec`: a base row of billets across the
    /// firebox floor with air between them, and a crossing log on top once there
    /// is a bed to rest on. Pure and deterministic — the furnish pass spawns
    /// exactly these poses and the tests measure exactly these poses, so there
    /// is no second layout algorithm to drift from the first. Empty when the
    /// hearth does not burn cordwood.
    static std::vector<FuelBillet> fuelBillets(const HearthRecord& rec);
};

}  // namespace Core
}  // namespace Phyxel
