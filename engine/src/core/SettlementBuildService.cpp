#include "core/SettlementBuildService.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/ChunkManager.h"
#include "core/DimensionCanon.h"
#include "core/FenceBuilder.h"
#include "core/FurnitureCatalog.h"
#include "core/LocationRegistry.h"
#include "core/NPCManager.h"
#include "core/ObjectTemplateManager.h"
#include "core/PathPlanner.h"
#include "core/PlacedObjectManager.h"
#include "core/ResidentPlanner.h"
#include "core/RoomProgram.h"
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

SettlementBuildService::Plan SettlementBuildService::plan(const nlohmann::json& params,
                                                          const Deps& deps) {
    Plan res;
    res.paths       = std::make_shared<nlohmann::json>(nlohmann::json::object());
    res.yardProps   = std::make_shared<nlohmann::json>(nlohmann::json::object());
    res.lotFailures = std::make_shared<nlohmann::json>(nlohmann::json::array());
    res.residents = std::make_shared<nlohmann::json>(nlohmann::json::object());

    // Bound ONCE and captured BY VALUE into the work units, which outlive this frame.
    // Deliberately named to match the fields they replace so the relocated body reads
    // identically to the handler it came from (the move stays reviewable as a move).
    ChunkManager* const chunkManager                 = deps.chunkManager;
    PlacedObjectManager* const placedObjectManager   = deps.placedObjects;
    ObjectTemplateManager* const objectTemplateManager = deps.templates;
    LocationRegistry* const locationRegistry         = deps.locations;
    NPCManager* const npcManager                     = deps.npcs;
    const auto pushUndo                              = deps.pushUndo;

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
        if (programMode) {
            if (!programReg.loadFromFile("resources/settlement_program.json")) {
                res.error = {{"error", "settlement_program.json failed to load"}};
                return res;
            }
            tierP = programReg.get(era, tierName);
            if (!tierP) {
                res.error = {{"error", "unknown era/tier: " + era + "/" + tierName},
                     {"known_eras", programReg.eras()}, {"known_tiers", programReg.tiers(era)}};
                return res;
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
        roomReg.loadFromFile("resources/room_program.json");

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

        // TERRAIN MODE: analyse the live world's buildability and place plots on the buildable cells
        // (flat valleys + hilltops), skipping cliffs/steep. Else: the flat-plane grid.
        const bool terrain = p.value("terrain", false);
        Core::SettlementLayout layout;
        Core::MainStreetLayout msl;   // program main-street plan (used when mainStreetMode)
        int droppedPlots = 0;         // main-street plots dropped on unbuildable terrain (surfaced)
        if (terrain) {
            if (!chunkManager) { res.error = {{"error", "terrain mode needs a loaded world (no ChunkManager)"}}; return res; }
            const int plotSize  = p.value("plot_size", 12);
            const int maxRelief = p.value("max_relief", 6);
            const int maxPlots  = p.value("max_plots", 25);
            const int window    = std::max(1, plotSize / 2);
            const Core::BuildabilityMap site =
                Core::analyzeSite(W, D, maxRelief,
                                  [&](int x, int z) { return terrainTopAt(ox + x, oz + z); },
                                  {}, 1, window);
            if (mainStreetMode) {
                // The spine picks the FLATTEST straight alignment over the site, then plots whose
                // footprint touches an unbuildable cell are dropped with a surfaced count
                // (graceful degradation, TerrainAwareSettlement.md).
                const Core::StreetAxisChoice pick =
                    Core::chooseStreetAxis(site, tierP->street.mainWidth, tierP->plot.depthMin);
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
        // Buildings: main-street mode already assigned + sized every building from its typology
        // (footprint = natural size, front at the drawn setback); legacy mode sites one per plot
        // and sizes it in the queue loop below.
        std::vector<Core::PlacedBuilding> buildings;
        if (mainStreetMode) {
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
        auto propsJsonP = res.yardProps;
        auto residentsJsonP = res.residents;
        auto sharedPaved = std::make_shared<std::set<std::pair<int, int>>>();
        // Road-band cells (world coords, walkable headroom over the paving) recorded by
        // the street unit for the late "street sweep": construction can drop debris on a
        // finished road (a building's site prep UNDERMINES a roadside tree -> U4 topples
        // it -> the felled trunk RETIRES as static voxels across the street; measured,
        // deterministic at seed 3) — swept again after the buildings, before nav rebuild.
        auto sharedRoadBand = std::make_shared<std::vector<glm::ivec3>>();
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
        if (chunkManager) {
            std::vector<Core::Rect> prects;
            prects.reserve(layout.plots.size());
            for (const auto& pl : layout.plots) prects.push_back(pl.rect);
            units.push_back({"clearing parcels",
                [chunkManager, placedObjectManager, objectTemplateManager, locationRegistry, npcManager, pushUndo, prects, ox, oz, terrainTopAt, groundTopAt]() {
                if (!chunkManager) return;
                constexpr int kMargin = 4;    // canopy overhang reach beyond the plot line
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
                chunkManager->rebuildOccupancyFromChunks();
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
                units.push_back({ph, [chunkManager, placedObjectManager, objectTemplateManager, locationRegistry, npcManager, pushUndo, pr, ox, oz, oy, terrainTopAt, groundTopAt]() {
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
                            for (int y = cur + 1; y < target; ++y) addT(x, y, z, "Dirt");
                            tcut.push_back(glm::ivec3(x, target, z));   // resurface graded top
                            addT(x, target, z, "Grass");
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
            const auto variantMix =
                mainStreetMode ? std::vector<std::string>{b.typology} : mix;
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
                if (!mainStreetMode) {
                    if (const Core::RoomProgram* rp = roomReg.get(v.typology)) {
                        const int natLong  = std::max(1, (int)std::lround(rp->bays * rp->bayLength));
                        const int natShort = std::max(1, (int)std::lround(rp->widthMax > 0 ? rp->widthMax
                                                                                           : rp->widthMin));
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
            // Street-facing front hint (OpeningsLayoutTest.FrontHint*): the entrance wall faces
            // the plot's street side. The building rect is axis-aligned in settlement coords, so
            // 'S' (street at -z) = its local z0 wall, etc. No street (terrain mode) = no hint.
            std::string front;
            const char fside = mainStreetMode
                ? msl.assigned[i].streetSide   // explicit: the side this plot ABUTS the main street
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
            auto makeBp = [&](const Core::BuildingVariant& v, int w, int d, int x, int z) {
                return nlohmann::json{
                    {"schema", "v2"}, {"type", "house"}, {"style", v.style}, {"typology", v.typology},
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
                                          locationRegistry, npcManager, pushUndo, bp, bp2, seatInUnit,
                                          bw, bd, bw2, bd2, oy, lotFailures = res.lotFailures,
                                          lotIndex = static_cast<int>(i),
                                          typ1 = var.typology, typ2 = var2.typology]() mutable {
                if (!chunkManager) return;
                auto seat = [&](nlohmann::json& p, int w, int d) {
                    if (!seatInUnit) return;
                    const int cx = p["position"].value("x", 0) + w / 2;
                    const int cz = p["position"].value("z", 0) + d / 2;
                    p["position"]["y"] =
                        settlementTopScan(chunkManager, oy, cx, cz, /*floraBlind=*/true);
                };
                Core::StructureBuildService::Deps deps;
                deps.chunkManager  = chunkManager;
                deps.placedObjects = placedObjectManager ? &*placedObjectManager : nullptr;
                deps.templates     = objectTemplateManager ? &*objectTemplateManager : nullptr;
                deps.locations     = locationRegistry ? &*locationRegistry : nullptr;
                deps.npcs          = npcManager ? &*npcManager : nullptr;
                deps.pushUndo      = pushUndo;   // forwarded by the caller (editor: undo snapshot)
                seat(bp, bw, bd);
                const auto res1 = Core::StructureBuildService::buildV2(bp, deps);
                if (!res1.contains("error")) return;
                LOG_WARN_FMT("Settlement", "lot " << lotIndex << " (" << typ1 << ") refused: "
                             << res1["error"].dump() << " — re-rolling the variant");
                seat(bp2, bw2, bd2);
                const auto res2 = Core::StructureBuildService::buildV2(bp2, deps);
                if (!res2.contains("error")) return;
                LOG_WARN_FMT("Settlement", "lot " << lotIndex << " re-roll (" << typ2
                             << ") ALSO refused: " << res2["error"].dump()
                             << " — lot left EMPTY (recorded)");
                if (lotFailures)
                    lotFailures->push_back({{"lot", lotIndex}, {"typology", typ1},
                                            {"retry_typology", typ2},
                                            {"error", res1.value("error", std::string())},
                                            {"retry_error", res2.value("error", std::string())}});
            }});
            queued.push_back({{"plot", b.plotIndex}, {"position", {{"x", bx}, {"y", by}, {"z", bz}}},
                              {"footprint", nlohmann::json::array({bw, bd})},
                              {"typology", var.typology}, {"style", var.style}, {"shape", var.footprintShape}});
            doorCenters.push_back(glm::ivec3(bx + bw / 2, by, bz + bd / 2));  // path anchor
            bFoot.push_back(glm::ivec4(bx, bz, bw, bd));                      // footprint for path masking (V7)
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

        if (mainStreetMode && chunkManager) {
            // STREET PAVING (Phase 2), as ONE work unit: grade + pave the full street width in the
            // tier's material, plus a spur from every building's front-wall midpoint. CUT columns
            // honored (terrain removed above the graded surface, then capped).
            units.push_back({"grading + paving streets",
                [chunkManager, placedObjectManager, objectTemplateManager, locationRegistry, npcManager, pushUndo, tierStreetMat = tierP->street.material, msl, streets = layout.streets,
                 bFoot, ox, oz, groundMicro, groundTopAt, isFloraMat, fl9, emitMicro, abox,
                 pathsJsonP, sharedPaved, sharedRoadBand, sharedStamp]() {
            if (!chunkManager) return;
            auto& pathsJson = *pathsJsonP;
            auto& pavedCols = *sharedPaved;
            const std::string paveMat = tierStreetMat;
            std::vector<Core::DoorAnchor> doors;
            for (const auto& ap : msl.assigned) {
                const int fx = ox + ap.footprint.x, fz = oz + ap.footprint.z;
                const int fw = ap.footprint.w, fdp = ap.footprint.d;
                int dxm = 0, dzm = 0;   // micro, just OUTSIDE the front wall's midpoint
                switch (ap.streetSide) {
                    case 'S': dxm = (fx + fw / 2) * 9 + 4; dzm = fz * 9 - 5; break;
                    case 'N': dxm = (fx + fw / 2) * 9 + 4; dzm = (fz + fdp) * 9 + 4; break;
                    case 'W': dxm = fx * 9 - 5; dzm = (fz + fdp / 2) * 9 + 4; break;
                    default:  dxm = (fx + fw) * 9 + 4; dzm = (fz + fdp / 2) * 9 + 4; break;  // 'E'
                }
                doors.push_back({dxm, dzm, groundMicro(dxm, dzm)});
            }
            const Core::PavingPlan pave = Core::planStreetPaving(
                streets, glm::ivec2(ox, oz), doors, bFoot, groundMicro, abox, paveMat);
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
                        sharedRoadBand->push_back(glm::ivec3(cbx, y, cbz));
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
            sharedStamp->reserve(pave.columns.size());
            for (size_t i = 0; i < pave.columns.size(); ++i) {
                const auto& c = pave.columns[i];
                pavedCols.insert({fl9(c.x), fl9(c.z)});
                sharedStamp->push_back(glm::ivec4(c.x, startRow[i], c.z, c.surface));
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
            chunkManager->rebuildOccupancyFromChunks();
            const double stampMs = std::chrono::duration<double, std::milli>(
                                       std::chrono::steady_clock::now() - t0).count();
            LOG_INFO_FMT("Settlement", "streets: paved " << placedMicros << " micros over "
                         << pave.columns.size() << " columns (" << pave.levelCols << " level, "
                         << pave.fillCols << " fill, " << pave.cutCols << " cut honored, "
                         << cutCubes.size() << " cubes removed, " << bandCleared
                         << " corridor cells band-cleared), spurs " << pave.spursPlanned
                         << " ok / " << pave.spursFailed << " too steep, " << paveMat << ", "
                         << static_cast<long>(stampMs) << " ms");
            pathsJson = {{"paved_columns", static_cast<long>(pave.columns.size())},
                         {"paved_microcubes", placedMicros},
                         {"level_cols", pave.levelCols}, {"fill_cols", pave.fillCols},
                         {"cut_cols_honored", pave.cutCols},
                         {"cut_cubes_removed", static_cast<long>(cutCubes.size())},
                         {"cut_cells_unpaved", 0},
                         {"grass_converted", grassConverted},
                         {"spurs", pave.spursPlanned}, {"spurs_too_steep", pave.spursFailed},
                         {"material", paveMat}, {"stamp_ms", static_cast<long>(stampMs)}};
            }});
        } else if (terrain && chunkManager && doorCenters.size() >= 2) {
            // MST door-to-door path network (cluster/legacy morphology), as ONE work unit.
            units.push_back({"grading + paving paths",
                [chunkManager, placedObjectManager, objectTemplateManager, locationRegistry, npcManager, pushUndo, doorCenters, bFoot, ox, oy, groundMicro, surfMicro, fl9, emitMicro, abox,
                 pathsJsonP, sharedPaved]() {
            if (!chunkManager) return;
            auto& pathsJson = *pathsJsonP;
            auto& pavedCols = *sharedPaved;
            const std::string paveMat = "Cobblestone";
            std::vector<Core::DoorAnchor> doors;
            for (const auto& d : doorCenters)
                doors.push_back({d.x * 9, d.z * 9,
                                 (settlementTopScan(chunkManager, oy, d.x, d.z, true) + 1) * 9});
            const Core::SettlementPaths net = Core::planSettlementPaths(doors, groundMicro, abox);
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
            chunkManager->rebuildOccupancyFromChunks();   // so the paving is part of the static collision world
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
            units.push_back({"fencing parcels",
                [chunkManager, placedObjectManager, objectTemplateManager, locationRegistry, npcManager, pushUndo, layout, msl, doorCenters, ox, oz, terrainTopAt, mainStreetMode,
                 fl9, rem9, emitMicro, pathsJsonP, sharedPaved]() {
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
            Core::StructureResult fenceBatch;   // bulk emit — one place() for ALL parcels
            long fenceMicros = 0; int parcels = 0;
            for (size_t pi = 0; pi < layout.plots.size(); ++pi) {
                const auto& pl = layout.plots[pi];
                const Core::Rect& pr = pl.rect;
                if (pr.w < 2 || pr.d < 2) continue;
                // Gate side: a main-street parcel opens onto ITS street (the burgage frontage);
                // a scatter parcel faces the settlement centroid / path network.
                char gate;
                if (mainStreetMode && pi < msl.assigned.size()) {
                    gate = msl.assigned[pi].streetSide;
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
                        // Cube-aligned gate window, EXACTLY the legacy centering: derive
                        // the cube span (runLenMicro = (cubes-1)*9+1, so ceil-div
                        // recovers it) and center in cubes — the naive micro formula
                        // drifted up to 4 micro off the old center on odd spans
                        // (auditor-caught).
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
                        emitMicro(fenceBatch, wx, base + c.y, wz, "Log");
                    }
                };
                // KI-5f: composition from planParcelFenceRuns — every run ends exactly on
                // the corner intersections; one post per corner (endPosts mechanism),
                // perpendicular rails reach it (duplicate cells at the shared corner
                // column are deduped by place()).
                for (const auto& run : Core::planParcelFenceRuns(pr.x, pr.z, pr.w, pr.d))
                    stampEdge(run);
            }
            fenceMicros = Core::StructureGenerator::place(chunkManager, fenceBatch).placed;
            chunkManager->rebuildOccupancyFromChunks();
            LOG_INFO_FMT("Settlement", "fences: " << parcels << " parcels, " << fenceMicros
                         << " micros (picket, " << fH << "-micro tall, posts @" << fSp << ")");
            pathsJson["parcels"] = parcels;
            pathsJson["fence_micros"] = fenceMicros;
            pathsJson["fence_type"] = Core::fenceTypeToString(fenceType);
            }});
        }

        // YARD PROPS + the shared WELL (#29/#25 minimum slice), as ONE work unit: a woodpile
        // behind each house + a kitchen-garden bed in the open toft (planYardProps); tier
        // `public.well` anchors the market square (town) or the street verge (village).
        // Skips are COUNTED, never silent.
        if (mainStreetMode && placedObjectManager && objectTemplateManager && chunkManager) {
            units.push_back({"yard props + well",
                [chunkManager, placedObjectManager, objectTemplateManager, locationRegistry, npcManager, pushUndo, msl, ox, oz, seed, terrainTopAt, pubWell = tierP->pub.well, propsJsonP]() {
            if (!chunkManager || !placedObjectManager || !objectTemplateManager) return;
            int propsPlaced = 0, propsSkipped = 0;
            auto spawnProp = [&](const std::string& type, int cx, int cz, int rot) -> bool {
                const std::string tmpl = Core::FurnitureCatalog::templateFor(type);
                if (tmpl.empty() || !objectTemplateManager->getTemplate(tmpl)) return false;
                const int gy = terrainTopAt(cx, cz) + 1;             // stand on the ground/paving
                return !placedObjectManager
                            ->placeTemplateMicro(tmpl, glm::ivec3(cx * 9, gy * 9, cz * 9), rot, "")
                            .empty();
            };
            for (const auto& ap : msl.assigned)
                for (const auto& yp : Core::planYardProps(ap, seed))
                    (spawnProp(yp.type, ox + yp.cx, oz + yp.cz, yp.rotDeg) ? ++propsPlaced
                                                                           : ++propsSkipped);
            if (pubWell) {
                int wcx, wcz;
                if (msl.hasSquare) {                  // the town well anchors the market square
                    wcx = ox + msl.marketSquare.x + msl.marketSquare.w / 2;
                    wcz = oz + msl.marketSquare.z + msl.marketSquare.d / 2;
                } else {                              // village: the main street's verge, mid-length
                    const Core::Rect& ms = msl.mainStreet;
                    const bool msAlongX = ms.w >= ms.d;
                    wcx = ox + (msAlongX ? ms.x + ms.w / 2 : ms.x + 1);
                    wcz = oz + (msAlongX ? ms.z + 1 : ms.z + ms.d / 2);
                }
                (spawnProp("well", wcx, wcz, 0) ? ++propsPlaced : ++propsSkipped);
            }
            LOG_INFO_FMT("Settlement", "yard props: " << propsPlaced << " placed, "
                         << propsSkipped << " skipped");
            (*propsJsonP) = {{"placed", propsPlaced}, {"skipped", propsSkipped}};
            }});
        }

        // Buildings LAST (site prep — terrace/streets/fences/props — must precede them).
        for (auto& bu : buildingUnits) units.push_back(std::move(bu));

        // Street sweep AFTER the buildings: construction drops debris on finished
        // roads (site prep undermines a roadside tree -> U4 fells it -> the trunk
        // retires as static voxels across the street). Re-clear the walkable band
        // recorded by the street unit so the road the nav rebuild sees is the road
        // NPCs actually get.
        units.push_back({"street sweep",
            [chunkManager, placedObjectManager, objectTemplateManager, locationRegistry, npcManager, pushUndo, sharedRoadBand, sharedStamp, emitMicro,
             sweepMat = (programMode && tierP ? tierP->street.material
                                             : std::string("Gravel"))]() {
            if (!chunkManager || sharedRoadBand->empty()) return;
            std::map<Chunk*, std::vector<glm::ivec3>> byChunk;
            for (const auto& wp : *sharedRoadBand)
                if (Chunk* ch = chunkManager->getChunkAtFast(wp))
                    byChunk[ch].push_back(wp - ch->getWorldOrigin());
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
            chunkManager->rebuildOccupancyFromChunks();
            LOG_INFO_FMT("Settlement", "street sweep: " << swept << " cells cleared over "
                         << sharedRoadBand->size() << " band cells, " << restamped
                         << " paving micros re-stamped");
        }});

        // Nav rebuild after EVERYTHING: per-building builds refresh their own boxes
        // (StructureBuildService onRegionChanged), but street paving / terraces / fence
        // spurs mutate terrain outside those boxes, and on a fresh world the grid may not
        // exist at all (built pre-settlement over near-empty chunk bounds) — so region
        // refreshes hit no cells. One full rebuild re-derives bounds from the now-loaded
        // chunks; without it navgrid/path over the finished settlement returns found:false.
        units.push_back({"nav rebuild", [chunkManager, placedObjectManager, objectTemplateManager, locationRegistry, npcManager, pushUndo]() {
            if (npcManager) npcManager->buildNavGrid();
        }});

        // Residents (playable-town increment 4): one NPC per building, planned from the
        // locations the buildings just registered — the smith works the smithy, everyone
        // sleeps at home and hits the tavern in the evening (ResidentPlanner). Runs after
        // the nav rebuild so residents can path from frame one. Program-mode default ON;
        // {"residents": false} opts out. NOT persisted: NPCs live in memory like the
        // locations they reference — a reloaded world has neither (known follow-up).
        const bool spawnResidents = programMode && p.value("residents", true);
        if (spawnResidents) {
            const int rW = W, rD = D, rOx = ox, rOz = oz;
            units.push_back({"spawn residents", [chunkManager, placedObjectManager, objectTemplateManager, locationRegistry, npcManager, pushUndo, residentsJsonP, rW, rD, rOx, rOz]() {
                if (!npcManager || !locationRegistry) return;
                std::vector<Core::Location> locs;
                for (const auto& [id, loc] : locationRegistry->getAllLocations()) {
                    if (loc.position.x < rOx - 4 || loc.position.x > rOx + rW + 4 ||
                        loc.position.z < rOz - 4 || loc.position.z > rOz + rD + 4)
                        continue;
                    locs.push_back(loc);
                }
                auto plans = Core::ResidentPlanner::planResidents(locs);
                int spawned = 0;
                for (const auto& pl : plans) {
                    auto* npc = npcManager->spawnProceduralNPC(
                        pl.name, "resources/animated_characters/humanoid.anim",
                        pl.spawnPos + glm::vec3(0.0f, 1.0f, 0.0f),
                        Core::NPCBehaviorType::Scheduled, pl.role);
                    if (!npc) {
                        LOG_WARN_FMT("Settlement", "resident spawn FAILED: " << pl.name);
                        continue;
                    }
                    if (auto* sb = dynamic_cast<Scene::ScheduledBehavior*>(npc->getBehavior()))
                        sb->setSchedule(pl.schedule);
                    ++spawned;
                }
                LOG_INFO_FMT("Settlement", "residents: " << spawned << "/" << plans.size()
                             << " spawned");
                (*residentsJsonP) = {{"spawned", spawned}, {"planned", plans.size()}};
            }});
        }

        nlohmann::json programJson = nlohmann::json::object();
        if (programMode && tierP) {
            // Echo {era, tier, seed} so a live build is exactly reproducible (determinism contract).
            programJson = {{"era", era}, {"tier", tierName}, {"seed", seed},
                           {"morphology", tierP->morphology}};
            if (mainStreetMode) {
                programJson["main_street"] = {{"x", ox + msl.mainStreet.x}, {"z", oz + msl.mainStreet.z},
                                              {"w", msl.mainStreet.w}, {"d", msl.mainStreet.d}};
                programJson["dropped_plots"] = droppedPlots;
                if ((int)buildings.size() < tierP->buildingsMin)
                    programJson["below_tier_min"] = tierP->buildingsMin;   // surfaced, not silent
            }
        }
        const nlohmann::json settlementJson =
            {{"plots", layout.plots.size()}, {"streets", layout.streets.size()},
             {"buildings", buildings.size()}, {"origin", {{"x", ox}, {"y", oy}, {"z", oz}}}};

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
    res.program      = programJson;
    res.queuedBuilds = queued;
    res.jobLabel     = programMode
                           ? ("settlement " + era + "/" + tierName + " seed " + std::to_string(seed))
                           : std::string("settlement (legacy params)");
    res.units        = std::move(units);
    return res;
}

} // namespace Core
} // namespace Phyxel
