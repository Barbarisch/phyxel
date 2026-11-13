#include "core/SettlementBuildService.h"
#include "core/SettlementMapPlanner.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <sstream>
#include <functional>
#include "utils/CoordinateUtils.h"
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/ChunkManager.h"
#include "core/DamageSystem.h"
#include "core/DimensionCanon.h"
#include "core/FenceBuilder.h"
#include "core/SettlementWalkability.h"
#include "core/TraversalProbe.h"
#include "core/FloraSweep.h"
#include "core/TowerForge.h"
#include "core/FurnitureCatalog.h"
#include "core/LocationRegistry.h"
#include "core/NPCManager.h"
#include "core/ObjectTemplateManager.h"
#include "core/PathPlanner.h"
#include "core/PlacedObjectManager.h"
#include "core/ResidentPlanner.h"
#include "core/RoomProgram.h"
#include "core/RoomLayout.h"
#include "core/SettlementLayout.h"
#include "core/SettlementProgram.h"
#include "core/SiteAnalysis.h"
#include "core/StreetPaver.h"
#include "core/StructureBuildService.h"
#include "core/StructureGenerator.h"
#include "scene/behaviors/ScheduledBehavior.h"
#include "utils/Logger.h"

namespace Phyxel {
namespace Core {

int settlementTopScan(ChunkManager* cm, int oy, int wx, int wz, bool floraBlind) {
    if (!cm) return oy;
    const int top = oy + 96;
    for (int wy = top; wy >= 0 && wy >= top - 200; --wy)
        if (cm->hasVoxelAt(glm::ivec3(wx, wy, wz))) {
            if (floraBlind) {
                // ELEVATION means natural terrain, and terrain is always full cubes.
                // A present-but-cubeless cell is sub/micro content (tree branches,
                // fences, paving) - never ground.
                const auto* c = cm->getCubeAt(glm::ivec3(wx, wy, wz));
                if (!c) continue;
                const std::string& m = c->getMaterialName();
                if (m.rfind("Log", 0) == 0 || m.rfind("Leaf", 0) == 0) continue;
            }
            return wy;
        }
    return oy;
}

// M0 row 2b: "the agent seats at cell (cx, cz) near micro height `hint`" - the walkability
// gate's seatFeet, shared with the door-spur unit and the cluster path network so all three
// find the SAME door (SettlementBuildService::doorApproach). Lowest feet level at/above the
// column's top (scanned from hint+8 down 40 micro) where the whole box fits and is supported.
static bool settlementSeatsAt(ChunkManager* cm, const Core::AgentBox& box, int cx, int cz,
                              int hintMicroY) {
    if (!cm) return false;
    const std::function<bool(int, int, int)> occ = [cm](int x, int y, int z) {
        return cm->occupiedMicro(glm::ivec3(x, y, z));
    };
    const int mx = cx * 9 + 4, mz = cz * 9 + 4;
    int top = INT_MIN;
    for (int my = hintMicroY + 8; my > hintMicroY - 31; --my)
        if (occ(mx, my, mz)) { top = my; break; }
    if (top == INT_MIN) return false;
    Core::TraversalProbe probe(occ, box);
    for (int y = top + 1; y <= top + 10; ++y)
        if (probe.fits(mx, y, mz) && probe.supported(mx, y, mz)) return true;
    return false;
}

static SettlementBuildService::DoorApproach settlementDoorApproach(
    ChunkManager* cm, const Core::AgentBox& box, const glm::ivec3& marker, const glm::ivec4& foot,
    int thresholdMicro) {
    std::function<bool(int, int)> seats;
    if (cm && thresholdMicro >= 0)
        seats = [cm, box, thresholdMicro](int cx, int cz) {
            return settlementSeatsAt(cm, box, cx, cz, thresholdMicro);
        };
    return SettlementBuildService::doorApproach(marker, foot, seats);
}

// ── M0 (docs/WorldForgeV2Plan.md §5 M0, "site prep reordered") ──────────────────────
// The paving APPLIER, lifted verbatim out of the street unit so the door-spur unit (which
// runs AFTER the buildings, from the realized doors) stamps exactly like the streets:
// cut cubes removed, corridor band-cleared resolution-complete, [startRow..surface] micros
// placed in one bulk call, grass under paving converted to Dirt, the stamp recorded for the
// late sweep. Appends to pavedCols / roadBand / stamp.
struct PavingApply {
    long placedMicros = 0, grassConverted = 0, cutCubes = 0;
    int  bandCleared = 0;
    double stampMs = 0.0;
};
template <class GroundMicro, class GroundTop, class Fl9, class Emit>
static PavingApply applyPavingPlan(ChunkManager* chunkManager, const Core::PavingPlan& pave,
                                   const GroundMicro& groundMicro, const GroundTop& groundTopAt,
                                   const Fl9& fl9, const Emit& emitMicro,
                                   std::set<std::pair<int, int>>& pavedCols,
                                   std::vector<glm::ivec3>& roadBand, std::vector<glm::ivec4>& stamp,
                                   const std::string& paveMat) {
    const auto t0 = std::chrono::steady_clock::now();
    // Pass A (read): stamp-start row per column against the ORIGINAL terrain.
    std::vector<int> startRow(pave.columns.size());
    for (size_t i = 0; i < pave.columns.size(); ++i) {
        const auto& c = pave.columns[i];
        startRow[i] = c.cut ? (c.surface / 9) * 9 : groundMicro(c.x, c.z);
    }
    // Pass B: remove the terrain cubes whose top face exceeds a cut column's surface,
    // AND clear the road corridor RESOLUTION-COMPLETE. The old fell was cube-only
    // (getCubeAt + Log*/Leaf*) but forge flora is multi-res: it left sub/micro
    // branch litter hovering over the road — collision-solid yet cube-scan
    // invisible (measured: a road pinch at x98-101 that NPCs treadmilled on
    // across three sessions). clearCellsBulk wipes every resolution in the full
    // headroom band above the road surface; only terrain + flora exist at
    // street time, so no material filter is needed.
    std::set<std::tuple<int, int, int>> cutCubes;
    std::set<std::pair<int, int>> clearedCols;
    std::map<Chunk*, std::vector<glm::ivec3>> bandByChunk;
    for (const auto& c : pave.columns) {
        const int cbx = fl9(c.x), cbz = fl9(c.z);
        if (c.cut) {
            const int top = groundTopAt(cbx, cbz);
            for (int y = c.surface / 9; y <= top; ++y) cutCubes.insert({cbx, y, cbz});
        }
        if (clearedCols.insert({cbx, cbz}).second) {
            const int y0 = c.surface / 9;
            const int y1 = std::max(groundTopAt(cbx, cbz), y0 + 7);
            for (int y = y0; y <= y1; ++y) {
                const glm::ivec3 wp(cbx, y, cbz);
                if (Chunk* ch = chunkManager->getChunkAtFast(wp))
                    bandByChunk[ch].push_back(wp - ch->getWorldOrigin());
            }
            // Walkable headroom band (3 cells over the paving) for the late
            // street sweep — NOT taller: an eave can legitimately overhang a
            // door spur at y0+3 and must survive the sweep.
            for (int y = y0; y <= y0 + 2; ++y)
                roadBand.push_back(glm::ivec3(cbx, y, cbz));
        }
    }
    int bandCleared = 0;
    for (auto& [ch, cells] : bandByChunk) {
        bandCleared += ch->clearCellsBulk(cells);
        chunkManager->markChunkDirty(ch);
    }
    if (!cutCubes.empty()) {
        std::vector<glm::ivec3> cuts;
        cuts.reserve(cutCubes.size());
        for (const auto& t : cutCubes)
            cuts.push_back({std::get<0>(t), std::get<1>(t), std::get<2>(t)});
        Core::StructureGenerator::removeVoxels(chunkManager, cuts);
    }
    // Pass C (read, post-removal, pre-stamp): a removed cube may sit under a NEIGHBOUR
    // column's paving (same cube column, different micro surface) — extend that column's
    // stamp down to the new ground so no paving edge floats over a void.
    for (size_t i = 0; i < pave.columns.size(); ++i)
        startRow[i] = std::min(startRow[i], groundMicro(pave.columns[i].x, pave.columns[i].z));
    // Pass D (write): BATCH every column [startRow .. surface] and place in one bulk call.
    Core::StructureResult paveBatch;
    stamp.reserve(pave.columns.size());
    for (size_t i = 0; i < pave.columns.size(); ++i) {
        const auto& c = pave.columns[i];
        pavedCols.insert({fl9(c.x), fl9(c.z)});
        stamp.push_back(glm::ivec4(c.x, startRow[i], c.z, c.surface));
        for (int my = startRow[i]; my <= c.surface; ++my)
            emitMicro(paveBatch, c.x, my, c.z, paveMat);
    }
    const long placedMicros =
        Core::StructureGenerator::place(chunkManager, paveBatch).placed;
    // Pass E (KI-5e): a road KILLS the grass under it. Thin paving micros don't
    // suppress the ground cube's exposed-top blade layer, so grass sprouted
    // through the gravel. Convert the ground cube under every paved column to
    // Dirt — the same rule building pads already apply (V10 grass_under_house).
    long grassConverted = 0;
    {
        auto isGrassMat = [](const std::string& m) {
            return m == "Grass" || m == "GrassForest" || m == "GrassSavanna" ||
                   m == "SnowGrass";
        };
        std::vector<glm::ivec3> gcut;
        Core::StructureResult gfill;
        for (const auto& [cbx, cbz] : pavedCols) {
            const int gy = groundMicro(cbx * 9 + 4, cbz * 9 + 4) / 9 - 1;
            const glm::ivec3 gp(cbx, gy, cbz);
            if (const auto* cu = chunkManager->getCubeAt(gp))
                if (isGrassMat(cu->getMaterialName())) {
                    gcut.push_back(gp);
                    Core::VoxelPlacement vp;
                    vp.position = gp;
                    vp.material = "Dirt";
                    vp.level = Core::VoxelLevel::Cube;
                    gfill.voxels.push_back(vp);
                }
        }
        if (!gcut.empty()) {
            Core::StructureGenerator::removeVoxels(chunkManager, gcut);
            Core::StructureGenerator::place(chunkManager, gfill);
        }
        grassConverted = static_cast<long>(gcut.size());
    }
    const double stampMs = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - t0).count();
    PavingApply r;
    r.placedMicros = placedMicros;
    r.grassConverted = grassConverted;
    r.cutCubes = static_cast<long>(cutCubes.size());
    r.bandCleared = bandCleared;
    r.stampMs = stampMs;
    return r;
}

SettlementBuildService::Plan SettlementBuildService::plan(const nlohmann::json& params,
                                                          const Deps& deps) {
    Plan res;
    res.paths       = std::make_shared<nlohmann::json>(nlohmann::json::object());
    res.yardProps   = std::make_shared<nlohmann::json>(nlohmann::json::object());
    res.lotFailures = std::make_shared<nlohmann::json>(nlohmann::json::array());
    res.residents = std::make_shared<nlohmann::json>(nlohmann::json::object());
    res.walkability = std::make_shared<nlohmann::json>(nlohmann::json::object());

    // Bound ONCE and captured BY VALUE into the work units, which outlive this frame.
    // Deliberately named to match the fields they replace so the relocated body reads
    // identically to the handler it came from (the move stays reviewable as a move).
    ChunkManager* const chunkManager                 = deps.chunkManager;
    PlacedObjectManager* const placedObjectManager   = deps.placedObjects;
    ObjectTemplateManager* const objectTemplateManager = deps.templates;
    LocationRegistry* const locationRegistry         = deps.locations;
    NPCManager* const npcManager                     = deps.npcs;
    const auto pushUndo                              = deps.pushUndo;
    const auto addPointLight                         = deps.addPointLight;   // M5
    const auto keepClear                             = deps.keepClear;       // designer anchors
    ItemPropManager* const itemPropManager           = deps.itemProps;       // M3c signs/tableware

        const auto& p = params;
        const int W = p.value("width", 52), D = p.value("depth", 36);
        const int cols = p.value("cols", 2), rows = p.value("rows", 2);
        const int streetWidth = p.value("street_width", 4);
        const int setback = p.value("setback", 2);
        const int minBuilding = p.value("min_building", 8);
        const std::string typology = p.value("typology", std::string("hall_house"));
        const std::string style = p.value("style", std::string("timber_cottage"));
        const bool seatFlat = p.value("seat_flat", false);  // debug: force base-Y seating (red baseline)
        // Optional mixed typologies: cycle a list per plot (deterministic) so a settlement is a varied
        // village, not N identical houses. Empty -> the single `typology` everywhere.
        std::vector<std::string> mix;
        if (p.contains("typologies") && p["typologies"].is_array())
            for (const auto& t : p["typologies"]) if (t.is_string()) mix.push_back(t.get<std::string>());
        // Default to a VARIED typology palette (like styles below) so a settlement varies by default;
        // a caller wanting one typology passes typologies:["croft"]. (typology param seeds it if given.)
        if (mix.empty()) mix = (typology == "hall_house") ? std::vector<std::string>{"croft", "longhouse", "hall_house"}
                                                          : std::vector<std::string>{typology};
        // Style palette for per-building variation (material + roof form). Default = the three shipped
        // styles so a village mixes timber/stone, thatch/wood/stone roofs, gable/flat.
        std::vector<std::string> styles;
        if (p.contains("styles") && p["styles"].is_array())
            for (const auto& s : p["styles"]) if (s.is_string()) styles.push_back(s.get<std::string>());
        if (styles.empty()) styles = {style, "stone_manor", "stone_keep"};
        const unsigned varietySeed = static_cast<unsigned>(p.value("variety_seed", 1));
        int ox = 0, oy = 16, oz = 0;
        if (p.contains("position")) {
            ox = p["position"].value("x", 0); oy = p["position"].value("y", 16); oz = p["position"].value("z", 0);
        }

        // GROUNDING GATE (settlement-level, fail fast): the whole site must have terrain —
        // streets/terraces/fences stamp across the full rect, so a partially generated
        // world silently produced floating gravel ribbons + buildings in the void.
        // Refuse by default; {"allow_ungrounded": true} overrides (and is forwarded to
        // the per-building builds so they don't re-refuse).
        const bool allowUngrounded = p.value("allow_ungrounded", false);
        // M0 (WorldForgeV2Plan §5 M0): a blocked walkability gate FAILS the build unless
        // {"allow_unwalkable": true}. Owner ruling 2026-10-14: mark it failed, keep the
        // voxels, retry after the generator is fixed - never patch the world.
        res.allowUnwalkable = p.value("allow_unwalkable", false);
        if (chunkManager && !allowUngrounded) {
            const int gScanTop = oy + 64;
            int missing = 0;
            glm::ivec2 firstMiss(-1);
            for (int x = ox; x < ox + W; ++x)
                for (int z = oz; z < oz + D; ++z) {
                    bool found = false;
                    for (int y = gScanTop; y >= 0; --y)
                        if (chunkManager->hasVoxelAt(glm::ivec3(x, y, z))) { found = true; break; }
                    if (!found) {
                        if (missing == 0) firstMiss = {x, z};
                        ++missing;
                    }
                }
            if (missing > 0) {
                res.error = {{"error", "ungrounded site: " + std::to_string(missing) + " of " +
                               std::to_string(W * D) + " columns in the settlement rect have no "
                               "terrain - generate/stream terrain there first, or pass "
                               "allow_ungrounded:true"},
                     {"ungrounded_columns", missing}, {"site_columns", W * D},
                     {"first_ungrounded", {{"x", firstMiss.x}, {"z", firstMiss.y}}}};
                return res;
            }
        }

        // PROGRAM MODE (era + tier — resources/settlement_program.json): morphology, typology
        // weights and plot sizing become tier DATA (the era key is the extension hook: only
        // `medieval` ships; an unknown era/tier is a SURFACED error, never a silent default).
        // Legacy calls (no era/tier param) take exactly the pre-program code paths.
        const bool programMode = p.contains("tier") || p.contains("era");
        const std::string era = p.value("era", std::string("medieval"));
        const std::string tierName = p.value("tier", std::string("village"));
        const unsigned seed = static_cast<unsigned>(p.value("seed", static_cast<int>(varietySeed)));
        Core::SettlementProgramRegistry programReg;
        const Core::SettlementTierPreset* tierP = nullptr;
        // M3b density lever ("a very dense city"): clamped [0.5, 2]; 1.0 = identity. The
        // densified copy must outlive every use of tierP in this planning pass.
        const double density = std::clamp(p.value("density", 1.0), 0.5, 2.0);
        Core::SettlementTierPreset densified;
        Core::SettlementTierPreset sited;   // site-scaled copy (scaleForSite), same lifetime rule
        if (programMode) {
            // Overridable ({"settlement_program": path}) like room_program, so a test can
            // declare an era the room programs have no pack for and drive that refusal.
            const std::string settlementProgramPath =
                p.value("settlement_program", std::string("resources/settlement_program.json"));
            if (!programReg.loadFromFile(settlementProgramPath)) {
                res.error = {{"error", "settlement program not loadable: " + settlementProgramPath},
                             {"settlement_program", settlementProgramPath}};
                return res;
            }
            tierP = programReg.get(era, tierName);
            if (!tierP) {
                res.error = {{"error", "unknown era/tier: " + era + "/" + tierName},
                     {"known_eras", programReg.eras()}, {"known_tiers", programReg.tiers(era)}};
                return res;
            }
            if (density != 1.0) {
                densified = Core::applyDensity(*tierP, density);
                tierP = &densified;
            }
            // After density: the cap grows with the site beyond the tier's reference site.
            if (tierP->referenceSiteW > 0 && tierP->referenceSiteD > 0) {
                sited = Core::scaleForSite(*tierP, W, D);
                tierP = &sited;
            }
            if (tierP->morphology == "cluster") {
                // cluster reuses the legacy scatter/grid layout; the tier contributes its weighted
                // palette (weight-EXPANDED so pickBuildingVariant's uniform cycle honors the weights).
                mix.clear();
                for (const auto& [typ, wgt] : tierP->typologyWeights)
                    for (int k = 0; k < wgt; ++k) mix.push_back(typ);
                if (mix.empty()) mix = {"croft"};
            }
        }
        // main_street AND semi_organic (the city) share the whole street-settlement pipeline —
        // the city planner returns the same MainStreetLayout shape (axes + square + assigned).
        const bool cityMode = tierP && tierP->morphology == "semi_organic";
        const bool mainStreetMode = cityMode || (tierP && tierP->morphology == "main_street");
        // Typology natural sizes: a building must be sized to its TYPOLOGY (croft small, hall a big
        // hall) — main-street mode sizes the PLOT from the typology up front (the burgage principle);
        // legacy mode centres the natural footprint in its uniform plot below.
        Core::RoomProgramRegistry roomReg;
        // M0 (WorldForgeV2Plan §5 M0): the room program is a HARD dependency - its load result
        // was ignored, so a missing file dropped every plot and reported "footprint too small".
        // The path is overridable ({"room_program": path}) so the failure is testable, and the
        // settlement's era loads its data pack (resources/room_programs/<era>.json) - an era
        // without a pack refuses here instead of building medieval houses and calling them <era>.
        const std::string roomProgramPath = p.value("room_program", std::string("resources/room_program.json"));
        if (!roomReg.loadFromFile(roomProgramPath)) {
            res.error = {{"error", "room program not loadable: " + roomProgramPath +
                                  " (every typology's size comes from it; nothing was planned)"},
                         {"room_program", roomProgramPath}};
            return res;
        }
        if (era != "medieval") {
            const std::string pack = "resources/room_programs/" + era + ".json";
            if (!roomReg.loadPeriodPack(pack, era)) {
                res.error = {{"error", "no room-program pack for era '" + era + "': " + pack +
                                      " (the engine never substitutes another era's buildings)"},
                             {"era", era}, {"room_program_pack", pack},
                             {"known_periods", roomReg.periods()}};
                return res;
            }
        }

        // ground top at a WORLD column = the top solid voxel (the seatStructure primitive). Shared by
        // the terrain buildability scan AND per-building seating.
        // Column scans (see settlementTopScan): raw = what occupies the column (clearing);
        // flora-blind = the GROUND elevation (grading/seating/terracing/buildability/fences —
        // L4 find 2026-07-09: canopy tops read as hills, trunks as cliffs).
        auto groundTopAt = [chunkManager, placedObjectManager, objectTemplateManager, locationRegistry, npcManager, pushUndo, oy](int wx, int wz) -> int {
            return settlementTopScan(chunkManager, oy, wx, wz, /*floraBlind=*/false);
        };
        auto isFloraMat = [](const std::string& m) {
            return m.rfind("Log", 0) == 0 || m.rfind("Leaf", 0) == 0;
        };
        auto terrainTopAt = [chunkManager, placedObjectManager, objectTemplateManager, locationRegistry, npcManager, pushUndo, oy](int wx, int wz) -> int {
            return settlementTopScan(chunkManager, oy, wx, wz, /*floraBlind=*/true);
        };
        // M0 (WorldForgeV2Plan §5 M0): WATER is unbuildable. A column is water when the world
        // holds a water span on it (the chunk's persisted "water as world data" — loaded and
        // edited worlds, headless rigs) or when the generator says it is a river channel or an
        // open-water (lake / sea) column. Until this, analyzeSite received an EMPTY predicate
        // and a creek bed inside the site rect was "buildable".
        auto waterAt = [chunkManager, terrainTopAt](int wx, int wz) -> bool {
            if (!chunkManager) return false;
            const int top = terrainTopAt(wx, wz);
            for (int yy : {top, top + 1}) {
                const glm::ivec3 wp(wx, yy, wz);
                if (Chunk* ch = chunkManager->getChunkAtFast(wp)) {
                    const glm::ivec3 lp = Utils::CoordinateUtils::worldToLocalCoord(wp);
                    for (const auto& sp : ch->getWaterSpans())
                        if (sp.x == lp.x && sp.z == lp.z && sp.top > sp.bottom) return true;
                }
            }
            if (WorldGenerator* gen = chunkManager->getStreamingGenerator()) {
                if (gen->channelHitAt(wx + 0.5f, wz + 0.5f).hit) return true;   // null-safe without a bake
                if (gen->hydrology()) {
                    WaterSpan ws;
                    if (gen->waterSpanAt(wx, wz, ws)) return true;
                }
            }
            return false;
        };

        // TERRAIN MODE: analyse the live world's buildability and place plots on the buildable cells
        // (flat valleys + hilltops), skipping cliffs/steep. Else: the flat-plane grid.
        const bool terrain = p.value("terrain", false);
        Core::SettlementLayout layout;
        Core::MainStreetLayout msl;   // program main-street plan (used when mainStreetMode)
        int droppedPlots = 0;         // main-street plots dropped on unbuildable terrain (surfaced)
        std::vector<Core::PlacedBuilding> buildings;
        // ── M5a (WorldForgeV2Plan §M5, 2026-11-05): EVERY program tier is realized from the settlement MAP
        // (D41). The headless planner bakes it from injected samplers - the generator's columns when the
        // world has one (chunk-independent: a pure function of world position), the ground scan on a loaded
        // world without a generator, a flat field headless - and the roads arriving at the site (`entries`,
        // site-local, from WorldForgeBuildService); the units consume its projection (lots -> plots and
        // assigned plots, axis-aligned ways -> street rects, the market area -> the square). The legacy
        // non-program path (no era/tier) keeps the grid below.
        Core::SettlementMap siteMap;
        std::string mapSampler = "flat";
        std::vector<std::vector<glm::ivec2>> mapWayCells;   // the ways' node cells (WORLD cubes) for the paths unit
        std::vector<Core::WayRibbon> mapRibbons;             // M5b-1: the same cells with each way's paved width, for the ribbon paver
        std::vector<glm::ivec4> mapFoot;                     // M5b-1 (chain 114): the map's lot footprints (x, z, w, d) world cubes - the ways never pave inside one
        glm::ivec2 mapWellCell(0); bool mapWell = false;      // M5b-1: the village well on the primary's verge (the projection's)
        int primaryWayId = -1;
        // {"legacy_layout": true}: the layout AS BUILT before M5a for a program tier - the CONTROL of the L4
        // A/B (the same site, seed and terrain through the grid, so a gate refusal can be attributed)
        const bool legacyLayout = p.value("legacy_layout", false);
        const bool mapLots = (tierP != nullptr) && !legacyLayout;
        if (mapLots) {
            // the standing contract (SettlementBuildServiceTest.TerrainModeWithoutAWorldIsRefused, chain 66
            // caught its loss): terrain mode without a world is refused, not planned on a flat stand-in
            if (terrain && !chunkManager) { res.error = {{"error", "terrain mode needs a loaded world (no ChunkManager)"}}; return res; }
            Core::SettlementMapInput mi;
            mi.tier = tierP; mi.rooms = &roomReg; mi.era = era; mi.seed = seed;
            mi.W = W; mi.D = D; mi.originX = ox; mi.originY = oy; mi.originZ = oz;
            WorldGenerator* mapGen = (chunkManager && terrain) ? chunkManager->getStreamingGenerator() : nullptr;
            if (mapGen && mapGen->hydrology()) {
                // L4 runs 1-2 (2026-11-05): sampleSurface is the FULL column (~7 ms each on Release: 9.4 s for a
                // 40 x 32 hamlet); naturalGroundAt is the natural emitted ground (phases 0-2, pinned equal to the
                // sampler's bed height off channels, coasts and roads) - the field the box plan was routed on
                mapSampler = "generator";
                mi.heightAt = [mapGen, ox, oz](int x, int z) { return static_cast<int>(std::floor(mapGen->naturalGroundAt(ox + x, oz + z))); };
            } else if (chunkManager && terrain) {
                mapSampler = "chunks";
                mi.heightAt = [terrainTopAt, ox, oz](int x, int z) { return terrainTopAt(ox + x, oz + z); };
            } else {
                mi.heightAt = [oy](int, int) { return oy; };
            }
            if (mapGen && mapGen->hydrology()) {
                // L4 run 3: the build service's waterAt calls waterSpanAt (a bounded basin flood, ~7 ms a
                // column) - 1280 calls were the 9 s bake. The planner's water is the generator's own: a
                // carved channel under the column, or the hydrology's (rim-corrected) standing level above
                // the natural ground. The chunks' PLACED water spans (an edited world) are not consulted
                // by the planner; the realizer's units still are (waterAt below).
                mi.waterAt = [mapGen, ox, oz](int x, int z) {
                    const int wx = ox + x, wz = oz + z;
                    if (mapGen->channelHitAt(static_cast<float>(wx) + 0.5f, static_cast<float>(wz) + 0.5f).hit) return true;
                    const float level = mapGen->waterLevelAt(static_cast<float>(wx), static_cast<float>(wz));
                    return level > mapGen->naturalGroundAt(wx, wz);
                };
            } else if (chunkManager && terrain) {
                mi.waterAt = [waterAt, ox, oz](int x, int z) { return waterAt(ox + x, oz + z); };
            }
            if (p.contains("entries") && p["entries"].is_array())
                for (const auto& e : p["entries"]) if (e.is_object()) mi.entries.push_back({e.value("x", 0), e.value("z", 0), e.value("cls", 1)});
            mi.maxRelief = p.value("max_relief", 6);
            mi.reuse = p.value("route_reuse", 0.5f);
            if (p.value("map_water_check", false) && mi.waterAt && chunkManager && terrain) {   // (also run below the legacy branch)
                // audit E8: the planner's water (channelHitAt + the hydrology level above the natural ground) against
                // the realizer's waterAt (placed spans + channel + waterSpanAt's flood) over every cell of the site
                int disagree = 0, plannerWet = 0, realizerWet = 0;
                for (int z = 0; z < D; ++z) for (int x = 0; x < W; ++x) {
                    const bool a = mi.waterAt(x, z), b = waterAt(ox + x, oz + z);
                    plannerWet += a ? 1 : 0; realizerWet += b ? 1 : 0; disagree += (a != b) ? 1 : 0;
                }
                LOG_INFO_FMT("Settlement", "[M5A-WATER-CHECK] (map branch: mi.waterAt vs the realizer's waterAt) " << W << " x " << D << ": planner wet "
                             << plannerWet << ", realizer wet " << realizerWet << ", cells that disagree " << disagree);
            }
            siteMap = Core::planSettlementMap(mi);
            if (!siteMap.ok()) {
                res.error = {{"error", siteMap.refusal}, {"map_sampler", mapSampler}, {"dropped_lots", siteMap.droppedLots},
                             {"buildable_fraction", siteMap.buildableFraction},
                             {"seated_lots", siteMap.lots.size()}, {"target_lots", siteMap.targetLots}};
                res.map = siteMap.toJson();   // the partial map stays on record (L4 run 2: a planner refusal left none)
                return res;
            }
            const Core::MapProjection proj = Core::projectMap(siteMap);
            layout = proj.layout;
            msl = proj.msl;
            mapWellCell = proj.wellCell; mapWell = proj.hasWell; primaryWayId = proj.primaryWay;
            droppedPlots = siteMap.droppedLots;
            for (const auto& l : siteMap.lots) {
                Core::PlacedBuilding b;
                b.plotIndex = l.id;
                b.footprint = l.footprint;
                b.typology = l.typology;
                buildings.push_back(b);
                mapFoot.push_back(glm::ivec4(ox + l.footprint.x, oz + l.footprint.z, l.footprint.w, l.footprint.d));
            }
            for (const auto& w : siteMap.ways) {
                std::vector<glm::ivec2> cells;
                for (int nid : w.nodes) if (const Core::MapNode* n = siteMap.node(nid)) cells.push_back({ox + n->x, oz + n->z});
                if (cells.size() >= 2) {
                    mapRibbons.push_back({cells, std::max(1, static_cast<int>(std::lround(w.width)))});
                    mapWayCells.push_back(std::move(cells));
                }
            }
            LOG_INFO_FMT("Settlement", "[M5A-MAP] " << era << "/" << tierName << " seed " << seed << ": " << siteMap.lots.size() << " lots, "
                         << siteMap.ways.size() << " ways, " << siteMap.nodes.size() << " nodes, entry " << siteMap.entryNode
                         << ", sampler " << mapSampler << ", " << siteMap.droppedLots << " dropped, bake " << siteMap.bakeMs << " ms");
            if (buildings.empty()) {
                res.error = {{"error", "the settlement map carries no lot"}, {"map", siteMap.toJson()}};
                return res;
            }
        } else if (terrain) {
            if (!chunkManager) { res.error = {{"error", "terrain mode needs a loaded world (no ChunkManager)"}}; return res; }
            if (p.value("map_water_check", false)) {   // audit E8, on the legacy branch too (the L4 control runs here)
                if (WorldGenerator* wg = chunkManager->getStreamingGenerator(); wg && wg->hydrology()) {
                    int disagree = 0, plannerWet = 0, realizerWet = 0;
                    for (int z = 0; z < D; ++z) for (int x = 0; x < W; ++x) {
                        const int wx = ox + x, wz = oz + z;
                        const bool a = wg->channelHitAt(static_cast<float>(wx) + 0.5f, static_cast<float>(wz) + 0.5f).hit
                                    || wg->waterLevelAt(static_cast<float>(wx), static_cast<float>(wz)) > wg->naturalGroundAt(wx, wz);
                        const bool b = waterAt(wx, wz);
                        plannerWet += a ? 1 : 0; realizerWet += b ? 1 : 0; disagree += (a != b) ? 1 : 0;
                    }
                    LOG_INFO_FMT("Settlement", "[M5A-WATER-CHECK] (legacy branch: a COPY of the planner's predicate vs the realizer's waterAt) " << W << " x " << D
                                 << ": planner wet " << plannerWet << ", realizer wet " << realizerWet << ", cells that disagree " << disagree);
                }
            }
            const int plotSize  = p.value("plot_size", 12);
            const int maxRelief = p.value("max_relief", 6);
            const int maxPlots  = p.value("max_plots", 25);
            const int window    = std::max(1, plotSize / 2);
            const Core::BuildabilityMap site =
                Core::analyzeSite(W, D, maxRelief,
                                  [&](int x, int z) { return terrainTopAt(ox + x, oz + z); },
                                  [&](int x, int z) { return waterAt(ox + x, oz + z); },   // M0: water is unbuildable
                                  1, window);
            if (mainStreetMode) {
                // The spine picks the FLATTEST straight alignment over the site, then plots whose
                // footprint touches an unbuildable cell are dropped with a surfaced count
                // (graceful degradation, TerrainAwareSettlement.md).
                // Optional road-arrival bias ({"street_axis": "x"|"z"}, WorldForge passes it):
                // prefer the spine axis the arriving road runs along; terrain still wins.
                char prefAxis = 0;
                const std::string sa = p.value("street_axis", std::string());
                if (sa == "x" || sa == "X") prefAxis = 'X';
                else if (sa == "z" || sa == "Z") prefAxis = 'Z';
                // Optional lateral arrival preference ({"street_offset": site-local cross
                // coordinate of the road's arrival CENTER}): converted to a band start so
                // street and road meet head-on where terrain allows.
                int prefOffset = -1;
                if (p.contains("street_offset") && p["street_offset"].is_number_integer())
                    prefOffset = std::max(0, p["street_offset"].get<int>() -
                                                 tierP->street.mainWidth / 2);
                const Core::StreetAxisChoice pick =
                    Core::chooseStreetAxis(site, tierP->street.mainWidth, tierP->plot.depthMin,
                                           prefAxis, prefOffset);
                msl = cityMode
                    ? Core::planCityLayout(*tierP, W, D, roomReg, seed)   // city picks its own axes
                    : Core::planMainStreetLayout(*tierP, W, D, roomReg, seed,
                                                 pick.axis, pick.crossOffset);
                std::vector<Core::AssignedPlot> keep;
                for (const auto& ap : msl.assigned) {
                    bool buildable = true;
                    const Core::Rect& pr = ap.plot.rect;
                    for (int z = pr.z; z < pr.z1() && buildable; ++z)
                        for (int x = pr.x; x < pr.x1(); ++x) {
                            if (x < 0 || z < 0 || x >= site.W || z >= site.D) { buildable = false; break; }
                            const auto cls = site.at(x, z).cls;
                            if (cls == Core::Buildability::TooSteep ||
                                cls == Core::Buildability::Water) { buildable = false; break; }
                        }
                    if (buildable) keep.push_back(ap); else ++droppedPlots;
                }
                msl.assigned = std::move(keep);
                msl.base.plots.clear();
                for (const auto& ap : msl.assigned) msl.base.plots.push_back(ap.plot);
                layout = msl.base;
                LOG_INFO_FMT("Settlement", "main_street terrain: axis " << pick.axis << " offset "
                             << pick.crossOffset << ", " << msl.assigned.size() << " plots ("
                             << droppedPlots << " dropped unbuildable)");
                if (msl.assigned.empty()) {
                    res.error = {{"error", "terrain too steep - no buildable main-street plots"},
                         {"buildable_fraction", site.buildableFraction()},
                         {"dropped_plots", droppedPlots}};
                    return res;
                }
            } else {
                layout.plots = Core::selectBuildablePlots(site, plotSize, streetWidth, maxPlots);
                LOG_INFO_FMT("Settlement", "terrain analyse " << W << "x" << D << ": buildable="
                             << site.buildableFraction() << " -> " << layout.plots.size() << " plots");
                if (layout.plots.empty()) {
                    res.error = {{"error", "terrain too steep - no buildable plots"},
                         {"buildable_fraction", site.buildableFraction()}};
                    return res;
                }
            }
        } else if (mainStreetMode) {
            msl = cityMode ? Core::planCityLayout(*tierP, W, D, roomReg, seed)
                           : Core::planMainStreetLayout(*tierP, W, D, roomReg, seed);
            if (!msl.ok) {
                res.error = {{"error", "settlement footprint too small for a main street at tier '"
                                + tierName + "'"}};
                return res;
            }
            layout = msl.base;
        } else {
            layout = Core::subdividePlots(W, D, cols, rows, streetWidth, minBuilding);
            if (layout.plots.empty()) {
                res.error = {{"error", "settlement footprint too small for the requested grid + min_building"}};
                return res;
            }
        }
        // Buildings (the legacy non-program path; a program tier's come from the map above): main-street
        // mode already assigned + sized every building from its typology (footprint = natural size, front
        // at the drawn setback); legacy mode sites one per plot and sizes it in the queue loop below.
        if (mapLots) {
            // from the map
        } else if (mainStreetMode) {
            for (size_t i = 0; i < msl.assigned.size(); ++i) {
                Core::PlacedBuilding b;
                b.plotIndex = static_cast<int>(i);
                b.footprint = msl.assigned[i].footprint;
                b.typology = msl.assigned[i].typology;
                buildings.push_back(b);
            }
        } else {
            buildings = Core::populatePlots(layout, setback, minBuilding, typology);
        }
        if (buildings.empty()) {
            res.error = {{"error", "no buildable plots (setback too large for the plots)"}};
            return res;
        }

        // [no-frozen-engine] From here the heavy phases become sliced WORK UNITS (one per frame
        // via MainThreadJobs) instead of running inline in one multi-minute frame. Shared bits
        // that cross units live behind shared_ptrs; async is the DEFAULT ({"async": false} runs
        // every unit inline for callers needing the full synchronous response).
        std::vector<WorkUnit> units;
        auto pathsJsonP = res.paths;
        auto walkJsonP  = res.walkability;
        const bool allowUnwalkable = res.allowUnwalkable;
        auto propsJsonP = res.yardProps;
        auto residentsJsonP = res.residents;
        auto sharedPaved = std::make_shared<std::set<std::pair<int, int>>>();
        // M5b-1 (chain 107): the WAYS' paved columns alone (a snapshot taken when the door spurs begin) - the doorsteps'
        // anchors; a spur's own paving is the door's approach, never the way the approach must reach
        auto sharedWayPaved = std::make_shared<std::set<std::pair<int, int>>>();
        // M5b-1 (chain 109): threshold - pad * 9 as MEASURED per style on the buildings already built here (12 or 14 live)
        auto sharedPadOffset = std::make_shared<std::map<std::string, int>>();
        // Road-band cells (world coords, walkable headroom over the paving) recorded by
        // the street unit for the late "street sweep": construction can drop debris on a
        // finished road (a building's site prep UNDERMINES a roadside tree -> U4 topples
        // it -> the felled trunk RETIRES as static voxels across the street; measured,
        // deterministic at seed 3) — swept again after the buildings, before nav rebuild.
        auto sharedRoadBand = std::make_shared<std::vector<glm::ivec3>>();
        // O9: the cube AABB of every prop this build places BEFORE the sweep (the well, the yard props) - the sweep
        // never clears a cell inside one (planStreetSweep)
        auto sharedPropBoxes = std::make_shared<std::vector<std::pair<glm::ivec3, glm::ivec3>>>();
        // Front doors of the buildings that actually BUILT: marker cell (2 out from the
        // door, from the forge's front-door location), footprint, threshold micro.
        struct FrontDoor {
            glm::ivec3 marker; glm::ivec4 foot; int thresholdMicro;
            std::string typology;    ///< for the gate's route labels
            nlohmann::json rooms;    ///< [{story, purpose, rect:[x,z,w,d]}] building-local cubes
            // M5b-1 (chain 93): the forge's door - the wall cube it is cut through and the outward normal.
            // The marker above is SNAPPED to standable ground and can sit round the corner; the gate, the
            // spurs and the doorsteps read THIS door when it is recorded, the marker's side otherwise.
            bool hasDoor = false; glm::ivec2 door{0, 0}; glm::ivec2 normal{0, 0};
            int padY = INT_MIN;   // the pad level the forge leveled to (the requested one when the seat set it), for the record
        };
        auto sharedFrontDoors = std::make_shared<std::vector<FrontDoor>>();
        // M5a-1 (chain 74): the buildings that realized, so the gate can tell "no door recorded" from "nothing built"
        auto sharedBuilt = std::make_shared<int>(0);
        // Per-micro-column pavement stamp ({x, startRow, z, surface}, micro coords) so the
        // sweep can re-stamp the paving it clears along with the debris.
        auto sharedStamp = std::make_shared<std::vector<glm::ivec4>>();

        // PARCEL CLEARING (walkability-by-construction; USER find: trees overlapped
        // buildings): flora generates at worldgen and nothing ever cleared the plots —
        // building pads cut only their own footprint (cube-only at that), and neighbor
        // trees' canopies overhang roofs and block yard paths. Wipe every plot + a
        // canopy margin of ALL above-ground content at EVERY resolution before any
        // site prep. Only terrain + flora exist at this stage (site prep precedes all
        // buildings), so no material filter is needed; the ground surface itself stays.
        // M0 (WorldForgeV2Plan §5 M0): the canopy margins are DERIVED from the trees the site's
        // biome can grow (F4b read the library: ordinary oaks half-width 8, forge_enchanted_oak
        // 11.5, forge_jungle_l 9.5, megaflora up to 26) against literals of 4 (parcel clearing)
        // and 8 (orphan sweep) that only covered ordinary oaks. Rule: parcel margin = widest
        // crown half-width of the biome's pool + extra layers (floor 4); a procedural-mode entry
        // has no template, so the library's widest loaded crown bounds it; the orphan-canopy
        // sweep reaches 2x (a trunk inside the band leaves canopy one more radius out).
        int parcelClearMargin = 4, canopySweepMargin = 8;
        std::string canopyMarginSource = "default: no generator or template library";
        // M0: terrace materials from the SITE BIOME (a snow village is not resurfaced in temperate
        // turf), fences and cluster paths from the tier PROGRAM - no literals in the units.
        std::string terraceTop = "Grass", terraceFill = "Dirt";
        if (chunkManager) {
            if (WorldGenerator* gen = chunkManager->getStreamingGenerator()) {
                const auto& biomes = gen->getBiomes();
                const int bi = gen->sampleSurface(ox + W / 2, oz + D / 2).biomeIndex;
                if (bi >= 0 && bi < static_cast<int>(biomes.size())) {
                    terraceTop  = biomes[bi].surfaceMaterial;
                    terraceFill = biomes[bi].subsurfaceMaterial;
                }
            }
        }
        const std::string fenceMat = (programMode && tierP) ? tierP->fenceMaterial : std::string("Log");
        const std::string pathMat  = (programMode && tierP) ? tierP->street.material : std::string("Cobblestone");
        if (chunkManager && objectTemplateManager) {
            if (WorldGenerator* gen = chunkManager->getStreamingGenerator()) {
                const auto& biomes = gen->getBiomes();
                const int bi = gen->sampleSurface(ox + W / 2, oz + D / 2).biomeIndex;
                if (bi >= 0 && bi < static_cast<int>(biomes.size())) {
                    std::vector<int> halves;
                    auto add = [&](const std::vector<std::pair<std::string, int>>& items, const std::string& mode) {
                        for (const auto& item : items) {
                            const int hw = objectTemplateManager->templateHalfFootprintColumns(item.first);
                            halves.push_back(hw > 0 ? hw : (mode == "procedural" ? objectTemplateManager->floraMarginColumns() : 0));
                        }
                    };
                    add(biomes[bi].flora, biomes[bi].floraMode);
                    for (const auto& L : biomes[bi].extraFloraLayers) add(L.items, L.mode);
                    parcelClearMargin = canopyMarginCubes(halves, 4);
                    canopySweepMargin = std::max(8, 2 * parcelClearMargin);
                    canopyMarginSource = "biome pool: " + biomes[bi].name;
                }
            }
        }
        if (chunkManager) {
            std::vector<Core::Rect> prects;
            prects.reserve(layout.plots.size());
            for (const auto& pl : layout.plots) prects.push_back(pl.rect);
            units.push_back({"clearing parcels",
                [chunkManager, placedObjectManager, objectTemplateManager, locationRegistry, npcManager, pushUndo, prects, ox, oz, terrainTopAt, groundTopAt,
                 parcelClearMargin]() {
                if (!chunkManager) return;
                const int kMargin = parcelClearMargin;    // M0: canopy overhang reach beyond the plot line, derived
                std::map<Chunk*, std::vector<glm::ivec3>> byChunk;
                std::set<std::pair<int, int>> seen;
                for (const auto& pr : prects) {
                    for (int x = ox + pr.x - kMargin; x < ox + pr.x + pr.w + kMargin; ++x)
                        for (int z = oz + pr.z - kMargin; z < oz + pr.z + pr.d + kMargin; ++z) {
                            if (!seen.insert({x, z}).second) continue;
                            const int g = terrainTopAt(x, z);          // flora-blind ground
                            const int top = std::max(groundTopAt(x, z), g + 12);
                            for (int y = g + 1; y <= top; ++y) {
                                const glm::ivec3 wp(x, y, z);
                                if (Chunk* ch = chunkManager->getChunkAtFast(wp))
                                    byChunk[ch].push_back(wp - ch->getWorldOrigin());
                            }
                        }
                }
                int cleared = 0;
                for (auto& [ch, cells] : byChunk) {
                    cleared += ch->clearCellsBulk(cells);
                    chunkManager->markChunkDirty(ch);
                }
                LOG_INFO_FMT("Settlement", "parcel clearing: " << cleared
                             << " cells over " << seen.size() << " columns (margin "
                             << kMargin << ")");
            }});
        }

        // TERRACE (settlement pre-pass, terrain mode): treat each parcel (structure + its yard) as
        // ONE unit and grade it into the terrain BEFORE any building seats — so the yard is flat,
        // the floor lands flush, and the fence sits on level ground. One PARCEL = one work unit.
        if (terrain && chunkManager) {
            for (size_t pi = 0; pi < layout.plots.size(); ++pi) {
                const Core::Rect pr = layout.plots[pi].rect;   // by value into the unit
                const std::string ph = "terracing parcel " + std::to_string(pi + 1) + "/" +
                                       std::to_string(layout.plots.size());
                units.push_back({ph, [chunkManager, placedObjectManager, objectTemplateManager, locationRegistry, npcManager, pushUndo, pr, ox, oz, oy, terrainTopAt, groundTopAt,
                                      terraceTop, terraceFill]() {
                    if (!chunkManager) return;
                    const int SK = 4;             // skirt width blending plot grade -> natural
                    const int px0 = ox + pr.x, pz0 = oz + pr.z, pw = pr.w, pd = pr.d;
                    if (pw < 2 || pd < 2) return;
                    std::vector<int> tops;
                    for (int x = px0; x < px0 + pw; ++x)
                        for (int z = pz0; z < pz0 + pd; ++z) tops.push_back(terrainTopAt(x, z));
                    std::sort(tops.begin(), tops.end());
                    const int grade = tops[tops.size() / 2];      // median plot grade
                    std::vector<glm::ivec3> tcut;
                    Core::StructureResult tfill;
                    auto addT = [&](int x, int y, int z, const char* mat) {
                        Core::VoxelPlacement vp; vp.position = glm::ivec3(x, y, z);
                        vp.material = mat; vp.level = Core::VoxelLevel::Cube;
                        tfill.voxels.push_back(vp);
                    };
                    for (int x = px0 - SK; x < px0 + pw + SK; ++x)
                        for (int z = pz0 - SK; z < pz0 + pd + SK; ++z) {
                            const bool inPlot = x >= px0 && x < px0 + pw && z >= pz0 && z < pz0 + pd;
                            int target;
                            if (inPlot) {
                                target = grade;
                            } else {               // skirt: ramp grade -> natural, <=1/cube
                                const int dx = std::max({px0 - x, x - (px0 + pw - 1), 0});
                                const int dz = std::max({pz0 - z, z - (pz0 + pd - 1), 0});
                                const int dist = std::max(dx, dz);
                                const int nat = terrainTopAt(x, z);
                                target = (nat > grade) ? std::min(nat, grade + dist)
                                                       : std::max(nat, grade - dist);
                            }
                            // cur stays RAW (flora-inclusive): everything above the graded target —
                            // including a tree on the parcel — is cleared by the cut loop.
                            const int cur = groundTopAt(x, z);
                            if (target == cur) continue;
                            for (int y = target + 1; y <= cur; ++y) tcut.push_back(glm::ivec3(x, y, z));
                            for (int y = cur + 1; y < target; ++y) addT(x, y, z, terraceFill.c_str());   // M0: biome subsurface
                            tcut.push_back(glm::ivec3(x, target, z));   // resurface graded top
                            addT(x, target, z, terraceTop.c_str());    // M0: biome surface
                        }
                    if (!tcut.empty())         Core::StructureGenerator::removeVoxels(chunkManager, tcut);
                    if (!tfill.voxels.empty()) Core::StructureGenerator::place(chunkManager, tfill);
                    LOG_INFO_FMT("Settlement", "terrace: parcel graded (cut " << tcut.size()
                                 << ", fill " << tfill.voxels.size() << ")");
                }});
            }
            // one REGIONAL physics pass after ALL parcels (never whole-world, never per-parcel)
            units.push_back({"terrace physics", [chunkManager, placedObjectManager, objectTemplateManager, locationRegistry, npcManager, pushUndo, ox, oz, W, D, reqY = oy]() {
                if (chunkManager)
                    chunkManager->buildChunkPhysicsInRegion(
                        glm::ivec3(ox - 8, 0, oz - 8), glm::ivec3(ox + W + 8, reqY + 96, oz + D + 8));
            }});
        }

        nlohmann::json queued = nlohmann::json::array();
        // Building units are collected separately and appended AFTER the street/fence/prop units:
        // paving must run first (spur anchors sample ground — a built wall would read as terrain).
        std::vector<WorkUnit> buildingUnits;
        std::vector<glm::ivec3> doorCenters;  // per-building path anchor (footprint centre at seated ground)
        std::vector<glm::ivec4> bFoot;        // per-building footprint (x,z,w,d) — paths must not pave under it (V7)
        std::vector<int> lotToPlot;           // M0: lot index -> plot index (failed lots get no dressing)
        std::vector<WorkUnit> postBuildUnits; // M0: lot dressing (spurs/paths, fences, props) runs AFTER the lots exist
        for (size_t i = 0; i < buildings.size(); ++i) {
            const auto& b = buildings[i];
            const int plotX = ox + b.footprint.x, plotZ = oz + b.footprint.z;
            const int plotW = b.footprint.w, plotD = b.footprint.d;
            // terrain mode: seat each house on its LOCAL ground (top voxel at the footprint centre) so
            // it adapts to the hill, not the flat base Y. (Flush cut/fill is a Phase 2 follow-up.)
            // `seat_flat` (debug/test, default off): force base-Y seating even in terrain mode, so the
            // terrain-BLIND red baseline for the seating invariant is REPRODUCIBLE (verify_terrain_seating.py
            // --seat-flat). This isolates the seating variable: same buildable plots, only `by` changes.
            // Deterministic per-building variation: typology + style (material/roof) + footprint shape.
            // Main-street mode pins the ASSIGNED typology (the plot was sized from it); style/shape
            // still vary per plot.
            // M5a audit E1 (2026-11-05): a MAP lot pins its typology too - the cluster tier drew from the mix
            // cycle and built three of five croft lots as longhouses and a hall house squeezed into 6 x 8
            // (TheQueuedBuildingsAreTheMapsLots, red on chain 69)
            const auto variantMix =
                (mainStreetMode || mapLots) ? std::vector<std::string>{b.typology} : mix;
            const Core::BuildingVariant var =
                Core::pickBuildingVariant(static_cast<int>(i), variantMix, styles, varietySeed);
            // M3 repair-then-refuse: the plan-time FALLBACK variant for this lot — a
            // salted re-pick (different typology/style/shape where the mix allows) the
            // work unit tries once if the first variant's build REFUSES at a forge gate.
            const Core::BuildingVariant var2 = Core::pickBuildingVariant(
                static_cast<int>(i) + 7919, variantMix, styles, varietySeed);
            // Natural footprint from the typology canon: long axis = bays * bay_length, short axis =
            // width_max; oriented along the plot's longer side, clamped to the plot, then CENTRED.
            // (Main-street mode: b.footprint IS the natural, street-oriented footprint already —
            // planMainStreetLayout sized the plot from the typology, so no resize here.)
            auto naturalRect = [&](const Core::BuildingVariant& v) {
                int w = plotW, d = plotD;
                if (!mainStreetMode && !mapLots) {   // a map lot's footprint IS the typology's natural, oriented size
                    if (const Core::RoomProgram* rp = roomReg.get(v.typology)) {
                        const Core::NaturalFootprint nf = Core::naturalFootprintOf(*rp);   // M5b-1: one derivation
                        const int natLong = nf.longU, natShort = nf.shortU;
                        if (plotW >= plotD) { w = natLong; d = natShort; }
                        else                { w = natShort; d = natLong; }
                        w = std::min(std::max(1, w), plotW);
                        d = std::min(std::max(1, d), plotD);
                    }
                }
                // centre the building in its plot
                return std::array<int, 4>{w, d, plotX + (plotW - w) / 2, plotZ + (plotD - d) / 2};
            };
            const auto [bw, bd, bx, bz]     = naturalRect(var);
            const auto [bw2, bd2, bx2, bz2] = naturalRect(var2);
            // Terrain seating happens INSIDE the building unit (post-terrace units); this value
            // is the PLAN estimate used for path anchors + the response echo.
            const int by = (terrain && !seatFlat)
                ? terrainTopAt(bx + bw / 2, bz + bd / 2)
                : oy;
            const bool seatInUnit = terrain && !seatFlat;
            // M5b-1 (chain 101, DESIGN DECISION): a MAP lot fronting a way is built at the WAY'S level at its door - the
            // door-approach cell beyond the lot rect's front edge (the planner's lotDoorApproach), read from the ways
            // unit's paved stamp at seat time. The natural ground stays the seat where no paving lies there.
            glm::ivec2 approachCell(INT_MIN, INT_MIN);
            glm::ivec2 approachNormal(0, 0);   // the door's outward normal (chain 112: the seat walks it from the door)
            if (mapLots && i < msl.assigned.size()) {
                const auto& ap = msl.assigned[i];
                const int afx = ap.footprint.x, afz = ap.footprint.z, afw = ap.footprint.w, afd = ap.footprint.d;
                switch (ap.streetSide) {
                    case 'N': approachCell = {afx + afw / 2, ap.plot.rect.z1()}; approachNormal = {0, 1}; break;
                    case 'S': approachCell = {afx + afw / 2, ap.plot.rect.z - 1}; approachNormal = {0, -1}; break;
                    case 'E': approachCell = {ap.plot.rect.x1(), afz + afd / 2}; approachNormal = {1, 0}; break;
                    case 'W': approachCell = {ap.plot.rect.x - 1, afz + afd / 2}; approachNormal = {-1, 0}; break;
                    default: break;
                }
                if (approachCell.x != INT_MIN) approachCell += glm::ivec2(ox, oz);
            }
            // Street-facing front hint (OpeningsLayoutTest.FrontHint*): the entrance wall faces
            // the plot's street side. The building rect is axis-aligned in settlement coords, so
            // 'S' (street at -z) = its local z0 wall, etc. No street (terrain mode) = no hint.
            std::string front;
            const char fside = (mainStreetMode || (mapLots && i < msl.assigned.size()))
                ? msl.assigned[i].streetSide   // explicit: the side this plot ABUTS its way (the map, M5a) / the main street
                : ((b.plotIndex >= 0 && b.plotIndex < (int)layout.plots.size())
                       ? Core::streetSideForPlot(layout, layout.plots[b.plotIndex].rect)
                       : (char)0);
            switch (fside) {
                case 'S': front = "z0"; break;
                case 'N': front = "z1"; break;
                case 'W': front = "x0"; break;
                case 'E': front = "x1"; break;
                default: break;
            }
            if (mapLots && approachCell.x != INT_MIN) {
                // M5b-1 (chain 109, after the front hint): the DOOR is not always at the frontage's midpoint (a hall house's cross-passage door
                // sits at the hall / service boundary, four cells from it) and a way can climb 17 micro along one
                // frontage; the autofill is deterministic in (typology, footprint, front, the position seed), so the
                // door's wall cell is PREDICTED here exactly as the forge will place it, and the way is read at its
                // approach (two cells out along the normal). The midpoint stays the fallback.
                if (const Core::RoomProgram* rp = roomReg.get(var.typology)) {
                    Core::BuildingProgram pp;
                    pp.name = var.typology; pp.typology = var.typology; pp.footprintW = bw; pp.footprintD = bd;
                    pp.substructure = "slab"; pp.front = front;
                    Core::ProgStory st; st.height = 3; pp.stories.push_back(st);
                    const unsigned seed = (static_cast<unsigned>(bx) * 73856093u) ^ (static_cast<unsigned>(bz) * 19349663u) ^ 0x9e3779b9u;
                    if (Core::autofillRoomLayout(pp, seed ? seed : 1u, rp) && !pp.stories.empty()) {
                        const Core::ProgPortal* door = nullptr; bool onFront = false;
                        for (const auto& po : pp.stories[0].portals) {
                            if (po.kind != "door" || (po.a != "exterior" && po.b != "exterior")) continue;
                            const bool f = (front == "x0" && po.px == 0) || (front == "x1" && po.px == bw) || (front == "z0" && po.pz == 0) || (front == "z1" && po.pz == bd);
                            if (!door || (f && !onFront)) { door = &po; onFront = f; }
                        }
                        if (door) {
                            glm::ivec2 wall, nrm;
                            if (door->px == 0)       { wall = {bx, bz + door->pz}; nrm = {-1, 0}; }
                            else if (door->px == bw) { wall = {bx + bw - 1, bz + door->pz}; nrm = {1, 0}; }
                            else if (door->pz == 0)  { wall = {bx + door->px, bz}; nrm = {0, -1}; }
                            else                     { wall = {bx + door->px, bz + bd - 1}; nrm = {0, 1}; }
                            approachCell = wall + nrm * 2;
                            approachNormal = nrm;
                        }
                    }
                }
            }
            auto makeBp = [&](const Core::BuildingVariant& v, int w, int d, int x, int z) {
                return nlohmann::json{
                    {"schema", "v2"}, {"type", "house"}, {"style", v.style}, {"typology", v.typology},
                    {"period", era}, {"room_program", roomProgramPath},   // M0: the era reaches every building
                    {"footprint_shape", v.footprintShape}, {"front", front},
                    {"position", {{"x", x}, {"y", by}, {"z", z}}},
                    {"footprint", nlohmann::json::array({w, d})},
                    {"substructure", "slab"},
                    {"stories", nlohmann::json::array({nlohmann::json{{"height", 3}}})},
                    {"allow_ungrounded", allowUngrounded}   // settlement gate already ran
                };
            };
            nlohmann::json bp  = makeBp(var, bw, bd, bx, bz);
            nlohmann::json bp2 = makeBp(var2, bw2, bd2, bx2, bz2);
            // [no-frozen-engine] one building = one work unit calling the SAME v2 pipeline the
            // build_structure command uses (no queue-push: the whole queue drains in ONE frame,
            // which is exactly the freeze this replaces). Terrain seating re-samples the graded
            // ground at RUN time (terrace units precede building units).
            // M3: a REFUSED build (forge gate) re-rolls the lot ONCE with the fallback
            // variant; a second refusal leaves the lot honestly EMPTY and records it in
            // plan.lotFailures — a broken building never ships.
            const std::string ph = "building " + std::to_string(i + 1) + "/" +
                                   std::to_string(buildings.size());
            buildingUnits.push_back({ph, [chunkManager, placedObjectManager, objectTemplateManager,
                                          locationRegistry, npcManager, pushUndo, addPointLight, keepClear,
                                          itemPropManager, hostDeps = deps, bp, bp2, seatInUnit,
                                          bw, bd, bw2, bd2, oy, lotFailures = res.lotFailures,
                                          lotIndex = static_cast<int>(i), approachCell, approachNormal, sharedStamp, sharedPadOffset,
                                          typ1 = var.typology, typ2 = var2.typology,
                                          sharedFrontDoors, sharedBuilt]() mutable {
                if (!chunkManager) return;
                auto seat = [&](nlohmann::json& p, int w, int d) {
                    if (!seatInUnit) return;
                    const int cx = p["position"].value("x", 0) + w / 2;
                    const int cz = p["position"].value("z", 0) + d / 2;
                    const int natural = settlementTopScan(chunkManager, oy, cx, cz, /*floraBlind=*/true);
                    p["position"]["y"] = natural;
                    // M5b-1 (chain 101): the way's standing level at the door approach - the highest paved surface
                    // among the stamp's columns in the approach cell (then its 8 neighbours, nearest ring first)
                    if (approachCell.x != INT_MIN && sharedStamp && !sharedStamp->empty()) {
                        int wayTop = INT_MIN; glm::ivec2 at(INT_MIN, INT_MIN);
                        // chain 112: the cell the walker steps from INTO the doorway comes first - the way's paving can
                        // step 10 micro across its own width at a junction (1368 one cell out, 1378 two cells out: three
                        // neighbours on one street seated two cells out stood 12 above the cell at their door)
                        if (approachNormal.x != 0 || approachNormal.y != 0) {
                            const glm::ivec2 wall = approachCell - approachNormal * 2;
                            for (int k = 1; k <= 3 && wayTop == INT_MIN; ++k) {
                                const glm::ivec2 c = wall + approachNormal * k;
                                // chain 114: the way can step INSIDE the cell at the door (1368 on the door's axis, 1377 at
                                // its far edge): the centre column is the walker's level (the gate's convention), the
                                // lowest stamped column when the centre is not stamped
                                int centre = INT_MIN, lowest = INT_MAX;
                                for (const auto& st : *sharedStamp) {
                                    if (st.x < c.x * 9 || st.x >= c.x * 9 + 9 || st.z < c.y * 9 || st.z >= c.y * 9 + 9) continue;
                                    if (st.x == c.x * 9 + 4 && st.z == c.y * 9 + 4) centre = st.w;
                                    lowest = std::min(lowest, st.w);
                                }
                                if (centre != INT_MIN) { wayTop = centre; at = c; }
                                else if (lowest != INT_MAX) { wayTop = lowest; at = c; }
                            }
                        }
                        for (int r = 0; r <= 1 && wayTop == INT_MIN; ++r)
                            for (int dz = -r; dz <= r; ++dz) for (int dx = -r; dx <= r; ++dx) {
                                if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
                                const int ax = approachCell.x + dx, az = approachCell.y + dz;
                                for (const auto& c : *sharedStamp)
                                    if (c.x >= ax * 9 && c.x < ax * 9 + 9 && c.z >= az * 9 && c.z < az * 9 + 9 && c.w > wayTop) { wayTop = c.w; at = {ax, az}; }
                            }
                        if (wayTop != INT_MIN) {
                            // threshold = pad * 9 + offset: the offset MEASURED on this settlement's earlier buildings of the
                            // same style (chain 109: 12 on the town's, 14 on the village's), 12 until one is measured
                            const std::string styleName = p.value("style", std::string());
                            const int kThresholdAbovePad = sharedPadOffset->count(styleName) ? sharedPadOffset->at(styleName) : 12;
                            constexpr int kStepUp = 4;
                            const int padY = Core::padCubeForStreet(wayTop + 1, kThresholdAbovePad, kStepUp);
                            p["position"]["y"] = padY;
                            p["pad_y"] = padY;   // the forge's prepare_pad levels the footprint HERE (else the terrain median)
                            LOG_INFO_FMT("Settlement", "seat: " << p.value("typology", std::string("?")) << " @" << p["position"].value("x", 0) << ","
                                         << p["position"].value("z", 0) << " at the way's level: paved surface " << wayTop << " at (" << at.x << "," << at.y
                                         << ") -> pad y " << padY << " (natural " << natural << ")");
                        }
                    }
                };
                // M0: ONE mapping from the settlement's deps to a building's (Deps::structureDeps).
                Core::StructureBuildService::Deps deps = hostDeps.structureDeps();
                auto recordDoor = [&](const nlohmann::json& res, const nlohmann::json& prog, int w, int d) {
                    const int x = prog["position"].value("x", 0), z = prog["position"].value("z", 0);
                    if (!res.contains("locations") || !res["locations"].is_array()) return;
                    for (const auto& L : res["locations"]) {
                        const auto& P = L["position"];
                        FrontDoor fd{glm::ivec3(static_cast<int>(std::floor(P.value("x", 0.0f))),
                                                static_cast<int>(std::floor(P.value("y", 0.0f))),
                                                static_cast<int>(std::floor(P.value("z", 0.0f)))),
                                     glm::ivec4(x, z, w, d),
                                     res.value("floor_top_micro", -1),
                                     prog.value("typology", std::string("?")),
                                     res.value("rooms", nlohmann::json::array())};
                        if (L.contains("door") && L["door"].is_object()) {   // M5b-1: the forge's door
                            const auto& D = L["door"];
                            fd.hasDoor = true;
                            fd.door = {D.value("x", 0), D.value("z", 0)};
                            fd.normal = {D.value("nx", 0), D.value("nz", 0)};
                        }
                        fd.padY = prog.value("pad_y", prog["position"].value("y", INT_MIN));
                        if (fd.padY != INT_MIN && fd.thresholdMicro >= 0 && prog.contains("pad_y"))
                            (*sharedPadOffset)[prog.value("style", std::string())] = fd.thresholdMicro - fd.padY * 9;
                        sharedFrontDoors->push_back(fd);
                        break;   // one front door per building
                    }
                };
                seat(bp, bw, bd);
                const auto res1 = Core::StructureBuildService::buildV2(bp, deps);
                if (!res1.contains("error")) { ++*sharedBuilt; recordDoor(res1, bp, bw, bd); return; }
                LOG_WARN_FMT("Settlement", "lot " << lotIndex << " (" << typ1 << ") refused: "
                             << res1["error"].dump() << " — re-rolling the variant");
                seat(bp2, bw2, bd2);
                const auto res2 = Core::StructureBuildService::buildV2(bp2, deps);
                if (!res2.contains("error")) { ++*sharedBuilt; recordDoor(res2, bp2, bw2, bd2); return; }
                LOG_WARN_FMT("Settlement", "lot " << lotIndex << " re-roll (" << typ2
                             << ") ALSO refused: " << res2["error"].dump()
                             << " — lot left EMPTY (recorded)");
                if (lotFailures)
                    lotFailures->push_back({{"lot", lotIndex}, {"typology", typ1},
                                            {"retry_typology", typ2},
                                            {"error", res1.value("error", std::string())},
                                            {"retry_error", res2.value("error", std::string())}});
            }});
            queued.push_back({{"plot", b.plotIndex}, {"period", era}, {"position", {{"x", bx}, {"y", by}, {"z", bz}}},
                              {"footprint", nlohmann::json::array({bw, bd})},
                              {"typology", var.typology}, {"style", var.style}, {"shape", var.footprintShape}});
            doorCenters.push_back(glm::ivec3(bx + bw / 2, by, bz + bd / 2));  // path anchor
            bFoot.push_back(glm::ivec4(bx, bz, bw, bd));                      // footprint for path masking (V7)
            lotToPlot.push_back(b.plotIndex);
        }
        LOG_INFO_FMT("Settlement", "build_settlement: " << layout.plots.size() << " plots, "
                     << buildings.size() << " buildings queued (" << layout.streets.size() << " streets)");

        // CIRCULATION: main-street settlements pave the STREET NETWORK as real geometry (StreetPaver
        // #39 slice 2 — full-width graded streets + a spur per front door, tier material, CUT columns
        // honored); cluster/legacy terrain settlements keep the door-to-door MST ribbon (3c).
        nlohmann::json pathsJson = nlohmann::json::object();
        const Core::AgentBox abox;  // defaults match the engine character (step-up 4 micro)
        // micro <-> cube helpers + the microcube stamper, shared by street paving, the MST ribbon,
        // and the fences. Walkable surface micro = top FACE of the highest solid cube = (cube+1)*9.
        // NOTE: all helpers below capture BY VALUE — they are copied into work units that
        // outlive this handler's stack frame.
        auto fl9 = [](int v) { return v >= 0 ? v / 9 : -((-v + 8) / 9); };
        auto rem9 = [](int v) { int r = v % 9; return r < 0 ? r + 9 : r; };
        auto surfMicro = [terrainTopAt](int wx, int wz) { return (terrainTopAt(wx, wz) + 1) * 9; };
        auto groundMicro = [surfMicro, fl9](int mx, int mz) { return surfMicro(fl9(mx), fl9(mz)); };
        // [no-frozen-engine] BULK micro emit: collect placements into a StructureResult and let
        // StructureGenerator::place() write them (direct chunk writes, bulk-deferred collision,
        // dirty-marked remesh) instead of the per-voxel addMicrocubeWithMaterial wrapper — the
        // wrapper made the city's 268k-column paving take 181 s on the main thread.
        auto emitMicro = [fl9, rem9](Core::StructureResult& out, int mx, int my, int mz,
                             const std::string& mat) {
            Core::VoxelPlacement vp;
            vp.level = Core::VoxelLevel::Microcube;
            vp.position = glm::ivec3(fl9(mx), fl9(my), fl9(mz));
            const int rx = rem9(mx), ry = rem9(my), rz = rem9(mz);
            vp.subcubePos = glm::ivec3(rx / 3, ry / 3, rz / 3);
            vp.microcubePos = glm::ivec3(rx % 3, ry % 3, rz % 3);
            vp.material = mat;
            out.voxels.push_back(vp);
        };
        std::set<std::pair<int, int>> pavedCols;   // CUBE columns actually PAVED — fences gap here (V1)

        if ((mainStreetMode || mapLots) && chunkManager) {
            if (mapLots) {
            // M5b-1 (the design check's item 3a): ONE paver for every map way at its width - the hamlet's paths and
            // the street tiers' routed primary and lanes alike (Core::planWayPaving: each leg graded along its centre
            // line, LEVEL across the width, a capsule round diagonal legs); the same applier as the legacy street
            // unit, so the door spurs, the sweep and the fences' straddle rule read the same shared columns.
            units.push_back({"grading + paving ways",
                [chunkManager, placedObjectManager, objectTemplateManager, locationRegistry, npcManager, pushUndo, tierStreetMat = tierP->street.material, mapRibbons, mapFoot,
                 groundMicro, groundTopAt, fl9, emitMicro, abox, pathsJsonP, sharedPaved, sharedRoadBand, sharedStamp]() {
            if (!chunkManager) return;
            auto& pathsJson = *pathsJsonP;
            auto& pavedCols = *sharedPaved;
            Core::PavingPlan pave = Core::planWayPaving(mapRibbons, groundMicro, abox, tierStreetMat);
            // M5b-1 (chain 114): a way's capsule edge can poke into a setback-0 lot's footprint; the forge builds over the
            // stamped micros and those above its floor survive as a spike in the doorway (the town's tavern @13573:
            // columns at 1449 against a floor of 1443). No column inside a lot's footprint is stamped.
            {
                const size_t before = pave.columns.size();
                pave.columns.erase(std::remove_if(pave.columns.begin(), pave.columns.end(), [&](const Core::PavedColumn& c) {
                    const int cbx = fl9(c.x), cbz = fl9(c.z);
                    for (const auto& f : mapFoot)
                        if (cbx >= f.x && cbx <= f.x + f.z - 1 && cbz >= f.y && cbz <= f.y + f.w - 1) return true;
                    return false;
                }), pave.columns.end());
                if (pave.columns.size() != before)
                    LOG_INFO_FMT("Settlement", "ways: " << (before - pave.columns.size()) << " paved columns inside lot footprints dropped");
            }
            PavingApply ap;
            if (!pave.columns.empty())
                ap = applyPavingPlan(chunkManager, pave, groundMicro, groundTopAt, fl9, emitMicro,
                                     pavedCols, *sharedRoadBand, *sharedStamp, tierStreetMat);
            long legs = 0;
            for (const auto& r : mapRibbons) legs += static_cast<long>(r.cells.size()) - 1;
            LOG_INFO_FMT("Settlement", "ways: paved " << ap.placedMicros << " micros over " << pave.columns.size()
                         << " columns (" << pave.levelCols << " level, " << pave.fillCols << " fill, " << pave.cutCols
                         << " cut honored, " << ap.cutCubes << " cubes removed, " << ap.bandCleared
                         << " corridor cells band-cleared) for " << mapRibbons.size() << " ways / " << legs << " legs, "
                         << tierStreetMat << ", " << static_cast<long>(ap.stampMs) << " ms");
            pathsJson = {{"ways", static_cast<long>(mapRibbons.size())}, {"legs", legs},
                         {"paved_columns", static_cast<long>(pave.columns.size())},
                         {"paved_microcubes", ap.placedMicros},
                         {"level_cols", pave.levelCols}, {"fill_cols", pave.fillCols},
                         {"cut_cols_honored", pave.cutCols}, {"cut_cubes_removed", ap.cutCubes},
                         {"grass_converted", ap.grassConverted},
                         {"material", tierStreetMat}, {"stamp_ms", static_cast<long>(ap.stampMs)}};
            }});
            } else {
            // STREET PAVING (Phase 2), as ONE work unit: grade + pave the full street width in the
            // tier's material, plus a spur from every building's front-wall midpoint. CUT columns
            // honored (terrain removed above the graded surface, then capped).
            units.push_back({"grading + paving streets",
                [chunkManager, placedObjectManager, objectTemplateManager, locationRegistry, npcManager, pushUndo, tierStreetMat = tierP->street.material, streets = layout.streets,
                 bFoot, ox, oz, groundMicro, groundTopAt, isFloraMat, fl9, emitMicro, abox,
                 pathsJsonP, sharedPaved, sharedRoadBand, sharedStamp]() {
            if (!chunkManager) return;
            auto& pathsJson = *pathsJsonP;
            auto& pavedCols = *sharedPaved;
            const std::string paveMat = tierStreetMat;
            // M0: NO spurs here. The spurs run in the "door spurs" unit AFTER the buildings,
            // from each REALIZED front door (a refused lot records no door -> no spur).
            const std::vector<Core::DoorAnchor> doors;
            const Core::PavingPlan pave = Core::planStreetPaving(
                streets, glm::ivec2(ox, oz), doors, bFoot, groundMicro, abox, paveMat);
            const PavingApply ap = applyPavingPlan(chunkManager, pave, groundMicro, groundTopAt, fl9, emitMicro,
                                                   pavedCols, *sharedRoadBand, *sharedStamp, paveMat);
            const long placedMicros = ap.placedMicros;
            const long grassConverted = ap.grassConverted;
            const long stampMs = static_cast<long>(ap.stampMs);
            const int bandCleared = ap.bandCleared;
            LOG_INFO_FMT("Settlement", "streets: paved " << placedMicros << " micros over "
                         << pave.columns.size() << " columns (" << pave.levelCols << " level, "
                         << pave.fillCols << " fill, " << pave.cutCols << " cut honored, "
                         << ap.cutCubes << " cubes removed, " << bandCleared
                         << " corridor cells band-cleared), spurs " << pave.spursPlanned
                         << " ok / " << pave.spursFailed << " too steep, " << paveMat << ", "
                         << stampMs << " ms");
            pathsJson = {{"paved_columns", static_cast<long>(pave.columns.size())},
                         {"paved_microcubes", placedMicros},
                         {"level_cols", pave.levelCols}, {"fill_cols", pave.fillCols},
                         {"cut_cols_honored", pave.cutCols},
                         {"cut_cubes_removed", ap.cutCubes},
                         {"cut_cells_unpaved", 0},
                         {"grass_converted", grassConverted},
                         {"spurs", pave.spursPlanned}, {"spurs_too_steep", pave.spursFailed},
                         {"material", paveMat}, {"stamp_ms", stampMs}};
            }});
            }   // the legacy street rects (a build without a map)
            // M0: DOOR SPURS, after the buildings. Each spur runs from the REALIZED door's
            // marker (recorded by the building unit) to the nearest stamped street column,
            // meeting the street at its planned surface; a refused lot has no door -> no
            // spur to nothing. Same applier as the streets (the sweep re-stamps both).
            postBuildUnits.push_back({"door spurs",
                [chunkManager, placedObjectManager, objectTemplateManager, locationRegistry, npcManager, pushUndo,
                 tierStreetMat = tierP->street.material, bFoot, groundMicro, groundTopAt, fl9, emitMicro, abox,
                 pathsJsonP, sharedPaved, sharedWayPaved, sharedRoadBand, sharedStamp, sharedFrontDoors]() {
            if (!chunkManager) return;
            *sharedWayPaved = *sharedPaved;   // M5b-1 (chain 107): the ways' paving before any spur is laid
            // M0 row 2b: the spur starts one cell OUTSIDE THE DOOR (doorApproach - the gate's
            // door-finding), at the live ground there; not at the forge's marker, 2 cells out
            // and often on the street's edge, which left the straight run one cell short of
            // the pad-to-street drop (M0 L4 town: 3 of 5 spurs "too steep"; F7: 0 of 8).
            std::vector<Core::DoorAnchor> doors;
            for (const auto& fd : *sharedFrontDoors) {
                const auto da = fd.hasDoor ? SettlementBuildService::doorApproachAt(fd.door, fd.normal)   // M5b-1: the forge's door
                                           : settlementDoorApproach(chunkManager, abox, fd.marker, fd.foot, fd.thresholdMicro);
                const int mx = da.outside.x * 9 + 4, mz = da.outside.y * 9 + 4;
                // M5b-1 (chain 107): the spur starts at the door's THRESHOLD (its surface = the standing level less one),
                // not at the ground the pad left behind - the pad is seated at the way, the ground outside is not
                doors.push_back({mx, mz, fd.thresholdMicro - 1});
            }
            const std::vector<glm::ivec4> streetStamp = *sharedStamp;   // copy: the applier appends
            const Core::PavingPlan pave = Core::planDoorSpurs(doors, bFoot, streetStamp, groundMicro,
                                                              abox, tierStreetMat);
            for (const auto& f : pave.spurFailures) LOG_INFO_FMT("Settlement", "door spur too steep: " << f);
            auto& pavedCols = *sharedPaved;
            PavingApply ap;
            if (!pave.columns.empty())
                ap = applyPavingPlan(chunkManager, pave, groundMicro, groundTopAt, fl9, emitMicro,
                                     pavedCols, *sharedRoadBand, *sharedStamp, tierStreetMat);
            LOG_INFO_FMT("Settlement", "door spurs: " << pave.spursPlanned << " planned from "
                         << doors.size() << " realized doors, " << pave.spursFailed << " too steep, "
                         << ap.placedMicros << " micros over " << pave.columns.size() << " columns, "
                         << static_cast<long>(ap.stampMs) << " ms");
            for (const auto& f : pave.spurFailures)
                LOG_WARN_FMT("Settlement", "door spur too steep: " << f);
            (*pathsJsonP)["spurs"] = pave.spursPlanned;
            (*pathsJsonP)["spurs_too_steep"] = pave.spursFailed;
            (*pathsJsonP)["spur_failures"] = pave.spurFailures;
            (*pathsJsonP)["spur_columns"] = static_cast<long>(pave.columns.size());
            (*pathsJsonP)["spur_doors"] = static_cast<long>(doors.size());
            }});
        } else if (terrain && chunkManager && (doorCenters.size() >= 2 || !mapWayCells.empty())) {
            // MST door-to-door path network (cluster/legacy morphology), as ONE work unit.
            postBuildUnits.push_back({"grading + paving paths",
                [mapWayCells, chunkManager, placedObjectManager, objectTemplateManager, locationRegistry, npcManager, pushUndo, sharedFrontDoors, bFoot, ox, oy, groundMicro, surfMicro, fl9, emitMicro, abox, pathMat,
                 pathsJsonP, sharedPaved]() {
            if (!chunkManager) return;
            auto& pathsJson = *pathsJsonP;
            auto& pavedCols = *sharedPaved;
            const std::string paveMat = pathMat;   // M0: the tier's street material, not a literal
            // M0: the REALIZED doors (a refused lot records none); the network joins doors, not
            // footprint centres.
            std::vector<Core::DoorAnchor> doors;
            for (const auto& fd : *sharedFrontDoors) {   // M0 row 2b: outside the FOUND door
                const auto da = fd.hasDoor ? SettlementBuildService::doorApproachAt(fd.door, fd.normal)   // M5b-1: the forge's door
                                           : settlementDoorApproach(chunkManager, abox, fd.marker, fd.foot, fd.thresholdMicro);
                doors.push_back({da.outside.x * 9 + 4, da.outside.y * 9 + 4,
                                 (settlementTopScan(chunkManager, oy, da.outside.x, da.outside.y, true) + 1) * 9});
            }
            if (doors.size() < 2 && mapWayCells.empty()) {
                LOG_INFO_FMT("Settlement", "paths: " << doors.size() << " realized door(s) - no network to grade");
                pathsJson = {{"edges", 0}, {"connected", 0}, {"doors", static_cast<long>(doors.size())}};
                return;
            }
            // M5a: the MAP's ways are what is paved (a terrain path per node pair + a spur per realized
            // door); the door-to-door MST stays for a plan without a map (the legacy path)
            const Core::SettlementPaths net = mapWayCells.empty()
                ? Core::planSettlementPaths(doors, groundMicro, abox)
                : Core::pathsForMapWays(mapWayCells, doors, groundMicro, abox);
            LOG_INFO_FMT("Settlement", "paths: " << net.connected << "/" << net.edges
                         << " edges graded, " << net.failedEdges.size() << " too steep");

            // STAMP the graded path surfaces as voxels (sub-slice 2): a Cobblestone paving ribbon carved
            // PERPENDICULAR to travel (±halfWidth). HONEST SCOPE — this stamps ONLY the FILL portion
            // (where the graded surface S rises ABOVE the terrain); CUT cells stay unpaved on the MST
            // ribbon (main-street settlements route through StreetPaver above, which honors cuts).
            // READ PASS: collect the unique corridor cells (perpendicular carve, deduped) with their target
            // surface S and the ORIGINAL terrain — BEFORE any stamping, so the paving we add doesn't raise
            // the terrain we measure against for later cells.
            const int hw = abox.halfWidthMicro;
            std::unordered_map<long long, std::pair<int, int>> col;  // key(cx,cz) -> (S, originalTerr)
            auto colKey = [](int x, int z) { return (static_cast<long long>(x) << 32) ^ (z & 0xffffffffLL); };
            for (const auto& p : net.paths) {
                const auto& cs = p.cells;
                for (size_t i = 0; i < cs.size(); ++i) {
                    bool tX = false, tZ = false;       // travel direction (carve perpendicular to it)
                    if (i + 1 < cs.size()) { tX |= cs[i + 1].x != cs[i].x; tZ |= cs[i + 1].z != cs[i].z; }
                    if (i > 0)             { tX |= cs[i].x != cs[i - 1].x; tZ |= cs[i].z != cs[i - 1].z; }
                    if (!tX && !tZ) tX = true;
                    const int S = cs[i].surfaceY;
                    for (int o = -hw; o <= hw; ++o) {
                        const int cx = cs[i].x + (tZ ? o : 0), cz = cs[i].z + (tX ? o : 0);
                        col.emplace(colKey(cx, cz), std::make_pair(S, surfMicro(fl9(cx), fl9(cz))));
                    }
                }
            }
            // WRITE PASS: pave each cell. FILL/LEVEL (S>=terr) place a Cobblestone ribbon from the terrain
            // up to S — the top microcube sits in open air (no terrain cube to subdivide), so it is cheap.
            long placedFill = 0, levelPaved = 0, cut = 0;
            // V7: a path must route AROUND buildings — never pave the footprint INTERIOR (inset 1 from the
            // walls so a path meeting the door at the perimeter is fine, matching the V7 detector).
            auto insideFootprint = [&](int cbx, int cbz) {
                for (const auto& f : bFoot)
                    if (cbx >= f.x + 1 && cbx <= f.x + f.z - 2 && cbz >= f.y + 1 && cbz <= f.y + f.w - 2)
                        return true;
                return false;
            };
            const auto t0 = std::chrono::steady_clock::now();
            Core::StructureResult levelBatch, fillBatch;   // bulk emit (see emitMicro note)
            for (const auto& kv : col) {
                const int cx = static_cast<int>(kv.first >> 32), cz = static_cast<int>(static_cast<int32_t>(kv.first & 0xffffffffLL));
                const int ccx = fl9(cx), ccz = fl9(cz);
                if (insideFootprint(ccx, ccz)) continue;   // V7: don't pave under a building
                const int S = kv.second.first, terr = kv.second.second;
                if (S >= terr) {
                    pavedCols.insert({ccx, ccz});          // V1: only REALLY-paved columns gap the fence (not cut cells)
                    for (int my = terr; my <= S; ++my)
                        emitMicro(S == terr ? levelBatch : fillBatch, cx, my, cz, paveMat);
                } else ++cut;  // S below terrain -> owed: removeMicrocube above S (36b)
            }
            levelPaved = Core::StructureGenerator::place(chunkManager, levelBatch).placed;
            placedFill = Core::StructureGenerator::place(chunkManager, fillBatch).placed;
            const double stampMs = std::chrono::duration<double, std::milli>(
                                       std::chrono::steady_clock::now() - t0).count();
            LOG_INFO_FMT("Settlement", "paths: paved " << (placedFill + levelPaved) << " microcubes ("
                         << levelPaved << " level caps, " << placedFill << " fill), " << cut
                         << " cut cells owed; stamp " << static_cast<long>(stampMs) << " ms");
            pathsJson = {{"edges", net.edges}, {"connected", net.connected},
                         {"too_steep", static_cast<int>(net.failedEdges.size())},
                         {"paved_microcubes", placedFill + levelPaved}, {"level_caps", levelPaved},
                         {"fill_microcubes", placedFill}, {"cut_cells_unpaved", cut},
                         {"stamp_ms", static_cast<long>(stampMs)}};
            }});
        }

        // FENCES + YARDS (#39, GROUNDED), as ONE work unit: a THIN typed fence along each parcel
        // edge (canon picket dims), gate onto the parcel's street (main-street) or toward the
        // settlement centre (scatter). Runs for terrain scatter AND any main-street settlement.
        if ((terrain || mainStreetMode) && chunkManager && !layout.plots.empty()) {
            postBuildUnits.push_back({"fencing parcels",
                [mapLots, chunkManager, placedObjectManager, objectTemplateManager, locationRegistry, npcManager, pushUndo, layout, msl, doorCenters, ox, oz, terrainTopAt, mainStreetMode,
                 fl9, rem9, emitMicro, pathsJsonP, sharedPaved, seed, lotFailuresP = res.lotFailures, lotToPlot, fenceMat,
                 fenceFraction = (programMode && tierP ? tierP->fenceFraction : 1.0),
                 coreRing = (programMode && tierP ? tierP->coreRing : 0)]() {
            if (!chunkManager) return;
            auto& pathsJson = *pathsJsonP;
            auto& pavedCols = *sharedPaved;
            Core::DimensionCanonRegistry fenceCanon;
            fenceCanon.loadFromFile("resources/object_dimensions.json");
            const Core::FenceType fenceType = Core::FenceType::Picket;
            const Core::ArchetypeDims* fd = fenceCanon.get(Core::fenceArchetype(fenceType));
            const int fH  = fd ? std::max(1, (int)std::lround(fd->value("height") * 9.0)) : 8;       // ~0.9 m
            const int fSp = fd ? std::max(1, (int)std::lround(fd->value("post_spacing") * 9.0)) : 16; // ~1.8 m
            const int fRails = fd ? (int)fd->value("rails") : 2;
            const int gateW = 2;  // cubes
            double scx = 0, scz = 0;
            for (const auto& d : doorCenters) { scx += d.x; scz += d.z; }
            if (!doorCenters.empty()) { scx /= doorCenters.size(); scz /= doorCenters.size(); }
            std::unordered_map<long long, int> gtMemo;   // memoise terrain top per cube column (groundTopAt is a scan)
            auto gtAt = [&](int cx, int cz) -> int {
                const long long k = (static_cast<long long>(cx) << 32) ^ (cz & 0xffffffffLL);
                auto it = gtMemo.find(k);
                if (it != gtMemo.end()) return it->second;
                const int v = terrainTopAt(cx, cz); gtMemo[k] = v; return v;
            };
            // Fence POLICY (CityForgePlan M3): core ring unfenced, flush buildings unfenced,
            // seeded fraction of the rest — shouldFencePlot owns the rules.
            Core::FencePolicy fencePol;
            fencePol.fraction = fenceFraction;
            if (mainStreetMode && msl.hasSquare && coreRing > 0) {
                fencePol.hasCore = true;
                fencePol.coreRing = coreRing;
                fencePol.coreCu = msl.marketSquare.x + msl.marketSquare.w / 2;
                fencePol.coreCv = msl.marketSquare.z + msl.marketSquare.d / 2;
            }
            Core::StructureResult fenceBatch;   // bulk emit — one place() for ALL parcels
            long fenceMicros = 0; int parcels = 0, unfenced = 0, pinchedRuns = 0, skippedFailed = 0;
            const std::set<int> failedPlotSet = SettlementBuildService::failedPlots(*lotFailuresP, lotToPlot);
            for (size_t pi = 0; pi < layout.plots.size(); ++pi) {
                const auto& pl = layout.plots[pi];
                if (failedPlotSet.count(static_cast<int>(pi))) { ++skippedFailed; continue; }   // M0: a refused lot gets no fence
                const Core::Rect& pr = pl.rect;
                if (pr.w < 2 || pr.d < 2) continue;
                // Gate side: a main-street parcel opens onto ITS street (the burgage frontage);
                // a scatter parcel faces the settlement centroid / path network.
                char gate;
                int doorRunMicroX = -1, doorRunMicroZ = -1;   // front-door midpoint (local micro)
                if ((mainStreetMode || mapLots) && pi < msl.assigned.size()) {
                    const auto& ap = msl.assigned[pi];
                    gate = ap.streetSide;   // M5a: a map lot's gate opens onto ITS way (the side the path arrives on)
                    if (!Core::shouldFencePlot(static_cast<int>(pi), seed, pr, ap.footprint,
                                               fencePol)) { ++unfenced; continue; }
                    // The gate must land on the FRONT DOOR: the paver's spur anchor is the
                    // footprint's front-wall midpoint — project it onto the gate side's run.
                    doorRunMicroX = (ap.footprint.x + ap.footprint.w / 2) * 9 + 4;
                    doorRunMicroZ = (ap.footprint.z + ap.footprint.d / 2) * 9 + 4;
                } else {
                    const double pcx = ox + pr.x + pr.w / 2.0, pcz = oz + pr.z + pr.d / 2.0;
                    gate = (std::abs(scx - pcx) > std::abs(scz - pcz)) ? (scx > pcx ? 'E' : 'W')
                                                                       : (scz > pcz ? 'N' : 'S');
                }
                ++parcels;
                // stamp one parcel edge: run along an axis at a fixed boundary cube row; leave the gate gap.
                const int NDX[4] = {1, -1, 0, 0}, NDZ[4] = {0, 0, 1, -1};
                // KI-5f: runs are MICRO-precise (FenceRun) so all four ends land exactly
                // on the corner-plane intersections — cube-granular spans made the N/E
                // planes miss the perpendicular fence by up to 8 micro (ragged corners).
                auto stampEdge = [&](const Core::FenceRun& run) {
                    const int runLenMicro = run.toMicro - run.fromMicro;
                    if (runLenMicro <= 0) return;
                    const Core::FenceProfile prof = Core::planFenceProfile(
                        runLenMicro, fH, fSp, fRails, fenceType, 1, run.cornerPosts);
                    if (!prof.ok) return;
                    int gLo = -1, gHi = -1;
                    if (run.side == gate) {
                        // Cube-aligned gate window. With a known front door (main-street
                        // plots) the gate TRACKS the door's run coordinate (M3 — user find:
                        // gates didn't match the entrance path); otherwise the legacy
                        // centred window (its centering nuance is auditor-pinned).
                        const int pref = run.alongX ? doorRunMicroX - run.fromMicro
                                                    : doorRunMicroZ - run.fromMicro;
                        if (doorRunMicroX >= 0)
                            Core::fenceGateWindowAt(runLenMicro, gateW, pref, gLo, gHi);
                        else
                            Core::fenceGateWindow(runLenMicro, gateW, gLo, gHi);
                    }
                    for (const auto& c : prof.cells) {
                        if (c.u >= gLo && c.u < gHi) continue;                 // gate opening
                        const int wx = run.alongX ? ox * 9 + run.fromMicro + c.u
                                                  : ox * 9 + run.fixedMicro + c.w;
                        const int wz = run.alongX ? oz * 9 + run.fixedMicro + c.w
                                                  : oz * 9 + run.fromMicro + c.u;
                        const int ccx = fl9(wx), ccz = fl9(wz);
                        if (pavedCols.count({ccx, ccz})) continue;            // V1: gap where a path crosses (gate straddle)
                        const int g0 = gtAt(ccx, ccz);
                        bool cliff = false;                                    // V2: don't fence along a >=2-cube terrain step
                        for (int k = 0; k < 4; ++k)
                            if (std::abs(gtAt(ccx + NDX[k], ccz + NDZ[k]) - g0) >= 2) { cliff = true; break; }
                        if (cliff) continue;
                        const int base = (g0 + 1) * 9;                         // top face of terrain
                        emitMicro(fenceBatch, wx, base + c.y, wz, fenceMat);   // M0: program material
                    }
                };
                // KI-5f: composition from planParcelFenceRuns — every run ends exactly on
                // the corner intersections; one post per corner (endPosts mechanism),
                // perpendicular rails reach it (duplicate cells at the shared corner
                // column are deduped by place()).
                // Ravenmere G-78: a plane that would leave less than the settlement's built
                // corridor minimum of clear width against a NEIGHBOUR's wall is dropped (the
                // side stays open). The minimum is SettlementWalkability's
                // kMinCorridorWidthCubes (2 cubes: the agent box needs 5 micro, boundaries are
                // planned per cube while fences/walls eat micro rows off both faces, and it is
                // the fence stamper's own gate width) - the measured 1.0 m alley with a proud
                // corner quoin (1/3 cube) left 0.11 m of slack.
                constexpr int kMinAlleyCells = Core::kMinCorridorWidthCubes;
                std::vector<Core::CubeRect> nbs;
                if (mainStreetMode)
                    for (size_t j = 0; j < msl.assigned.size(); ++j) {
                        if (j == pi) continue;
                        const auto& f = msl.assigned[j].footprint;
                        nbs.push_back({f.x, f.z, f.w, f.d});
                    }
                auto runs = Core::planParcelFenceRuns(pr.x, pr.z, pr.w, pr.d);
                bool droppedSide[4] = {false, false, false, false};   // W, E, S, N
                auto sideIdx = [](char c) { return c == 'W' ? 0 : c == 'E' ? 1 : c == 'S' ? 2 : 3; };
                for (const auto& run : runs)
                    if (Core::fenceRunPinchesNeighbour(run, pr.x, pr.z, pr.w, pr.d, nbs, kMinAlleyCells)) {
                        droppedSide[sideIdx(run.side)] = true;
                        ++pinchedRuns;
                    }
                for (auto run : runs) {
                    if (droppedSide[sideIdx(run.side)]) continue;
                    // A surviving run must not plant its corner post on the dropped plane's
                    // pinch line either: pull it back by the alley width.
                    if (!Core::trimFenceRunAtDroppedCorners(run, droppedSide[0], droppedSide[1],
                                                            droppedSide[2], droppedSide[3], kMinAlleyCells))
                        continue;
                    stampEdge(run);
                }
            }
            fenceMicros = Core::StructureGenerator::place(chunkManager, fenceBatch).placed;
            LOG_INFO_FMT("Settlement", "fences: " << parcels << " parcels fenced, " << unfenced
                         << " unfenced by policy, " << pinchedRuns << " runs dropped (alley < 2 cells to a neighbour wall), "
                         << fenceMicros << " micros (picket, " << fH << "-micro tall, posts @" << fSp << ")");
            pathsJson["fence_runs_dropped_for_alley"] = pinchedRuns;
            pathsJson["parcels"] = parcels;
            pathsJson["unfenced_by_policy"] = unfenced;
            pathsJson["fence_skipped_failed_lots"] = skippedFailed;
            pathsJson["fence_micros"] = fenceMicros;
            pathsJson["fence_type"] = Core::fenceTypeToString(fenceType);
            }});
        }

        // YARD PROPS + the shared WELL (#29/#25 minimum slice), as ONE work unit: a woodpile
        // behind each house + a kitchen-garden bed in the open toft (planYardProps); tier
        // `public.well` anchors the market square (town) or the street verge (village).
        // Skips are COUNTED, never silent.
        if (mainStreetMode && placedObjectManager && objectTemplateManager && chunkManager) {
            postBuildUnits.push_back({"yard props + well",
                [chunkManager, placedObjectManager, objectTemplateManager, locationRegistry, npcManager, pushUndo, msl, ox, oz, seed, terrainTopAt, pubSpec = tierP->pub, propsJsonP,
                 lotFailuresP = res.lotFailures, lotToPlot, mapWell, mapWellCell, sharedPropBoxes]() {
            if (!chunkManager || !placedObjectManager || !objectTemplateManager) return;
            int propsPlaced = 0, propsSkipped = 0;
            auto spawnProp = [&](const std::string& type, int cx, int cz, int rot) -> bool {
                const std::string tmpl = Core::FurnitureCatalog::templateFor(type);
                if (tmpl.empty() || !objectTemplateManager->getTemplate(tmpl)) return false;
                const int gy = terrainTopAt(cx, cz) + 1;             // stand on the ground/paving
                const std::string id = placedObjectManager->placeTemplateMicro(tmpl, glm::ivec3(cx * 9, gy * 9, cz * 9), rot, "");
                if (id.empty()) return false;
                if (const Core::PlacedObject* po = placedObjectManager->get(id))
                    sharedPropBoxes->push_back({po->boundingMin, po->boundingMax});   // O9: the sweep keeps these cells
                return true;
            };
            const std::set<int> failedPlotSet = SettlementBuildService::failedPlots(*lotFailuresP, lotToPlot);
            int propsSkippedFailedLots = 0;
            for (size_t pi = 0; pi < msl.assigned.size(); ++pi) {
                if (failedPlotSet.count(static_cast<int>(pi))) { ++propsSkippedFailedLots; continue; }   // M0
                const auto& ap = msl.assigned[pi];
                for (const auto& yp : Core::planYardProps(ap, seed))
                    (spawnProp(yp.type, ox + yp.cx, oz + yp.cz, yp.rotDeg) ? ++propsPlaced
                                                                           : ++propsSkipped);
            }
            // Square features (well/statue/stalls) are NOT placed here: the square is part of
            // the swept road band, and the late "street sweep" unit clears whole cells over it
            // — anything dressed onto the square before the sweep is silently erased (found
            // live 2026-08-26: stalls registered but voxel-gone). The dedicated "square
            // dressing" unit AFTER the sweep owns them now.
            if (!msl.hasSquare && pubSpec.well) {     // village: the main street's verge, mid-length
                const Core::Rect& ms = msl.mainStreet;
                const bool msAlongX = ms.w >= ms.d;
                // M5b-1: a map build's primary way is a polyline - the planner's projection names the verge cell
                const int wcx = mapWell ? ox + mapWellCell.x : ox + (msAlongX ? ms.x + ms.w / 2 : ms.x + 1);
                const int wcz = mapWell ? oz + mapWellCell.y : oz + (msAlongX ? ms.z + 1 : ms.z + ms.d / 2);
                (spawnProp("well", wcx, wcz, 0) ? ++propsPlaced : ++propsSkipped);
            }
            LOG_INFO_FMT("Settlement", "yard props: " << propsPlaced << " placed, "
                         << propsSkipped << " skipped");
            (*propsJsonP) = {{"placed", propsPlaced}, {"skipped", propsSkipped},
                             {"skipped_failed_lots", propsSkippedFailedLots}};
            }});
        }

        // Buildings LAST (site prep — terrace/streets/fences/props — must precede them).
        for (auto& bu : buildingUnits) units.push_back(std::move(bu));
        for (auto& pu : postBuildUnits) units.push_back(std::move(pu));   // M0: lot dressing AFTER the lots exist

        // Street sweep AFTER the buildings: construction drops debris on finished
        // roads (site prep undermines a roadside tree -> U4 fells it -> the trunk
        // retires as static voxels across the street). Re-clear the walkable band
        // recorded by the street unit so the road the nav rebuild sees is the road
        // NPCs actually get.
        units.push_back({"street sweep",
            [chunkManager, placedObjectManager, objectTemplateManager, locationRegistry, npcManager, pushUndo, sharedRoadBand, sharedStamp, emitMicro, bFoot, sharedPropBoxes,
             sweepMat = (programMode && tierP ? tierP->street.material
                                             : std::string("Gravel"))]() {
            if (!chunkManager || sharedRoadBand->empty()) return;
            // M5b-1 (chain 103): never sweep a cube inside a BUILDING's footprint (walls included) - a setback-0 lot's
            // doorway cube abuts the way's capsule, counts as a band cube, and was hollowed to the paving's level
            // (the town's hall_house @13514 and blacksmith @13503: the doorway floor 11-12 micro under the threshold).
            // O9: nor a cube inside a prop this build placed before the sweep (the well on the way's verge, the yard
            // props). The selection is Core::planStreetSweep (SettlementBuildServiceTest.SweepNeverErasesPlacedObjectCells).
            const Core::SweepPlan sweepPlan = Core::planStreetSweep(*sharedRoadBand, bFoot, *sharedPropBoxes);
            std::map<Chunk*, std::vector<glm::ivec3>> byChunk;
            const int keptForBuildings = sweepPlan.keptForBuildings;
            for (const auto& wp : sweepPlan.cells) {
                if (Chunk* ch = chunkManager->getChunkAtFast(wp))
                    byChunk[ch].push_back(wp - ch->getWorldOrigin());
            }
            int swept = 0;
            for (auto& [ch, cells] : byChunk) {
                swept += ch->clearCellsBulk(cells);
                chunkManager->markChunkDirty(ch);
            }
            // The sweep clears WHOLE cells, pavement included — re-stamp the paving
            // from the street unit's recorded per-column stamp (idempotent bulk place).
            long restamped = 0;
            if (!sharedStamp->empty()) {
                Core::StructureResult re;
                for (const auto& s : *sharedStamp)
                    for (int my = s.y; my <= s.w; ++my)
                        emitMicro(re, s.x, my, s.z, sweepMat);
                restamped = Core::StructureGenerator::place(chunkManager, re).placed;
            }
            LOG_INFO_FMT("Settlement", "street sweep: " << swept << " cells cleared over "
                         << sharedRoadBand->size() << " band cells (" << keptForBuildings << " inside a building's footprint kept, "
                         << sweepPlan.keptForObjects << " inside one of " << sharedPropBoxes->size() << " placed props' boxes kept), "
                         << restamped << " paving micros re-stamped");
        }});

        // CIRCUIT WALL (place_town_wall #42, CityForgePlan M7). The band sits OUTSIDE the
        // built site, so it cannot land on a plot; every street that reaches the edge gets a
        // gate, and an ungateable street REFUSES the whole circuit rather than walling a road
        // in. Runs after the buildings so nothing later clears it, before the nav rebuild so
        // pathing sees both the wall and its gateways.
        if (chunkManager && programMode && tierP && tierP->walls.enabled) {
            units.push_back({"raising the town wall",
                [chunkManager, placedObjectManager, objectTemplateManager, locationRegistry, npcManager, pushUndo,
                 spec = tierP->walls, layout, buildings, ox, oz, W, D, terrainTopAt, pathsJsonP]() {
            if (!chunkManager) return;
            std::vector<Core::Rect> footprints;
            footprints.reserve(buildings.size());
            for (const auto& b : buildings) footprints.push_back(b.footprint);
            const Core::TownWallPlan wp = Core::planTownWall(
                Core::Rect{0, 0, W, D}, layout.streets, footprints, spec);
            if (!wp.ok) {
                LOG_WARN_FMT("Settlement", "town wall REFUSED: " << wp.refusal);
                (*pathsJsonP)["town_wall"] = {{"built", false}, {"refusal", wp.refusal}};
                return;
            }
            // Gate cells (cube columns left open for the passage) — a set so the run stamp
            // can skip them without caring which gate they belong to.
            std::set<std::pair<int, int>> gateCols;
            for (const auto& g : wp.gates)
                for (int x = g.opening.x; x < g.opening.x1(); ++x)
                    for (int z = g.opening.z; z < g.opening.z1(); ++z)
                        gateCols.insert({x, z});

            const int kGateClearCubes = spec.gateClearCubes;  // headroom under the gate lintel
            Core::StructureResult batch;
            // The wall must OWN its line. place() will not overwrite an occupied cell, so a
            // tree standing on the wall line silently punched a hole in the circuit (found by
            // an A/B of two identical cities: the plans matched cell-for-cell, the WORLD
            // differed by exactly the cells where flora stood). Every wall column is cleared
            // before it is stamped, the same way the street grader clears its corridor.
            std::vector<glm::ivec3> toClear;
            auto column = [&](int lx, int lz, int height, bool crenellate) {
                const int wx = ox + lx, wz = oz + lz;
                const int base = terrainTopAt(wx, wz) + 1;
                const bool isGate = gateCols.count({lx, lz}) > 0;
                for (int y = base; y <= base + height; ++y)
                    toClear.push_back(glm::ivec3(wx, y, wz));
                // A gate column starts ABOVE the passage: the wall bridges over the road.
                const int y0 = isGate ? base + kGateClearCubes : base;
                for (int y = y0; y < base + height; ++y) {
                    Core::VoxelPlacement v;
                    v.position = glm::ivec3(wx, y, wz);
                    v.material = spec.material;
                    batch.voxels.push_back(v);
                }
                // Merlons: every other cell of the OUTER course, one cube proud of the walk.
                if (crenellate && !isGate && ((lx + lz) % 2 == 0)) {
                    Core::VoxelPlacement m;
                    m.position = glm::ivec3(wx, base + height, wz);
                    m.material = spec.material;
                    batch.voxels.push_back(m);
                }
            };

            // The band: stamp each run, crenellating only its OUTERMOST cube course so the
            // inner course stays a clear wall-walk.
            const Core::Rect& o = wp.outerBound;
            for (const auto& r : wp.runs)
                for (int x = r.band.x; x < r.band.x1(); ++x)
                    for (int z = r.band.z; z < r.band.z1(); ++z) {
                        const bool outerCourse =
                            (r.side == 'W' && x == o.x)  || (r.side == 'E' && x == o.x1() - 1) ||
                            (r.side == 'S' && z == o.z)  || (r.side == 'N' && z == o.z1() - 1);
                        column(x, z, spec.heightCubes, spec.crenellations && outerCourse);
                    }
            // Corner towers: taller than the curtain, and topped the way real drums are —
            // a crenellated PARAPET (English/Welsh: Conwy, Caernarfon) or a CONICAL roof
            // (French/German: Carcassonne, the Loire). A bare cylinder is neither, which is
            // what the first pass built.
            const int towerH = spec.heightCubes + spec.towerExtraHeight;
            int towersUsable = 0;
            const bool conical = (spec.towerCap == "conical");
            for (const auto& tw : wp.towers) {
                const auto cells = Core::towerFootprintCells(tw, spec.towerShape);
                std::set<std::pair<int, int>> body;
                for (const auto& c : cells) body.insert({tw.x + c.x, tw.z + c.y});
                // A TOWER, not a drum-shaped pile: hollow shaft, a spiral stair whose treads
                // are subcube plates (3 micro — inside the agent's 4-micro step; a cube stair
                // is scenery), floors to arrive at, a doorway, and arrow loops. Planned by
                // TowerForge and proven climbable by a TraversalProbe in TowerForgeTest.
                Core::TowerSpec ts;
                ts.shape = spec.towerShape;
                ts.heightCubes = towerH;
                ts.storeyCubes = 3;
                ts.arrowLoops = true;
                ts.battlements = spec.crenellations && !conical;
                // Face the doorway INTO the town, so the stair is reached from the streets.
                ts.doorSide = (tw.z <= wp.outerBound.z + 1) ? 'N'
                            : (tw.z1() >= wp.outerBound.z1() - 1) ? 'S'
                            : (tw.x <= wp.outerBound.x + 1) ? 'E' : 'W';
                const Core::TowerPlan tp = Core::planTower(tw, ts);
                if (!tp.ok) {
                    // Honest fallback: a tower we cannot make usable is built solid and SAID
                    // so, rather than shipping a hollow shell nobody can enter.
                    LOG_WARN_FMT("Settlement", "tower at (" << tw.x << "," << tw.z
                                 << ") built solid: " << tp.refusal);
                    for (const auto& [wx, wz] : body) {
                        const bool rimCell = !body.count({wx + 1, wz}) || !body.count({wx - 1, wz}) ||
                                             !body.count({wx, wz + 1}) || !body.count({wx, wz - 1});
                        column(wx, wz, towerH, spec.crenellations && !conical && rimCell);
                    }
                } else {
                    ++towersUsable;
                    // Stamp the plan. Full 9-micro courses go in as CUBES; the 3-micro
                    // treads and floor slabs go in as SUBCUBE layers, which is what makes
                    // the stair climbable at all.
                    auto towerBaseMicro = [&](int lx, int lz) {
                        return (terrainTopAt(ox + lx, oz + lz) + 1) * 9;
                    };
                    auto emitRange = [&](int lx, int lz, int y0m, int y1m) {
                        const int wx = ox + lx, wz = oz + lz;
                        const int baseM = towerBaseMicro(lx, lz);
                        int y = y0m;
                        while (y < y1m) {
                            const int abs = baseM + y;
                            if (abs % 9 == 0 && y + 9 <= y1m) {
                                Core::VoxelPlacement v;
                                v.position = glm::ivec3(wx, abs / 9, wz);
                                v.material = spec.material;
                                batch.voxels.push_back(v);
                                toClear.push_back(v.position);
                                y += 9;
                            } else if (abs % 3 == 0 && y + 3 <= y1m) {
                                const int cubeY = abs / 9, sy = (abs % 9) / 3;
                                for (int sx = 0; sx < 3; ++sx)
                                    for (int sz = 0; sz < 3; ++sz) {
                                        Core::VoxelPlacement v;
                                        v.position = glm::ivec3(wx, cubeY, wz);
                                        v.level = Core::VoxelLevel::Subcube;
                                        v.subcubePos = glm::ivec3(sx, sy, sz);
                                        v.material = spec.material;
                                        batch.voxels.push_back(v);
                                    }
                                toClear.push_back(glm::ivec3(wx, cubeY, wz));
                                y += 3;
                            } else {
                                ++y;
                            }
                        }
                    };
                    for (const auto& w : tp.walls)
                        emitRange(tw.x + w.cx, tw.z + w.cz, w.fromMicroY, w.toMicroY);
                    for (const auto& pl : tp.plates)
                        emitRange(tw.x + pl.cx, tw.z + pl.cz, pl.yMicro,
                                  pl.yMicro + pl.thicknessMicro);
                    // Merlons on the rim top (the fighting deck), parapet form only.
                    if (spec.crenellations && !conical)
                        for (const auto& [wx, wz] : body) {
                            const bool rimCell = !body.count({wx + 1, wz}) || !body.count({wx - 1, wz}) ||
                                                 !body.count({wx, wz + 1}) || !body.count({wx, wz - 1});
                            if (!rimCell || ((wx + wz) % 2)) continue;
                            const int base = terrainTopAt(ox + wx, oz + wz) + 1;
                            Core::VoxelPlacement m;
                            m.position = glm::ivec3(ox + wx, base + towerH, oz + wz);
                            m.material = spec.material;
                            batch.voxels.push_back(m);
                            toClear.push_back(m.position);
                        }
                }
                if (!conical) continue;
                // CONICAL ("pepperpot") ROOF: rings of shrinking radius above the drum. The
                // rise is kRise courses PER inward step, so the cone stands about as tall as
                // it is wide — a one-course-per-step cone is a stubby cap, not a roof (built
                // that first and it read as a stone lid). Slate over the stone drum.
                constexpr int kRise = 2;
                const int cxA = tw.x, czA = tw.z;
                int y = 0;
                for (int lvl = 1;; ++lvl) {
                    Core::Rect shrunk{cxA + lvl, czA + lvl, tw.w - 2 * lvl, tw.d - 2 * lvl};
                    if (shrunk.w <= 0 || shrunk.d <= 0) break;
                    for (int rise = 0; rise < kRise; ++rise, ++y)
                        for (const auto& c : Core::towerFootprintCells(shrunk, spec.towerShape)) {
                            const int wx = ox + shrunk.x + c.x, wz = oz + shrunk.z + c.y;
                            const int base = terrainTopAt(wx, wz) + 1;
                            Core::VoxelPlacement v;
                            v.position = glm::ivec3(wx, base + towerH + y, wz);
                            v.material = "Slate";
                            batch.voxels.push_back(v);
                            toClear.push_back(v.position);
                        }
                }
                // Finial: one course closing the apex so the cone ends in a point, not a hole.
                {
                    const int wx = ox + cxA + tw.w / 2, wz = oz + czA + tw.d / 2;
                    Core::VoxelPlacement v;
                    v.position = glm::ivec3(wx, terrainTopAt(wx, wz) + 1 + towerH + y, wz);
                    v.material = "Slate";
                    batch.voxels.push_back(v);
                    toClear.push_back(v.position);
                }
            }

            // Clear the wall line first (trees, undergrowth, anything standing in it), then
            // stamp — so the circuit is a pure function of the plan, not of what grew there.
            std::map<Chunk*, std::vector<glm::ivec3>> clearByChunk;
            for (const auto& wp2 : toClear)
                if (Chunk* ch = chunkManager->getChunkAtFast(wp2))
                    clearByChunk[ch].push_back(wp2 - ch->getWorldOrigin());
            int cleared = 0;
            for (auto& [ch, cells] : clearByChunk) {
                cleared += ch->clearCellsBulk(cells);
                chunkManager->markChunkDirty(ch);
            }
            const auto placed = Core::StructureGenerator::place(chunkManager, batch);
            LOG_INFO_FMT("Settlement", "town wall: " << placed.placed << " cubes, "
                         << wp.gates.size() << " gates, " << wp.towers.size()
                         << " towers (" << towersUsable << " walkable), line cleared "
                         << cleared << ", displaced " << placed.displaced);
            (*pathsJsonP)["town_wall"] = {{"built", true}, {"cubes", placed.placed},
                                          {"gates", wp.gates.size()},
                                          {"towers", wp.towers.size()}, {"towers_walkable", towersUsable},
                                          {"line_cleared", cleared},
                                          {"displaced", placed.displaced}};
            }});
        }

        // ORPHANED-CANOPY SWEEP (user find 2026-08-27: "leftover foliage and pieces of trees
        // floating in the air"). Every site-prep pass clears flora inside its OWN band —
        // plot boxes, the road corridor, building pads — so a tree whose TRUNK stood in one
        // of them keeps whatever reached outside it, hanging with nothing underneath.
        // planOrphanedFloraSweep finds tree matter that can no longer reach support THROUGH
        // tree matter and returns it for removal; components touching the scan box are left
        // alone (their support may lie outside it — a healthy neighbour's overhang).
        // Runs AFTER every clearer, including the street sweep.
        if (chunkManager) {
            units.push_back({"clearing orphaned canopy",
                [chunkManager, placedObjectManager, objectTemplateManager, locationRegistry, npcManager, pushUndo,
                 ox, oz, W, D, terrainTopAt, pathsJsonP, canopySweepMargin]() {
            if (!chunkManager) return;
            // Vertical band: from just under the lowest ground in the rect to well above the
            // tallest canopy. A tree TALLER than the band touches the box top and is left
            // alone (conservative by design — see FloraSweep.h).
            int gMin = INT_MAX, gMax = INT_MIN;
            for (int x = ox; x <= ox + W; x += 4)
                for (int z = oz; z <= oz + D; z += 4) {
                    const int g = terrainTopAt(x, z);
                    gMin = std::min(gMin, g);
                    gMax = std::max(gMax, g);
                }
            if (gMin == INT_MAX) return;
            // Margin = a canopy radius beyond the settlement rect: site prep clears INSIDE
            // the rect, so an edge tree loses its trunk in-rect while its canopy hangs
            // OUTSIDE it. A 2-cube margin missed exactly that case (measured).
            const int kCanopyMargin = canopySweepMargin;   // M0: 2x the derived parcel margin, >= 8
            Core::SweepBounds bounds;
            bounds.min = glm::ivec3(ox - kCanopyMargin, gMin - 1, oz - kCanopyMargin);
            bounds.max = glm::ivec3(ox + W + kCanopyMargin, gMax + 44, oz + D + kCanopyMargin);
            auto isFlora = [chunkManager](const glm::ivec3& p) {
                return DamageSystem::isTreeMatterCell(chunkManager, p);
            };
            auto isSolid = [chunkManager](const glm::ivec3& p) {
                return chunkManager->hasVoxelAt(p);
            };
            const auto orphans = Core::planOrphanedFloraSweep(bounds, isFlora, isSolid);
            std::map<Chunk*, std::vector<glm::ivec3>> byChunk;
            for (const auto& wp : orphans)
                if (Chunk* ch = chunkManager->getChunkAtFast(wp))
                    byChunk[ch].push_back(wp - ch->getWorldOrigin());
            int cleared = 0;
            for (auto& [ch, cells] : byChunk) {
                cleared += ch->clearCellsBulk(cells);
                chunkManager->markChunkDirty(ch);
            }
            LOG_INFO_FMT("Settlement", "orphaned canopy: " << orphans.size()
                         << " cells found, " << cleared << " cleared");
            (*pathsJsonP)["orphaned_canopy_cleared"] = cleared;
            }});
        }

        // SQUARE DRESSING after the sweep (CityForgePlan M1): statue at the market-cross spot,
        // the tier well (centre, or relocated off the statue), stalls on the corner pads. It
        // MUST run after "street sweep" — the square is inside the swept road band, so dressing
        // placed earlier gets its cells cleared (observed live 2026-08-26: 5 props registered,
        // zero voxels standing). Before "nav rebuild" so the grid sees the props.
        if (programMode && tierP && mainStreetMode && placedObjectManager && objectTemplateManager &&
            chunkManager && msl.hasSquare) {
            units.push_back({"square dressing",
                [chunkManager, placedObjectManager, objectTemplateManager, locationRegistry, npcManager, pushUndo, msl, ox, oz, seed, terrainTopAt, pubSpec = tierP->pub, propsJsonP]() {
            if (!chunkManager || !placedObjectManager || !objectTemplateManager) return;
            int placed = 0, skipped = 0;
            const auto dress = Core::planSquareDressing(msl, pubSpec, seed);
            for (const auto& p : dress.props) {
                const std::string tmpl = Core::FurnitureCatalog::templateFor(p.type);
                bool ok = false;
                if (!tmpl.empty() && objectTemplateManager->getTemplate(tmpl)) {
                    const int cx = ox + p.cx, cz = oz + p.cz;
                    const int gy = terrainTopAt(cx, cz) + 1;   // stand on the plaza paving
                    ok = !placedObjectManager
                              ->placeTemplateMicro(tmpl, glm::ivec3(cx * 9, gy * 9, cz * 9),
                                                   p.rotDeg, "")
                              .empty();
                }
                (ok ? ++placed : ++skipped);
            }
            LOG_INFO_FMT("Settlement", "square dressing: " << placed << " placed, "
                         << skipped << " skipped");
            (*propsJsonP)["square_dressing"] = {{"placed", placed}, {"skipped", skipped}};
            }});
        }

        // DOORSTEPS (Ravenmere G-68): a threshold higher than the character's auto step
        // (4 micro = 0.44 m) is a wall to NPCs and the player alike. The hall's 0.5 m stone
        // slab sat 5 micro above its paved spur. Runs AFTER the street sweep (the swept band
        // covers every paved column incl. spurs and would erase a stoop painted earlier) and
        // before the nav rebuild. Measurement-driven: reads the paving under the doorstep
        // and stoops up to the threshold in <= 4-micro risers, one cell per step outward.
        if (mainStreetMode && chunkManager) {
            units.push_back({"doorsteps",
                [chunkManager, placedObjectManager, objectTemplateManager, locationRegistry, npcManager, pushUndo,
                 sharedFrontDoors, sharedWayPaved, emitMicro, fl9, pathsJsonP,
                 stepMat = (programMode && tierP ? tierP->street.material : std::string("Cobblestone"))]() {
            if (!chunkManager) return;
            constexpr int kStepUpMicro = 4;          // AnimatedVoxelCharacter::m_maxStepHeight = 4/9 m
            constexpr int kMaxSteps = 6;             // M5b-1 chain 98: the face can lie beyond a level apron
            auto solidMicro = [chunkManager](int mx, int my, int mz) {
                return chunkManager->occupiedMicro(glm::ivec3(mx, my, mz));
            };
            // Highest solid micro at a column centre below `fromMicroY` (exclusive), or INT_MIN.
            auto topBelow = [&](int cx, int cz, int fromMicroY) {
                const int mx = cx * 9 + 4, mz = cz * 9 + 4;
                for (int my = fromMicroY - 1; my > fromMicroY - 40; --my)
                    if (solidMicro(mx, my, mz)) return my;
                return INT_MIN;
            };
            // Erase every solid micro cell in a column between two micro heights (inclusive
            // bottom, exclusive top) - the fence slats/rails, a yard prop's spill - so the
            // approach band is AIR above the paving before the stoop is laid. Subcubes are
            // removed whole (nothing walkable is authored as a subcube in the band).
            int approachMicrosCleared = 0;
            auto clearAbove = [&](int cx, int cz, int fromMicro, int toMicro) {
                const glm::ivec3 cube0(cx, 0, cz);
                for (int my = fromMicro; my < toMicro; ++my) {
                    const int cy = fl9(my);
                    const glm::ivec3 cube(cx, cy, cz);
                    Chunk* ch = chunkManager->getChunkAtFast(cube);
                    if (!ch) continue;
                    const glm::ivec3 lp = Utils::CoordinateUtils::worldToLocalCoord(cube);
                    const int ry = my - cy * 9;
                    for (int dx = 0; dx < 9; ++dx)
                        for (int dz = 0; dz < 9; ++dz) {
                            if (!chunkManager->occupiedMicro(glm::ivec3(cx * 9 + dx, my, cz * 9 + dz))) continue;
                            if (chunkManager->getVoxelTypeAt(cube) == VoxelLocation::CUBE) continue;   // a full cube is terrain/wall: not ours
                            const glm::ivec3 sp(dx / 3, ry / 3, dz / 3), mp(dx % 3, ry % 3, dz % 3);
                            if (ch->hasSubcubeAt(lp, sp)) { if (ch->removeSubcube(lp, sp)) approachMicrosCleared += 27; }
                            else if (ch->removeMicrocube(lp, sp, mp)) ++approachMicrosCleared;
                        }
                    chunkManager->markChunkDirty(ch);
                }
                (void)cube0;
            };
            int stooped = 0, steps = 0, skipped = 0;
            Core::StructureResult out;
            for (const auto& fd : *sharedFrontDoors) {
                if (fd.thresholdMicro < 0) { ++skipped; continue; }
                const int fx = fd.foot.x, fz = fd.foot.y, fw = fd.foot.z, fdp = fd.foot.w;
                // The door's wall cube and outward normal: the forge's recorded door (M5b-1); without one,
                // inferred from where the marker sits relative to the footprint.
                int nx = 0, nz = 0, wallX = 0, wallZ = 0;
                if (fd.hasDoor) { nx = fd.normal.x; nz = fd.normal.y; wallX = fd.door.x; wallZ = fd.door.y; }
                else {
                    if (fd.marker.x < fx)            nx = -1;
                    else if (fd.marker.x >= fx + fw) nx = +1;
                    else if (fd.marker.z < fz)       nz = -1;
                    else                             nz = +1;
                    wallX = nx < 0 ? fx : nx > 0 ? fx + fw - 1 : fd.marker.x;
                    wallZ = nz < 0 ? fz : nz > 0 ? fz + fdp - 1 : fd.marker.z;
                }
                const int threshold = fd.thresholdMicro;             // feet micro inside
                // DOOR-APPROACH BAND (G-72): the two cells outside the door must be air over
                // the paving up to the character's height - the parcel fence ran across the
                // hall's doorstep (its gate sat at the front wall's midpoint, the door did not).
                // M5b-1 chain 98: each approach cell's GROUND is the lowest column top over the cell (a fence's slat or
                // post across the approach is not the ground; the centre column alone read the fence's rail as ground
                // and cleared the air above it, leaving the fence), read below the threshold plus a cube (a bump
                // outside a door stands above the threshold and is a step DOWN into the house).
                auto cellGroundTop = [&](int cx, int cz, int fromMicroY) {
                    int lowest = INT_MAX; bool anyCol = false;
                    for (int dx = 0; dx < 9; ++dx)
                        for (int dz = 0; dz < 9; ++dz) {
                            const int mx = cx * 9 + dx, mz = cz * 9 + dz;
                            for (int my = fromMicroY - 1; my > fromMicroY - 40; --my)
                                if (solidMicro(mx, my, mz)) { lowest = std::min(lowest, my); anyCol = true; break; }
                        }
                    return anyCol ? lowest : INT_MIN;
                };
                // chain 116: a PAVED cell's standing level is its paving - the centre column's top (the gate's own
                // convention), the highest column's otherwise; the band covers such a cell only partly at its edge and
                // the lowest column read the natural ground 10 micro under the way (then the approach clearing below
                // wiped the paving down to it at the town's blacksmith and apothecary)
                auto cellPavedTop = [&](int cx, int cz, int fromMicroY) {
                    const int mx = cx * 9 + 4, mz = cz * 9 + 4;
                    for (int my = fromMicroY - 1; my > fromMicroY - 40; --my)
                        if (solidMicro(mx, my, mz)) return my;
                    int highest = INT_MIN;
                    for (int dx = 0; dx < 9; ++dx)
                        for (int dz = 0; dz < 9; ++dz)
                            for (int my = fromMicroY - 1; my > fromMicroY - 40; --my)
                                if (solidMicro(cx * 9 + dx, my, cz * 9 + dz)) { highest = std::max(highest, my); break; }
                    return highest;
                };
                std::vector<int> groundFeet;   // the standing level each cell outward offers
                std::vector<bool> pavedCell;   // a paved column (the way): an anchor the stoop never touches
                for (int k = 1; k <= kMaxSteps; ++k) {
                    const int cx = wallX + nx * k, cz = wallZ + nz * k;
                    const bool paved = sharedWayPaved->count({cx, cz}) > 0;
                    const int g = paved ? cellPavedTop(cx, cz, threshold + 9) : cellGroundTop(cx, cz, threshold + 9);
                    groundFeet.push_back(g == INT_MIN ? INT_MIN : g + 1);
                    pavedCell.push_back(paved);
                }
                // M5b-1 chain 105: the LEVELS - the unpaved cells between the door and the way take the line from the
                // threshold down to the way (<= 4 a cell, never above the threshold: the lintel); a cell above its
                // level is CUT, below it FILLED; the way's cells are anchors (one sill on its edge at most)
                const std::vector<int> fills = Core::planDoorstepLevels(threshold, groundFeet, pavedCell, kStepUpMicro);
                // DOOR-APPROACH BAND (G-72): the two cells outside the door, and every cell that takes a level, must be
                // air over the paving / the step up to the character's height - the parcel fence ran across the
                // hall's doorstep (its gate sat at the front wall's midpoint, the door did not).
                std::vector<glm::ivec3> cutCubes;
                for (int k = 1; k <= kMaxSteps; ++k) {
                    const size_t ki = static_cast<size_t>(k - 1);
                    if (groundFeet[ki] == INT_MIN) continue;
                    const int cx = wallX + nx * k, cz = wallZ + nz * k;
                    const int tgt = fills[ki];
                    if (tgt != INT_MIN && tgt < groundFeet[ki]) {
                        // a CUT: every terrain cube from the one holding the new top up to the found ground goes
                        for (int cy = fl9(tgt - 1); cy <= fl9(groundFeet[ki] - 1); ++cy) cutCubes.push_back(glm::ivec3(cx, cy, cz));
                    }
                    if (k > 2 && tgt == INT_MIN) continue;
                    const int base = (tgt != INT_MIN) ? tgt : groundFeet[ki];
                    clearAbove(cx, cz, std::min(base, groundFeet[ki]), base + 18);
                }
                if (!cutCubes.empty()) Core::StructureGenerator::removeVoxels(chunkManager, cutCubes);
                bool any = false; int cuts = 0;
                for (int k = 1; k <= kMaxSteps; ++k) {
                    const size_t ki = static_cast<size_t>(k - 1);
                    if (groundFeet[ki] == INT_MIN) break;
                    const int stepFeet = fills[ki];
                    if (stepFeet == INT_MIN) continue;
                    const int cx = wallX + nx * k, cz = wallZ + nz * k;
                    if (stepFeet < groundFeet[ki]) ++cuts;
                    const int groundTop = std::min(groundFeet[ki], stepFeet) - 1;
                    // Fill the cell up to the level (exclusive at stepFeet) from EACH micro column's own top: a stoop
                    // laid from one height alone floats over lower columns (regen #10: a 3-micro slab at 156..158 over
                    // air at 153..155 beside the croft's door - the gate's start flood could not get under or onto it).
                    // After a cut the columns' tops are the cube below the removed ones.
                    for (int dx = 0; dx < 9; ++dx)
                        for (int dz = 0; dz < 9; ++dz) {
                            const int mx = cx * 9 + dx, mz = cz * 9 + dz;
                            int colTop = INT_MIN;
                            for (int my = stepFeet - 1; my > stepFeet - 40; --my)
                                if (solidMicro(mx, my, mz)) { colTop = my; break; }
                            if (colTop == INT_MIN) colTop = groundTop;
                            for (int my = colTop + 1; my < stepFeet; ++my)
                                emitMicro(out, mx, my, mz, stepMat);
                        }
                    ++steps; any = true;
                }
                if (any) ++stooped;
                {   // the door's approach as this unit read it (the gate measures the same cells)
                    const int doorwayTop = cellGroundTop(wallX, wallZ, threshold + 9);
                    std::string prof;
                    for (size_t ki = 0; ki < groundFeet.size(); ++ki)
                        prof += " " + (groundFeet[ki] == INT_MIN ? std::string("-") : std::to_string(groundFeet[ki])) + (pavedCell[ki] ? "p" : "")
                              + (fills[ki] != INT_MIN ? "->" + std::to_string(fills[ki]) + (fills[ki] < groundFeet[ki] ? "c" : "") : "");
                    LOG_INFO_FMT("Settlement", "doorsteps: " << fd.typology << " @" << fx << "," << fz << " door wall (" << wallX << "," << wallZ
                                 << ") normal (" << nx << "," << nz << ") threshold " << threshold << " doorway floor "
                                 << (doorwayTop == INT_MIN ? -1 : doorwayTop + 1) << " ground feet outward" << prof
                                 << " -> " << (any ? (cuts ? "stooped with cuts" : "stooped") : "no step") << (fd.hasDoor ? "" : " (marker-inferred wall)")
                                 << " [threshold - pad*9 = " << (fd.padY == INT_MIN ? -999 : threshold - fd.padY * 9) << "]");
                }
            }
            long placed = 0;
            if (!out.voxels.empty()) {
                placed = Core::StructureGenerator::place(chunkManager, out).placed;
            }
            LOG_INFO_FMT("Settlement", "doorsteps: " << stooped << " of " << sharedFrontDoors->size()
                         << " front doors stooped (" << steps << " steps, " << placed
                         << " micros; " << skipped << " without a threshold); approach band cleared "
                         << approachMicrosCleared << " micros");
            (*pathsJsonP)["doorsteps"] = {{"doors", sharedFrontDoors->size()}, {"stooped", stooped},
                                          {"steps", steps}, {"micros", placed},
                                          {"approach_cleared_micros", approachMicrosCleared}};
            }});
        }

        // WALKABILITY GATE (WalkabilityGateAndPlaytestLoop increment 1, layer A). On the
        // REALIZED occupancy - buildings, fences, paving, doorsteps, static furniture, all
        // as the chunks hold them - walk the routes a resident needs: street -> the passage
        // cell inside every front door, and that passage -> every ground-floor room centre.
        // The agent is the engine character's box (TraversalProbe AgentBox). A blocked route
        // is reported LOCATED (pinch coordinates) in the log and the job result; it is a
        // generator defect record, never patched here. Item props (fine-voxel, not chunk
        // voxels) are not in this occupancy yet - see the loop doc.
        // M0: EVERY morphology is gated. The routes start at each recorded front-door
        // marker, never at a street rectangle, so cluster hamlets gate exactly like a
        // main-street town (F7 live baseline 2026-10-14: 5 of 8 sites had no gate line).
        if (chunkManager) {
            units.push_back({"walkability gate",
                [chunkManager, placedObjectManager, objectTemplateManager, locationRegistry, npcManager, pushUndo,
                 sharedFrontDoors, sharedBuilt, pathsJsonP, walkJsonP]() {
            if (!chunkManager) return;
            const std::function<bool(int, int, int)> occ = [chunkManager](int x, int y, int z) {
                return chunkManager->occupiedMicro(glm::ivec3(x, y, z));
            };
            auto topBelow = [&](int cx, int cz, int fromMicroY) {
                const int mx = cx * 9 + 4, mz = cz * 9 + 4;
                for (int my = fromMicroY - 1; my > fromMicroY - 40; --my)
                    if (occ(mx, my, mz)) return my;
                return INT_MIN;
            };
            Core::AgentBox box;   // == the engine character
            Core::TraversalProbe probe(occ, box);
            // Seat a route endpoint: the lowest feet level at/above the column's top where
            // the WHOLE box fits and is supported (regen #9: a 1-micro-thick paving stamp on
            // uneven terrain and a threshold recorded as the surface micro left the start
            // box a micro inside the ground -> "reached 1/24659" / START-UNSUPPORTED, probe
            // artefacts reported as town defects). INT_MIN when nothing seats within 10 micro.
            auto seatFeet = [&](int cx, int cz, int hintMicroY) {
                const int top = topBelow(cx, cz, hintMicroY + 9);
                if (top == INT_MIN) return INT_MIN;
                const int mx = cx * 9 + 4, mz = cz * 9 + 4;
                for (int y = top + 1; y <= top + 10; ++y)
                    if (probe.fits(mx, y, mz) && probe.supported(mx, y, mz)) return y;
                return INT_MIN;
            };
            nlohmann::json routesJ = nlohmann::json::array();
            int walkable = 0, blocked = 0, skipped = 0;
            for (const auto& fd : *sharedFrontDoors) {
                if (fd.thresholdMicro < 0) { ++skipped; continue; }
                const int fx = fd.foot.x, fz = fd.foot.y, fw = fd.foot.z, fdp = fd.foot.w;
                // The marker is not always in front of the door (regen #14: house_14's
                // location sat at the footprint's corner column, 5 cells out on the street,
                // while its door is 4 cells along the wall). M0 row 2b: ONE door-finding
                // (doorApproach), shared with the spur unit and the cluster path network.
                const auto da = fd.hasDoor ? SettlementBuildService::doorApproachAt(fd.door, fd.normal)   // M5b-1: the forge's door
                                           : SettlementBuildService::doorApproach(fd.marker, fd.foot,
                                                 [&](int cx, int cz) { return seatFeet(cx, cz, fd.thresholdMicro) != INT_MIN; });
                const int passX = da.inside.x, passZ = da.inside.y;      // one cell inside the door
                glm::ivec2 startCell(fd.marker.x, fd.marker.z);
                int streetFeet = seatFeet(startCell.x, startCell.y, fd.thresholdMicro);
                // M5b-1 (chain 103): the marker's column can seat nothing at micro level (a fence's cube read as
                // standable by the cube-granular snap); the route then starts in FRONT of the door, two to four
                // cells out along its normal - the street side the marker meant.
                if (streetFeet == INT_MIN && (da.normal.x != 0 || da.normal.y != 0))
                    for (int k = 2; k <= 4 && streetFeet == INT_MIN; ++k) {
                        const glm::ivec2 c(da.wall.x + da.normal.x * k, da.wall.y + da.normal.y * k);
                        const int f = seatFeet(c.x, c.y, fd.thresholdMicro);
                        if (f != INT_MIN) { startCell = c; streetFeet = f; }
                    }
                const int passFeet = seatFeet(passX, passZ, fd.thresholdMicro);
                const std::string tag = fd.typology + " @" + std::to_string(fx) + "," + std::to_string(fz);
                if (streetFeet == INT_MIN || passFeet == INT_MIN) {
                    LOG_WARN_FMT("Settlement", "walkability: " << tag << " endpoint does not seat (street "
                                 << streetFeet << " at marker (" << fd.marker.x << "," << fd.marker.z << "), passage "
                                 << passFeet << " at cell (" << passX << "," << passZ << "), threshold "
                                 << fd.thresholdMicro << ", foot " << fx << "," << fz << " " << fw << "x" << fdp
                                 << ") - probe input, skipped");
                    ++skipped; continue;
                }
                std::vector<Core::WalkRoute> routes;
                Core::WalkRoute in;
                in.label = tag + ": street -> door";
                in.from = glm::ivec3(startCell.x * 9 + 4, streetFeet, startCell.y * 9 + 4);
                in.to   = glm::ivec3(passX * 9 + 4, passFeet, passZ * 9 + 4);
                in.goalRadiusMicro = 4;
                routes.push_back(in);
                for (const auto& rm : fd.rooms) {
                    if (rm.value("story", 0) != 0) continue;
                    const auto& rc = rm["rect"];
                    if (!rc.is_array() || rc.size() < 4) continue;
                    const int rx = fx + rc[0].get<int>(), rz = fz + rc[1].get<int>();
                    const int rw = rc[2].get<int>(), rd = rc[3].get<int>();
                    Core::WalkRoute r;
                    r.label = tag + ": door -> " + rm.value("purpose", std::string("room"));
                    r.from = in.to;
                    const int rcx = rx + rw / 2, rcz = rz + rd / 2;
                    const int roomFeet = seatFeet(rcx, rcz, fd.thresholdMicro);
                    r.to = glm::ivec3(rcx * 9 + 4, roomFeet == INT_MIN ? fd.thresholdMicro : roomFeet, rcz * 9 + 4);
                    r.goalRadiusMicro = 4;
                    // M5a-1 (2026-11-05, chains 78-80 on the live hamlet 3 headless): the goal is the room's
                    // INNER FLOOR AREA - the rect shrunk by one cube on each side where it is wider than two
                    // (the ring against the walls is where the furniture stands), the whole rect otherwise -
                    // at the passage's feet level with a story of slack. The point goal (the centre within 4
                    // micro) refused a croft whose centre cube the furnisher covered with a table: a walker
                    // cannot be within 4 micro of a point under an 18-micro table without standing on it, and
                    // the comment that stood here ("a fixture on the centre cell is normal - the goal box is 9
                    // micro wide and a story tall") assumed a climb the 4-micro step-up does not allow. The
                    // centre stays `to` for the record and the way back; a room whose inner area is wholly
                    // furnished is still refused - the walker must reach some cell of it.
                    const auto gb = roomGoalBox(rx, rz, rw, rd, passFeet);
                    r.goalIsBox = true;
                    r.goalLo = gb.first;
                    r.goalHi = gb.second;
                    routes.push_back(r);
                }
                // The band's floor must sit BELOW every endpoint: a threshold two cubes above
                // the street put the street start under the band's floor, and the flood's
                // bounds check broke out of the step loop at h=0 - "reached 1/24659" for
                // five doors in regen #10 (the traversal_probe diagnostic floods 8641 cells
                // from the same point with a sane band).
                // ...and its walls must enclose every endpoint with a margin. The recorded
                // footprint's w/d do not always match the marker's side of the building (a
                // rotated typology), and regen #12's gate-time diagnostics showed every first
                // step fitting and supported while the flood still reached one cell: the
                // start lay OUTSIDE the x/z band, so no neighbour passed the bounds check.
                glm::ivec3 bandLo = in.from, bandHi = in.from;
                auto grow = [&](const glm::ivec3& p) { bandLo = glm::min(bandLo, p); bandHi = glm::max(bandHi, p); };
                grow(in.to);
                for (const auto& r : routes) { grow(r.from); grow(r.to); }
                grow(glm::ivec3(fx * 9, in.from.y, fz * 9));
                grow(glm::ivec3((fx + fw) * 9, in.from.y, (fz + fdp) * 9));
                const glm::ivec3 lo(bandLo.x - 6 * 9, bandLo.y - 18, bandLo.z - 6 * 9);
                const glm::ivec3 hi(bandHi.x + 6 * 9, bandHi.y + 30, bandHi.z + 6 * 9);
                const auto rep = Core::checkRoutes(occ, box, routes, lo, hi, /*diagnose=*/true);
                walkable += rep.walkable; blocked += rep.blocked;
                for (size_t ri = 0; ri < rep.routes.size(); ++ri) {   // rep.routes is one per route, in order
                    const auto& rr = rep.routes[ri];
                    const auto& rin = routes[ri];
                    // M5a-1 (chain 79): the endpoints and the goal box travel with the record, so a blocked
                    // route can be re-walked and its goal inspected outside the gate
                    nlohmann::json j = {{"label", rr.label}, {"walkable", rr.walkable},
                                        {"from", {rin.from.x, rin.from.y, rin.from.z}}, {"to", {rin.to.x, rin.to.y, rin.to.z}},
                                        {"goal_radius", rin.goalRadiusMicro}, {"goal_height", rin.goalHeightMicro}};
                    if (rin.goalIsBox) j["goal_box"] = {rin.goalLo.x, rin.goalLo.y, rin.goalLo.z, rin.goalHi.x, rin.goalHi.y, rin.goalHi.z};
                    if (!rr.walkable) {
                        // A 1-cell start set is either a sealed start or a probe artefact:
                        // record the first-step verdicts (fits/supported per direction and
                        // step height) AT GATE TIME - regen #10/#11 reported five such starts
                        // that the same flood on the saved world does not reproduce.
                        if (rr.reachedFromStart <= 1) {
                            const glm::ivec3 st = rr.pinchFrom;
                            static const int ddx[4] = {1, -1, 0, 0}, ddz[4] = {0, 0, 1, -1};
                            std::ostringstream fs;
                            fs << "start (" << st.x << "," << st.y << "," << st.z << ") fits=" << probe.fits(st.x, st.y, st.z)
                               << " sup=" << probe.supported(st.x, st.y, st.z) << " occ_below=" << occ(st.x, st.y - 1, st.z);
                            for (int d = 0; d < 4; ++d) {
                                fs << " | d(" << ddx[d] << "," << ddz[d] << ")";
                                for (int h = 0; h <= box.maxStepUpMicro; ++h)
                                    fs << " h" << h << ":" << (probe.fits(st.x + ddx[d], st.y + h, st.z + ddz[d]) ? "F" : "-")
                                       << (probe.supported(st.x + ddx[d], st.y + h, st.z + ddz[d]) ? "S" : "-");
                            }
                            LOG_WARN_FMT("Settlement", "walkability: first-step diagnostics " << rr.label << ": " << fs.str());
                            j["first_steps"] = fs.str();
                        }
                        j["pinch_from"] = {rr.pinchFrom.x, rr.pinchFrom.y, rr.pinchFrom.z};
                        j["pinch_to"] = {rr.pinchTo.x, rr.pinchTo.y, rr.pinchTo.z};
                        j["pinch_gap_micro"] = rr.pinchGapMicro;
                        j["reached_from_start"] = rr.reachedFromStart;
                        j["reached_from_goal"] = rr.reachedFromGoal;
                        j["start_unsupported"] = rr.startUnsupported;
                        j["goal_unsupported"] = rr.goalUnsupported;
                        j["one_way"] = rr.oneWay;   // M0 row 2c: in but not back (a drop)
                        LOG_WARN_FMT("Settlement", "walkability: BLOCKED " << (rr.oneWay ? "(ONE-WAY, a drop) " : "") << rr.label
                                     << " pinch (" << rr.pinchFrom.x << "," << rr.pinchFrom.y << "," << rr.pinchFrom.z
                                     << ")->(" << rr.pinchTo.x << "," << rr.pinchTo.y << "," << rr.pinchTo.z
                                     << ") gap " << rr.pinchGapMicro << " micro; reached "
                                     << rr.reachedFromStart << "/" << rr.reachedFromGoal
                                     << (rr.startUnsupported ? " START-UNSUPPORTED" : "")
                                     << (rr.goalUnsupported ? " GOAL-UNSUPPORTED" : ""));
                    }
                    routesJ.push_back(j);
                }
            }
            LOG_INFO_FMT("Settlement", "walkability gate: " << walkable << " walkable, " << blocked
                         << " blocked over " << (walkable + blocked) << " routes ("
                         << sharedFrontDoors->size() << " front doors of " << *sharedBuilt << " built, " << skipped << " skipped)");
            (*pathsJsonP)["walkability"] = {{"routes", routesJ}, {"walkable", walkable},
                                            {"blocked", blocked}, {"failed", blocked}, {"skipped", skipped},
                                            {"doors", sharedFrontDoors->size()}, {"built", *sharedBuilt}};
            *walkJsonP = (*pathsJsonP)["walkability"];
            }});
        }

        // Nav rebuild after EVERYTHING: per-building builds refresh their own boxes
        // (StructureBuildService onRegionChanged), but street paving / terraces / fence
        // spurs mutate terrain outside those boxes, and on a fresh world the grid may not
        // exist at all (built pre-settlement over near-empty chunk bounds) — so region
        // refreshes hit no cells. One full rebuild re-derives bounds from the now-loaded
        // chunks; without it navgrid/path over the finished settlement returns found:false.
        units.push_back({"nav rebuild", [chunkManager, placedObjectManager, objectTemplateManager, locationRegistry, npcManager, pushUndo,
                                          walkJsonP, allowUnwalkable]() {
            if (gateOutcome(*walkJsonP, allowUnwalkable).value("fails_build", false)) {
                LOG_WARN_FMT("Settlement", "walkability gate BLOCKED the build (" << walkJsonP->value("blocked", 0)
                             << " blocked routes, " << walkJsonP->value("skipped", 0)
                             << " front door(s) unseated) - nav rebuild skipped; the settlement is a refused outcome");
                return;
            }
            if (npcManager) npcManager->buildNavGrid();
        }});

        // Residents (playable-town increment 4): one NPC per building, planned from the
        // locations the buildings just registered — the smith works the smithy, everyone
        // sleeps at home and hits the tavern in the evening (ResidentPlanner). Runs after
        // the nav rebuild so residents can path from frame one. Program-mode default ON;
        // {"residents": false} opts out (worldforge builds do — the ResidentSpawner owns
        // their lifecycle). NPCs are DERIVED state, never persisted; the LOCATIONS persist
        // (world_meta["locations"]) and the ResidentSpawner re-derives identical residents
        // on reload/stream-in, adopting any this unit spawned by their deterministic names.
        const bool spawnResidents = programMode && p.value("residents", true);
        if (spawnResidents) {
            const int rW = W, rD = D, rOx = ox, rOz = oz;
            units.push_back({"spawn residents", [chunkManager, placedObjectManager, objectTemplateManager, locationRegistry, npcManager, pushUndo, residentsJsonP, rW, rD, rOx, rOz,
                                              walkJsonP, allowUnwalkable]() {
                if (gateOutcome(*walkJsonP, allowUnwalkable).value("fails_build", false)) return;  // M0: no residents in a refused town
                if (!npcManager || !locationRegistry) return;
                std::vector<Core::Location> locs;
                for (const auto& [id, loc] : locationRegistry->getAllLocations()) {
                    if (loc.position.x < rOx - 4 || loc.position.x > rOx + rW + 4 ||
                        loc.position.z < rOz - 4 || loc.position.z > rOz + rD + 4)
                        continue;
                    locs.push_back(loc);
                }
                auto plans = Core::ResidentPlanner::planResidents(locs);
                int spawned = 0, adopted = 0, failed = 0;
                for (const auto& pl : plans) {
                    // The live ResidentSpawner (streaming/reload path) spawns a resident as
                    // soon as its building's locations land - by the same deterministic name.
                    // ADOPT it (schedule refreshed from the final plan) instead of failing on
                    // "already exists" and reporting a bogus 0/N (Ravenmere G-70).
                    if (auto* existing = npcManager->getNPC(pl.name)) {
                        if (auto* sb = dynamic_cast<Scene::ScheduledBehavior*>(existing->getBehavior()))
                            sb->setSchedule(pl.schedule);
                        ++adopted;
                        continue;
                    }
                    auto* npc = npcManager->spawnProceduralNPC(
                        pl.name, "resources/animated_characters/humanoid.anim",
                        pl.spawnPos + glm::vec3(0.0f, 1.0f, 0.0f),
                        Core::NPCBehaviorType::Scheduled, pl.role);
                    if (!npc) {
                        LOG_WARN_FMT("Settlement", "resident spawn FAILED: " << pl.name);
                        ++failed;
                        continue;
                    }
                    if (auto* sb = dynamic_cast<Scene::ScheduledBehavior*>(npc->getBehavior()))
                        sb->setSchedule(pl.schedule);
                    ++spawned;
                }
                LOG_INFO_FMT("Settlement", "residents: " << spawned << " spawned + " << adopted
                             << " adopted of " << plans.size() << " planned (" << failed << " failed)");
                (*residentsJsonP) = {{"spawned", spawned}, {"adopted", adopted}, {"failed", failed},
                                     {"planned", plans.size()}};
            }});
        }

        nlohmann::json programJson = nlohmann::json::object();
        if (programMode && tierP) {
            // Echo {era, tier, seed, density} so a live build is exactly reproducible
            // (determinism contract; density echoes CLAMPED so the caller sees what applied).
            programJson = {{"era", era}, {"tier", tierName}, {"seed", seed},
                           {"density", density}, {"morphology", tierP->morphology},
                           // the EFFECTIVE cap after density and site scaling (echoed, so a
                           // caller can tell "the site was full" from "the cap bound")
                           {"buildings_cap", tierP->buildingsMax}};
            if (mainStreetMode) {
                programJson["main_street"] = {{"x", ox + msl.mainStreet.x}, {"z", oz + msl.mainStreet.z},
                                              {"w", msl.mainStreet.w}, {"d", msl.mainStreet.d}};   // a map build: the primary way's bounding box
                if (primaryWayId >= 0) programJson["primary_way"] = primaryWayId;
                programJson["dropped_plots"] = droppedPlots;
                if ((int)buildings.size() < tierP->buildingsMin)
                    programJson["below_tier_min"] = tierP->buildingsMin;   // surfaced, not silent
            }
        }
        const nlohmann::json settlementJson =
            {{"plots", layout.plots.size()}, {"streets", layout.streets.size()},
             {"buildings", buildings.size()}, {"origin", {{"x", ox}, {"y", oy}, {"z", oz}}}};

    // M0: the derived canopy margins are part of the plan's record (a caller/test can see them).
    programJson["parcel_clear_margin"] = parcelClearMargin;
    programJson["canopy_sweep_margin"] = canopySweepMargin;
    programJson["canopy_margin_source"] = canopyMarginSource;
    programJson["terrace_top"] = terraceTop;
    programJson["terrace_fill"] = terraceFill;
    programJson["fence_material"] = fenceMat;
    programJson["path_material"] = pathMat;
    res.settlement   = settlementJson;
    if (!chunkManager) {
        // Planning without a world is legitimate (a caller may only want the layout), but
        // EVERY site-prep unit is world-gated, so the returned plan builds BUILDINGS ONLY --
        // no parcel clearing, terracing, street paving, fences or yard props. Surface that
        // instead of handing back a quietly partial plan.
        res.settlement["site_prep_skipped"] = true;
        res.settlement["site_prep_skipped_reason"] =
            "no ChunkManager: clearing / terracing / paving / fencing / yard-prop units were "
            "not planned; this plan places buildings only";
    }
    if (mapLots) {
        programJson["map_sampler"] = mapSampler;
        programJson["target_lots"] = siteMap.targetLots;   // the growth's aim; fewer buildings than this = lots the site did not seat (surfaced)
        res.map = siteMap.toJson();
        res.settlement["map"] = {{"lots", siteMap.lots.size()}, {"ways", siteMap.ways.size()}, {"nodes", siteMap.nodes.size()}, {"entry_node", siteMap.entryNode}};
    }
    res.program      = programJson;
    res.queuedBuilds = queued;
    res.jobLabel     = programMode
                           ? ("settlement " + era + "/" + tierName + " seed " + std::to_string(seed))
                           : std::string("settlement (legacy params)");
    res.units        = std::move(units);
    return res;
}

std::pair<glm::ivec3, glm::ivec3> SettlementBuildService::roomGoalBox(int rx, int rz, int rw, int rd, int feetMicro) {
    const int ix0 = (rw > 2) ? rx + 1 : rx, ix1 = (rw > 2) ? rx + rw - 1 : rx + rw;
    const int iz0 = (rd > 2) ? rz + 1 : rz, iz1 = (rd > 2) ? rz + rd - 1 : rz + rd;
    return {glm::ivec3(ix0 * 9, feetMicro - 9, iz0 * 9), glm::ivec3(ix1 * 9 - 1, feetMicro + 9, iz1 * 9 - 1)};
}

nlohmann::json SettlementBuildService::gateOutcome(const nlohmann::json& walkability,
                                                   bool allowUnwalkable) {
    // Pure: the verdict is a function of the gate's result and the override, nothing
    // else — the editor response, the WorldForge checkpoint and the post-gate units
    // must never disagree about whether a settlement failed its gate.
    const bool ran = walkability.is_object() && walkability.contains("blocked");
    const int walkable = ran ? walkability.value("walkable", 0) : 0;
    const int blocked  = ran ? walkability.value("blocked", 0) : 0;
    // M0 row 2d (solution-auditor 2026-10-15): a SKIPPED front door - one whose street or
    // passage endpoint does not seat the agent at the threshold, or that recorded no
    // threshold - is a door nobody can use. The gate used to skip it as "probe input" and
    // pass (the town's tavern: "5 front doors, 1 skipped" shipped). It now fails the build
    // like a blocked route ("unseated"), with the same override.
    const int skipped  = ran ? walkability.value("skipped", 0) : 0;
    // M5a-1 (chain 74): a gate that measured NO door of a built building is not a pass - the
    // headless rig built 3 and 5 buildings whose doors were never recorded and "passed" on 0 routes.
    // The gate unit records how many buildings realized and how many front doors it saw; a
    // shortfall fails the build like a blocked route ("doorless"), with the same override. A
    // built count of 0 (every lot refused) is not doorless - the lot failures carry that; a
    // result without the counts (an older record) keeps its old reading.
    const int doors = ran ? walkability.value("doors", -1) : -1;
    const int built = ran ? walkability.value("built", -1) : -1;
    const bool doorless = ran && built > 0 && doors >= 0 && doors < built;
    std::string gate = "not_run";
    bool failsBuild = false;
    if (ran) {
        if (blocked <= 0 && skipped <= 0 && !doorless) gate = "passed";
        else if (allowUnwalkable)         gate = "overridden";
        else if (blocked > 0) { gate = "blocked";  failsBuild = true; }
        else if (skipped > 0) { gate = "unseated"; failsBuild = true; }
        else                  { gate = "doorless"; failsBuild = true; }
    }
    nlohmann::json out = {{"gate", gate}, {"walkable", walkable}, {"blocked", blocked},
                          {"skipped", skipped}, {"fails_build", failsBuild}};
    if (doors >= 0) out["doors"] = doors;
    if (built >= 0) out["built"] = built;
    if (failsBuild) {   // ONE wording for the editor response, the job result and the ledger
        out["error"] = "unwalkable: " + std::to_string(blocked) + " of " + std::to_string(blocked + walkable)
                       + " street->door routes blocked, " + std::to_string(skipped)
                       + " front door(s) unseated (walkability gate)";
        if (doorless)
            out["error"] = out["error"].get<std::string>() + "; doorless: " + std::to_string(built - doors) + " of "
                           + std::to_string(built) + " built building(s) recorded no front door - the gate measured nothing for them";
    }
    return out;
}

StructureBuildService::Deps SettlementBuildService::Deps::structureDeps() const {
    StructureBuildService::Deps d;
    d.chunkManager  = chunkManager;
    d.placedObjects = placedObjects;
    d.templates     = templates;
    d.locations     = locations;
    d.npcs          = npcs;
    d.itemProps     = itemProps;      // M3c: sign items + tableware in settlements
    d.keepClear     = keepClear;      // designer anchors stay furniture-free
    d.pushUndo      = pushUndo;       // editor undo snapshot, forwarded
    d.addPointLight = addPointLight;  // M5: light settlement interiors too
    return d;
}

int SettlementBuildService::canopyMarginCubes(const std::vector<int>& crownHalfWidths, int floorCubes) {
    int m = floorCubes;
    for (int hw : crownHalfWidths) m = std::max(m, hw);
    return m;
}

std::set<int> SettlementBuildService::failedPlots(const nlohmann::json& lotFailures,
                                                  const std::vector<int>& lotToPlot) {
    std::set<int> out;
    if (!lotFailures.is_array()) return out;
    for (const auto& f : lotFailures) {
        const int lot = f.value("lot", -1);
        if (lot >= 0 && lot < static_cast<int>(lotToPlot.size()) && lotToPlot[lot] >= 0)
            out.insert(lotToPlot[lot]);
    }
    return out;
}

SettlementBuildService::DoorApproach SettlementBuildService::doorApproach(
    const glm::ivec3& marker, const glm::ivec4& foot, const std::function<bool(int, int)>& seats) {
    DoorApproach d;
    const int fx = foot.x, fz = foot.y, fw = foot.z, fdp = foot.w;
    int nx = 0, nz = 0;
    if (marker.x < fx)            nx = -1;
    else if (marker.x >= fx + fw) nx = +1;
    else if (marker.z < fz)       nz = -1;
    else                          nz = +1;
    const int wallX = nx < 0 ? fx : nx > 0 ? fx + fw - 1 : marker.x;
    const int wallZ = nz < 0 ? fz : nz > 0 ? fz + fdp - 1 : marker.z;
    d.normal = {nx, nz};
    d.wall = {wallX, wallZ};
    if (seats) {
        // The opening along the marker's wall whose wall cell AND inside cell both seat the
        // agent at the threshold (a window bay seats only the wall cell), nearest the marker.
        int best = INT_MAX;
        const int len = (nz != 0) ? fw : fdp;
        for (int k = 0; k < len; ++k) {
            const int cx = (nz != 0) ? fx + k : wallX;
            const int cz = (nz != 0) ? wallZ : fz + k;
            if (!seats(cx, cz) || !seats(cx - nx, cz - nz)) continue;
            const int dist = (nz != 0) ? std::abs(cx - marker.x) : std::abs(cz - marker.z);
            if (dist < best) { best = dist; d.wall = {cx, cz}; d.found = true; }
        }
    }
    const int wx = d.wall.x, wz = d.wall.y;
    d.outside = {wx + nx, wz + nz};
    d.inside = {wx - nx, wz - nz};
    return d;
}

SettlementBuildService::DoorApproach SettlementBuildService::doorApproachAt(const glm::ivec2& doorWall, const glm::ivec2& normal) {
    DoorApproach d;
    d.wall = doorWall; d.normal = normal; d.found = true;
    d.outside = {doorWall.x + normal.x, doorWall.y + normal.y};
    d.inside = {doorWall.x - normal.x, doorWall.y - normal.y};
    return d;
}


} // namespace Core
} // namespace Phyxel
