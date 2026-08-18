#pragma once

// ============================================================================
// SettlementLayout — the settlement tier (the leap from one building to a town).
// subdivide_plots (#40): partition a settlement footprint into non-overlapping
// PLOTS separated by STREET corridors, each plot >= a min building size. The
// settlement analog of generateRoomLayout: deterministic, tiling-with-gaps.
//
// L2 invariants (gated by SettlementLayoutTest): plots don't overlap, all fit the
// footprint, each >= minPlot, and adjacent plots are separated by a >= streetWidth
// GEOMETRIC GAP. (L2 = the gap is wide enough on paper; actually WALKING it with a
// TraversalProbe is the deferred L3 slice — not proven here.) The emitted street
// bands are the corridors that L3 slice will probe.
// ============================================================================

#include <map>
#include <string>
#include <vector>

#include "core/BuildingProgram.h"     // Rect
#include "core/RoomProgram.h"         // RoomProgramRegistry (typology-sized frontage)
#include "core/SettlementProgram.h"   // SettlementTierPreset (era/tier data presets)
#include "core/SiteAnalysis.h"        // BuildabilityMap (terrain-aware placement, Phase 2)

namespace Phyxel {
namespace Core {

struct Plot {
    Rect rect;     ///< the buildable footprint of this plot (cubes), local to the settlement origin
    int  row = 0;  ///< grid row/col (for addressing / frontage)
    int  col = 0;
};

struct SettlementLayout {
    std::vector<Plot> plots;    ///< non-overlapping buildable plots
    std::vector<Rect> streets;  ///< street corridors between/around the plots (>= streetWidth wide)
};

/// Partition a W×D (cubes) settlement footprint into a cols×rows grid of plots separated by
/// `streetWidth`-wide street corridors. Each plot is >= minPlot per side. Returns an EMPTY layout
/// (caller reduces density) if the grid can't fit every plot at >= minPlot. Deterministic.
SettlementLayout subdividePlots(int W, int D, int cols, int rows, int streetWidth, int minPlot = 6);

/// Which side of `plot` fronts a street corridor: 'S' (-z) | 'N' (+z) | 'W' (-x) | 'E' (+x),
/// or 0 if no side touches a street (e.g. terrain-mode layouts have no street rects). Prefers
/// the plot's LONG sides — the building inside orients along the plot's long axis, and its
/// entrance wall (the cross-passage long elevation) faces perpendicular to it. Deterministic.
char streetSideForPlot(const SettlementLayout& layout, const Rect& plot);

/// One building sited on a plot (populate_plots #45).
struct PlacedBuilding {
    int         plotIndex = 0;
    Rect        footprint;   ///< building footprint (settlement-local), inset within the plot by the yard
    std::string typology;    ///< RoomProgram typology to build (passthrough to the building generator)
};

/// TERRAIN-AWARE plot placement (Phase 2): choose up to `maxPlots` non-overlapping plotSize×plotSize
/// plots whose ENTIRE footprint is buildable (no TooSteep/Water cell in `site`), separated by >=
/// `spacing` cells (the street/yard gap), preferring the FLATTEST spots (lowest total relief). On
/// mostly-unbuildable terrain (a steep mountain) it returns few/none — graceful degradation, not a
/// broken result. Plot rects are in site-cell coords. Deterministic (flattest-first, stable tiebreak).
std::vector<Plot> selectBuildablePlots(const BuildabilityMap& site, int plotSize, int spacing,
                                       int maxPlots);

/// Site one building per plot: footprint = the plot inset by `setback` on every side. A positive
/// setback leaves a yard (room for a path/garden) between the building and the street; setback=0 is
/// allowed and puts the building FLUSH to the plot edge (an urban row-house — NO yard, but still clear
/// of the street, since the plot itself starts a streetWidth in). Plots too small for the building
/// (footprint < minBuilding) are SKIPPED. Buildings inherit `typology`. Each footprint is contained in
/// its plot and plots don't overlap, so the buildings don't overlap each other or sit in a street.
std::vector<PlacedBuilding> populatePlots(const SettlementLayout& layout, int setback,
                                          int minBuilding, const std::string& typology = "hall_house");

/// One building's chosen VARIATION (so a village isn't N identical boxes). Each dimension is picked
/// deterministically + independently from the plot index, so neighbours differ in typology AND style
/// AND footprint shape, reproducibly.
struct BuildingVariant {
    std::string typology;        ///< from the typologies palette (room layout)
    std::string style;           ///< from the styles palette (wall/roof material + roof form)
    std::string footprintShape;  ///< "rect" | "L" (non-rectangular winged plan)
};

/// Pick a deterministic, varied building for `plotIndex` from the typology + style palettes (cycled via
/// independent hashes so the three dimensions vary independently). ~1/3 of buildings get an L-plan
/// footprint. Empty palettes fall back to sane defaults. Deterministic in (plotIndex, seed).
BuildingVariant pickBuildingVariant(int plotIndex, const std::vector<std::string>& typologies,
                                    const std::vector<std::string>& styles, unsigned seed);

/// A parcel fence: which perimeter CELLS get a fence post vs the gate opening. The fence runs the whole
/// parcel boundary so a character can't slip through anywhere EXCEPT the gate (a >= gateWidth gap on one
/// side). Posts ∪ gate == the full perimeter; posts ∩ gate == empty.
struct FencePlan {
    std::vector<std::pair<int, int>> posts;  ///< perimeter cells to fence (cubes, parcel-local or world)
    std::vector<std::pair<int, int>> gate;   ///< the gate opening cells (the only passable boundary run)
    bool ok = false;
};

/// Enclose `parcel` (a plot rect, cubes) with a fence on its whole perimeter, leaving a gate of
/// `gateWidth` cubes centred on `gateSide` ('N'=+z, 'S'=-z, 'E'=+x, 'W'=-x). The yard is the parcel
/// minus the building inside it (the fence doesn't touch the building). Returns ok=false if the parcel
/// is too small for a gate. Deterministic. (place_fence #22 / zone_parcel #21.)
FencePlan planParcelFence(const Rect& parcel, char gateSide, int gateWidth);

// ============================================================================
// Main-street morphology (lay_street_network #39 / site_settlement #38 partial)
// — the row-village / burgage form: ONE street spine, plots allocated frontage-
// by-frontage on BOTH sides, each plot's typology assigned FIRST (weighted
// deterministic draw) and the plot sized FROM that typology's grounded width
// (the burgage principle). Era/tier variation is DATA (SettlementTierPreset).
// ============================================================================

/// One plot with its pre-assigned, pre-sized building.
struct AssignedPlot {
    Plot        plot;        ///< the toft rect (settlement-local); abuts the street on `streetSide`
    std::string typology;    ///< pre-assigned via the weighted draw (drives frontage)
    char        streetSide;  ///< which side of THIS plot fronts the main street ('N'|'S'|'E'|'W')
    int         setback;     ///< drawn yard depth (cubes): front yard AND side margins for this plot
    Rect        footprint;   ///< building footprint: typology NATURAL size (never stretched),
                             ///< front wall `setback` in from the street edge, centred on the frontage
};

struct MainStreetLayout {
    SettlementLayout base;   ///< plots + street rects — legacy consumers (terrace/fence/path) iterate these
    Rect mainStreet;         ///< the spine (distinguished for paving + the L3 end-to-end walk)
    Rect marketSquare;       ///< the market place (town tier) — a WIDENED-main-street square at
                             ///< mid-length (the common English market form); paved as a plaza,
                             ///< building-free, the tier well's anchor. Valid iff hasSquare.
    bool hasSquare = false;
    std::vector<AssignedPlot> assigned;
    bool ok = false;         ///< false = the footprint can't host a main street at this tier
};

/// Weighted deterministic typology draw for plot `plotIndex` (the settlement analog of
/// pickBuildingVariant's hash). Empty/zero weights fall back to "hall_house". Deterministic in
/// (plotIndex, seed, salt); `salt` lets a caller redraw when a pick doesn't fit its plot.
std::string drawTypology(const std::map<std::string, int>& weights, int plotIndex, unsigned seed,
                         unsigned salt = 0);

/// Lay a main-street settlement into a W×D (cubes) footprint per the tier preset: a street spine
/// along the LONG axis (or `axis` = 'X'|'Z' if forced; 0 = auto), plots on both sides sized from
/// their assigned typology (frontage = building frontage + 2*setback; orientation per the typology's
/// entrance rule: "long_wall" dwellings present their LONG wall to the street, gable/shop typologies
/// their GABLE — the burgage read). `crossOffset` (-1 = centred) positions the spine on the cross
/// axis (terrain mode passes the flattest alignment). Deterministic in (tier, W, D, seed).
MainStreetLayout planMainStreetLayout(const SettlementTierPreset& tier, int W, int D,
                                      const RoomProgramRegistry& rooms, unsigned seed,
                                      char axis = 0, int crossOffset = -1);

/// SEMI-ORGANIC CITY (city tier, site_settlement #38 growth axes + zone_districts #41): the
/// "slightly organized, a little chaotic" quarter. Crossroads AXES — the main street and a cross
/// street meeting at the market square — plus SECONDARY cross streets at seeded-JITTERED block
/// intervals (the bounded chaos: axis-aligned rects, never free angles). Burgage rows front the
/// main street in every block AND the central cross street beyond the main rows' band. Districts
/// are DATA: inside `coreRing` of the square the typology draw uses `coreTypologyWeights` (trades
/// cluster on the market); the fringe uses the base weights and +1 setback (looser edges).
/// Returns the same MainStreetLayout shape (mainStreet = the main axis, marketSquare set), so the
/// whole downstream pipeline (paver, fences, props, well) is shared. Deterministic in (tier,W,D,seed).
MainStreetLayout planCityLayout(const SettlementTierPreset& tier, int W, int D,
                                const RoomProgramRegistry& rooms, unsigned seed);

/// One yard prop sited on a parcel (place_yard_props #29 / place_garden #25 minimum slice).
struct YardProp {
    std::string type;    ///< FurnitureCatalog type ("woodpile" | "garden_bed")
    int cx = 0, cz = 0;  ///< min-corner cube position, settlement-local
    int w = 1, d = 1;    ///< cube footprint (long axis along the rotated width)
    int rotDeg = 0;      ///< 0 = long axis along X | 90 = along Z
};

/// Furnish an assigned plot's REAR TOFT (the yard behind the building, opposite the street):
/// a woodpile near the building's rear wall + a kitchen-garden bed in the open toft.
/// Invariants (YardPropsTest): every prop inside the plot INSET 1 cube (clear of the fence
/// line), outside the building footprint, on the rear side (farther from the street than the
/// building's rear wall), non-overlapping. A rear toft too small gets fewer/no props (honest
/// degradation). Deterministic in (plot, seed).
std::vector<YardProp> planYardProps(const AssignedPlot& ap, unsigned seed);

/// The flattest straight spine alignment for a main street over `site`: evaluates every axis-aligned
/// band of `mainWidth` cells (both axes, every cross offset) by PER-CELL relief + unbuildable-cell
/// penalty (per-cell, so the short axis gets no cell-count advantage), returns the minimum (stable
/// tiebreak: the LONGER axis first, then lower offset). `minPlotDepth` restricts the offset search so
/// at least that much plot room remains on BOTH sides of the band (clamped for small sites — never
/// an empty search). Deterministic.
struct StreetAxisChoice {
    char axis = 'X';     ///< spine runs along X ('X') or Z ('Z')
    int  crossOffset = 0;///< band start on the cross axis (site-cell coords)
    long score = 0;      ///< per-cell relief x1000 + penalties (lower = flatter)
};
/// `preferredAxis` ('X'/'Z', 0 = none): a bounded score bias toward the axis an arriving
/// inter-settlement road runs along (WorldForge passes it), so the main street MEETS the
/// road on comparable terrain — row villages grow along their through-road (the
/// settlement_program morphology sources). Terrain still wins: the bias never overrides
/// water/cliff penalties or a decisively flatter axis.
/// `preferredOffset` (band-start cell on the cross axis, -1 = none): the same bounded idea
/// laterally — bias the band toward where the road actually arrives, so on comparable
/// terrain street and road meet head-on instead of offset by half the site.
StreetAxisChoice chooseStreetAxis(const BuildabilityMap& site, int mainWidth, int minPlotDepth = 0,
                                  char preferredAxis = 0, int preferredOffset = -1);

} // namespace Core
} // namespace Phyxel
