#include "core/StructureBuildService.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <vector>

#include "core/BuildingProgram.h"
#include "core/BuildingProgramValidator.h"
#include "core/ChunkManager.h"
#include "core/FurnitureCatalog.h"
#include "core/FurniturePlacer.h"
#include "core/LocationRegistry.h"
#include "core/NPCManager.h"
#include "core/ObjectTemplateManager.h"
#include "core/PlacedObjectManager.h"
#include "core/RealizedStructureValidator.h"
#include "core/RoomLayout.h"
#include "core/RoomProgram.h"
#include "core/StructureGenerator.h"
#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"
#include "utils/Logger.h"

namespace fs = std::filesystem;

namespace Phyxel {
namespace Core {

namespace {

// Template .metrics.json sidecar (footprints, hearth heights). Parse failures are
// treated as "no sidecar" — better to allow than to wrongly block.
nlohmann::json loadAssetMetricsSidecar(const std::string& templateName) {
    fs::path candidates[] = {
        fs::path("resources/templates") / (templateName + ".metrics.json"),
        fs::current_path() / "resources" / "templates" / (templateName + ".metrics.json"),
    };
    for (const auto& p : candidates) {
        if (!fs::exists(p)) continue;
        try {
            std::ifstream in(p);
            return nlohmann::json::parse(in);
        } catch (...) {
            return nullptr;
        }
    }
    return nullptr;
}

// [no-frozen-engine] phase timing — MEASURE before optimizing: the city L4 froze the main
// loop ~25 min across 28 synchronous builds; these numbers decide what gets regionalized,
// bulk-pathed, or sliced. lap() returns ms since the last lap and restarts the clock.
struct PhaseClock {
    std::chrono::steady_clock::time_point t = std::chrono::steady_clock::now();
    long long lap() {
        const auto now = std::chrono::steady_clock::now();
        const auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - t).count();
        t = now;
        return ms;
    }
};

struct PlaceOutcome {
    nlohmann::json response;
    std::string objectId;
    glm::ivec3 smin{INT_MAX, INT_MAX, INT_MAX}, smax{INT_MIN, INT_MIN, INT_MIN};
    int posX = 0, posY = 0, posZ = 0;
    bool ok = false;
    long long msPlace = 0, msGrass = 0, msNav = 0;   // phase timings (perf triage)
};

// Shared placement tail: undo snapshot (optional), place, honest-zero check,
// location + placed-object registration (+ assembly_plan metadata), grass
// clearing under the footprint, navgrid rebuild.
PlaceOutcome placeAndRegisterImpl(const StructureResult& structure, const nlohmann::json& params,
                                  const StructureBuildService::Deps& deps,
                                  const nlohmann::json& planMeta, bool doSnapshot) {
    PlaceOutcome out;
    auto* chunkManager = deps.chunkManager;
    auto* placedObjectManager = deps.placedObjects;
    auto* locationRegistry = deps.locations;

    for (const auto& v : structure.voxels) {
        out.smin = glm::min(out.smin, v.position);
        out.smax = glm::max(out.smax, v.position);
    }
    if (doSnapshot && deps.pushUndo)
        deps.pushUndo(out.smin, out.smax,
                      "build_structure:" + params.value("type", std::string("unknown")));

    PhaseClock pc;
    auto placement = StructureGenerator::place(chunkManager, structure);
    out.msPlace = pc.lap();

    if (placement.placed == 0) {
        // Honest failure: nothing landed. Do NOT register a wall-less "ghost"
        // (bbox + furniture with no voxels) or report success.
        out.response = {{"success", false},
                        {"error", "structure placement failed — 0 of " +
                                  std::to_string(structure.voxels.size()) + " voxels placed"},
                        {"placed", 0}, {"failed", placement.failed},
                        {"voxels_generated", structure.voxels.size()}};
        LOG_WARN_FMT("StructureBuild", "placement failed (0/" << structure.voxels.size()
                     << " placed) — not registering ghost");
        return out;
    }

    // Auto-register locations
    nlohmann::json locationsJson = nlohmann::json::array();
    if (locationRegistry) {
        auto locations = placement.locations;
        for (auto& loc : locations) {
            if (loc.id.empty()) {
                std::string stype = params.value("type", std::string("structure"));
                loc.id = stype + "_" + std::to_string(static_cast<int>(loc.position.x)) +
                         "_" + std::to_string(static_cast<int>(loc.position.z));
            }
            Location regLoc;
            regLoc.id = loc.id;
            regLoc.name = loc.name;
            regLoc.position = loc.position;
            regLoc.radius = loc.radius;
            regLoc.type = loc.type;
            locationRegistry->addLocation(regLoc);
            locationsJson.push_back({
                {"id", loc.id}, {"name", loc.name},
                {"position", {{"x", loc.position.x}, {"y", loc.position.y}, {"z", loc.position.z}}},
                {"radius", loc.radius}, {"type", Location::typeToString(loc.type)}
            });
        }
    }

    out.response = {{"success", true}, {"placed", placement.placed},
                    {"failed", placement.failed}, {"voxels_generated", structure.voxels.size()},
                    {"locations", locationsJson}};

    // Register with PlacedObjectManager for tracking
    if (placedObjectManager) {
        out.posX = params.contains("position") ? params["position"].value("x", out.smin.x) : out.smin.x;
        out.posY = params.contains("position") ? params["position"].value("y", out.smin.y) : out.smin.y;
        out.posZ = params.contains("position") ? params["position"].value("z", out.smin.z) : out.smin.z;
        std::string parentId = params.value("parent_id", std::string());
        out.objectId = placedObjectManager->registerStructure(
            params.value("type", std::string("structure")),
            glm::ivec3(out.posX, out.posY, out.posZ), 0, out.smin, out.smax, parentId);
        out.response["object_id"] = out.objectId;
        if (!out.objectId.empty() && !planMeta.is_null())
            placedObjectManager->setMetadata(out.objectId, "assembly_plan", planMeta);
        // NOT persisted here: records save WITH the chunks (save_world / scene save),
        // never eagerly — an eagerly-saved record whose voxels were never saved is a
        // GHOST on the next load (bbox + metadata pointing at empty terrain).

        // Clear grass under the whole footprint (V10 grass_under_house). prepare_pad
        // works on the program footprint and can miss perimeter/wall columns; sweep
        // the structure's ACTUAL bbox for terrain grass just below the floor and turn
        // it to Dirt so no grass blades emit under the building.
        if (!out.objectId.empty() && chunkManager) {
            pc.lap();
            auto isG = [](const std::string& m) {
                return m == "Grass" || m == "GrassForest" || m == "GrassSavanna";
            };
            std::vector<glm::ivec3> gcut;
            StructureResult gfill;
            for (int gx = out.smin.x; gx <= out.smax.x; ++gx)
                for (int gz = out.smin.z; gz <= out.smax.z; ++gz)
                    for (int gy = out.smin.y - 1; gy >= out.smin.y - 4; --gy) {
                        auto* gc = chunkManager->getCubeAt(glm::ivec3(gx, gy, gz));
                        if (gc && isG(gc->getMaterialName())) {
                            gcut.push_back(glm::ivec3(gx, gy, gz));
                            VoxelPlacement vp;
                            vp.position = glm::ivec3(gx, gy, gz);
                            vp.material = "Dirt";
                            vp.level    = VoxelLevel::Cube;
                            gfill.voxels.push_back(vp);
                        }
                    }
            if (!gcut.empty()) {
                StructureGenerator::removeVoxels(chunkManager, gcut);   // bulk-end rebuilds collision
                StructureGenerator::place(chunkManager, gfill);
            }
            out.msGrass = pc.lap();
        }
    }

    // NOTE: yard grading (V3 yard_not_flat) is intentionally NOT done here — correct
    // yard grading is SETTLEMENT-level terracing (see build_settlement), a per-building
    // flatten creates a cliff at the ring boundary (regressed fence_along_cliff 0->17).

    // Rebuild NavGrid for the affected region
    pc.lap();
    if (deps.npcs) deps.npcs->onRegionChanged(out.smin, out.smax);
    out.msNav = pc.lap();

    out.ok = true;
    return out;
}

} // namespace

nlohmann::json StructureBuildService::placeAndRegister(const StructureResult& structure,
                                                       const nlohmann::json& params,
                                                       const Deps& deps,
                                                       const nlohmann::json& planMeta) {
    if (!deps.chunkManager) return {{"error", "ChunkManager not available"}};
    if (structure.voxels.empty())
        return {{"error", "Failed to generate structure (unknown type or invalid params)"}};
    return placeAndRegisterImpl(structure, params, deps, planMeta, /*doSnapshot=*/true).response;
}

nlohmann::json StructureBuildService::aliasLegacyParams(const nlohmann::json& params) {
    const std::string type = params.value("type", std::string());
    if (type != "house" && type != "tavern") return nullptr;

    nlohmann::json v2 = nlohmann::json::object();
    v2["schema"] = "v2";
    v2["type"] = type;
    if (params.contains("position")) v2["position"] = params["position"];
    if (params.contains("parent_id")) v2["parent_id"] = params["parent_id"];
    if (params.contains("seed")) v2["seed"] = params["seed"];

    const int w = std::max(4, params.value("width",  type == "tavern" ? 8 : 7));
    const int d = std::max(4, params.value("depth",  type == "tavern" ? 7 : 6));
    v2["footprint"] = nlohmann::json::array({w, d});
    v2["function"] = (type == "tavern") ? "tavern" : "house";
    // Typology: taverns have a grounded room program; houses pick by scale (a small
    // request is a croft, a large one a hall house). Caller may override.
    v2["typology"] = params.value("typology",
        type == "tavern" ? std::string("tavern")
                         : (std::max(w, d) >= 9 ? std::string("hall_house") : std::string("croft")));
    v2["style"] = params.value("style", std::string("timber_cottage"));
    v2["substructure"] = params.value("substructure", std::string("crawlspace"));

    // v1 "stories" was an int; v2 wants an array of story objects.
    nlohmann::json stories = nlohmann::json::array();
    const int nStories = std::max(1, params.contains("stories") && params["stories"].is_number_integer()
                                         ? params["stories"].get<int>() : 1);
    for (int i = 0; i < nStories; ++i) stories.push_back({{"height", 3}});
    v2["stories"] = stories;
    return v2;
}

nlohmann::json StructureBuildService::buildV2(const nlohmann::json& params, const Deps& deps) {
    auto* chunkManager = deps.chunkManager;
    auto* placedObjectManager = deps.placedObjects;
    auto* objectTemplateManager = deps.templates;
    if (!chunkManager) return {{"error", "ChunkManager not available"}};

    nlohmann::json response = nlohmann::json::object();

    // [no-frozen-engine] per-phase ms; logged + returned so the async triage is data-driven.
    PhaseClock pc;
    long long msSetup = 0, msOverlap = 0, msRealize = 0, msPad = 0, msExcav = 0,
              msRegister = 0, msFixtures = 0;

    BuildingProgram program = BuildingProgram::fromJson(params);

    // Resolve the grounded room-program typology ONCE: it drives both the purposed
    // room layout (autofill) and the validation gate. Declared typology wins, else
    // a coarse function default (croft/hall_house/...).
    RoomProgramRegistry roomReg;
    roomReg.loadFromFile("resources/room_program.json");
    const std::string typ = program.typology.empty()
        ? RoomProgramRegistry::defaultTypologyForFunction(program.function)
        : program.typology;
    const RoomProgram* rp = typ.empty() ? nullptr : roomReg.get(typ);

    // generate_room_layout (#05): auto-fill interiors for any story that authored no
    // rooms. Deterministic in a seed derived from the build position (stable rebuilds).
    {
        unsigned seed = params.value("seed", 0u);
        if (seed == 0u && params.contains("position")) {
            int sx = params["position"].value("x", 0);
            int sz = params["position"].value("z", 0);
            seed = (static_cast<unsigned>(sx) * 73856093u) ^
                   (static_cast<unsigned>(sz) * 19349663u) ^ 0x9e3779b9u;
        }
        int emptyBefore = 0;
        for (const auto& st : program.stories) if (st.rooms.empty()) ++emptyBefore;
        const bool typologyApplied = autofillRoomLayout(program, seed ? seed : 1u, rp);
        if (emptyBefore > 0) {
            int total = 0;
            for (const auto& st : program.stories) total += (int)st.rooms.size();
            LOG_INFO_FMT("StructureBuild", "generate_room_layout: auto-filled "
                         << emptyBefore << " story(ies) -> " << total << " rooms total"
                         << (rp ? " [typology " + typ + "]" : " [generic]"));
            // Surface silent degradation: a typology was resolved but the footprint
            // couldn't fit it, so the ground floor is a generic box.
            if (rp && !typologyApplied) {
                const int need = 2 * (int)rp->rooms.size();
                LOG_WARN_FMT("StructureBuild", "typology " << typ << " did NOT fit footprint "
                             << program.footprintW << "x" << program.footprintD
                             << " (need long axis >= " << need
                             << ") — ground floor used a GENERIC layout (no purposed rooms)");
                response["typology_unfit"] = {
                    {"typology", typ}, {"need_long_axis", need},
                    {"footprint", {program.footprintW, program.footprintD}}};
            }
        }
    }
    StyleProfileRegistry styleReg;
    styleReg.loadFromFile("resources/structure_styles.json");
    const StyleProfile* sp = styleReg.get(program.style);
    StyleProfile style = sp ? *sp : StyleProfile{};

    // Pre-build validation gate (WARN-BUT-ALLOW): grounded checks + the room-program
    // typology gate. Logged loudly; the build still proceeds.
    {
        ValidationReport vr = BuildingProgramValidator::validate(program, {}, rp);
        if (vr.ok())
            LOG_INFO_FMT("StructureBuild", "program validation: OK"
                         << (rp ? " [typology " + typ + "]" : ""));
        else
            LOG_WARN_FMT("StructureBuild", "program validation FAILED (warn-but-allow,"
                         " building anyway): " << vr.summary());
    }

    int ox = 0, oz = 0, reqY = 16;
    if (params.contains("position")) {
        ox   = params["position"].value("x", 0);
        oz   = params["position"].value("z", 0);
        reqY = params["position"].value("y", 16);
    }

    msSetup = pc.lap();
    // CONTEXT-AWARE PLACEMENT: remove any existing structure whose footprint overlaps
    // this one BEFORE seating. Without this, a rebuild stacks: terrain seating samples
    // the old structure's voxels as "ground" and seats the new one on top of it.
    if (placedObjectManager) {
        const int fw = std::max(program.footprintW, 1);
        const int fd = std::max(program.footprintD, 1);
        for (const auto& obj : placedObjectManager->list()) {
            if (obj.category != "structure") continue;
            const bool overlapXZ =
                obj.boundingMin.x <= ox + fw - 1 && obj.boundingMax.x >= ox &&
                obj.boundingMin.z <= oz + fd - 1 && obj.boundingMax.z >= oz;
            if (overlapXZ) {
                LOG_INFO_FMT("StructureBuild", "removing overlapping structure '"
                             << obj.id << "' before rebuild (no stacking)");
                placedObjectManager->remove(obj.id);
            }
        }
    }

    msOverlap = pc.lap();
    auto shell = StructureRealizer::realizeShell(program, style);
    if (!shell.ok) return {{"error", "realize failed: " + shell.error}};
    msRealize = pc.lap();

    // prepare_pad (#2): LEVEL the bumpy terrain under the footprint to a flat build
    // pad — cut the high side, fill the low side to the median grade — then seat the
    // foundation on it. Replaces bare median-seating.
    int W = std::max(program.footprintW, 1), D = std::max(program.footprintD, 1);
    int oy = reqY;
    {
        std::vector<int> tops;
        std::vector<glm::ivec2> cells;
        const int scanTop = reqY + 64;
        for (int x = ox; x < ox + W; ++x)
            for (int z = oz; z < oz + D; ++z) {
                int top = -1;
                for (int y = scanTop; y >= 0; --y)
                    if (chunkManager->hasVoxelAt(glm::ivec3(x, y, z))) { top = y; break; }
                if (top >= 0) { tops.push_back(top); cells.push_back(glm::ivec2(x, z)); }
            }
        if (!tops.empty()) {
            const int padLevel = StructureGenerator::planPadLevel(tops);
            std::vector<glm::ivec3> cut;          // terrain above the pad -> remove
            StructureResult fill;                 // terrain below the pad -> add (Dirt)
            for (size_t i = 0; i < tops.size(); ++i) {
                const glm::ivec2 cl = cells[i];
                for (int y = padLevel + 1; y <= tops[i]; ++y)
                    cut.push_back(glm::ivec3(cl.x, y, cl.y));
                for (int y = tops[i] + 1; y <= padLevel; ++y) {
                    VoxelPlacement vp;
                    vp.position = glm::ivec3(cl.x, y, cl.y);
                    vp.material = "Dirt";
                    vp.level    = VoxelLevel::Cube;
                    fill.voxels.push_back(vp);
                }
                // The pad must be BARE EARTH under the floor (V10 grass_under_house):
                // replace the pad-top cube and any buried grass surface with Dirt.
                auto clearToDirt = [&](int yy) {
                    cut.push_back(glm::ivec3(cl.x, yy, cl.y));
                    VoxelPlacement pv;
                    pv.position = glm::ivec3(cl.x, yy, cl.y);
                    pv.material = "Dirt";
                    pv.level    = VoxelLevel::Cube;
                    fill.voxels.push_back(pv);
                };
                clearToDirt(padLevel);
                if (tops[i] < padLevel) clearToDirt(tops[i]);
            }
            // No explicit physics rebuild: removeVoxels/place END their bulk ops with
            // buildInitialCollisionShapes per touched chunk — the SAME rebuild an explicit
            // buildChunkPhysicsInRegion would repeat (measured: the redundant pass cost
            // 18-61 s per building at settlement scale).
            if (!cut.empty())         StructureGenerator::removeVoxels(chunkManager, cut);
            if (!fill.voxels.empty()) StructureGenerator::place(chunkManager, fill);
            oy = padLevel + 1;        // foundation bottom rests on the flat pad
            LOG_INFO_FMT("StructureBuild", "prepare_pad: leveled footprint to y=" << padLevel
                         << " (cut " << cut.size() << ", fill " << fill.voxels.size() << ")");

            // excavate_basement (#34): a basement seats BELOW grade — the ground floor
            // lands at the surface and the cellar is DUG OUT beneath it. The realizer's
            // foundation ring becomes the retaining walls; the ground-floor slab is the
            // cellar ceiling.
            if (program.substructure == "basement" && shell.crawlHeightCubes > 0) {
                const int depth = shell.crawlHeightCubes;     // cellar height (cubes)
                oy = padLevel + 1 - depth;                    // ground floor stays at grade
                std::vector<glm::ivec3> dig;
                for (int x = ox; x < ox + W; ++x)
                    for (int z = oz; z < oz + D; ++z)
                        for (int y = oy; y <= padLevel; ++y)
                            dig.push_back(glm::ivec3(x, y, z));
                StructureGenerator::removeVoxels(chunkManager, dig);   // bulk-end rebuilds collision
                LOG_INFO_FMT("StructureBuild", "excavate_basement: dug cellar " << depth
                             << " cubes below grade (" << dig.size() << " voxels)");
            }
        }
    }
    msPad = pc.lap();
    StructureResult structure = StructureRealizer::toStructureResult(shell, glm::ivec3(ox, oy, oz));
    if (structure.voxels.empty())
        return {{"error", "Failed to generate structure (unknown type or invalid params)"}};

    // Persist the assembly plan with its placement origin: featureAt(local) + origin =
    // a post-build structural-feature query (wall/floor/ceiling/...) that no consumer
    // has to re-derive from voxel materials.
    const nlohmann::json planMeta = {{"origin", {ox, oy, oz}}, {"plan", shell.plan.toJson()}};

    // Fixture-pass context: the floor sits one cube above the foundation top.
    const int floorY = oy + shell.crawlHeightCubes;
    std::vector<int> floorYByStory;         // legacy cube Y per story (back-compat)
    std::vector<int> surfaceMicroYByStory;  // EXACT walkable surface micro-Y per story
    for (int ft : shell.floorTopByStory) {
        floorYByStory.push_back(oy + ft / 9);
        surfaceMicroYByStory.push_back(oy * 9 + ft);
    }
    // Exterior-wall thickness in micro — MUST equal what the REALIZER built (its
    // converter CLAMPS to [1,9]), not the raw style value: a stone_keep authors 3.0 m
    // and an unclamped 27-micro inset pushes furniture out of narrow rooms (dropped).
    const int extTMicro = StructureRealizer::thicknessMicro(
        style.thicknessOf("exterior_wall", 0.333));
    // Roof apex (world micro) for place_chimney (#14): the stack must clear it.
    int roofApexWorldMicro = 0;
    {
        glm::ivec3 cLo, cHi;
        if (shell.canvas.microBounds(cLo, cHi))
            roofApexWorldMicro = oy * 9 + cHi.y;
        else
            LOG_WARN("StructureBuild", "place_chimney: canvas microBounds failed -> "
                     "roof apex unknown; chimneys will be SKIPPED for this build");
    }

    // Snapshot BEFORE the excavation below so undo restores the pre-build terrain.
    glm::ivec3 smin(INT_MAX), smax(INT_MIN);
    for (const auto& v : structure.voxels) {
        smin = glm::min(smin, v.position);
        smax = glm::max(smax, v.position);
    }
    if (deps.pushUndo)
        deps.pushUndo(smin, smax, "build_structure:" + params.value("type", std::string("v2")));

    // P2 excavation: clear exactly the structure's cube cells so its voxels can't fail
    // against pre-existing terrain / other structures. Surgical (the building's own
    // footprint, not a bbox); every cell is at y >= oy = grade+1 so this never removes
    // the ground the building rests on.
    {
        std::vector<glm::ivec3> cells;
        cells.reserve(structure.voxels.size());
        for (const auto& v : structure.voxels) cells.push_back(v.position);
        StructureGenerator::removeVoxels(chunkManager, cells);
    }

    msExcav = pc.lap();
    PlaceOutcome out = placeAndRegisterImpl(structure, params, deps, planMeta, /*doSnapshot=*/false);
    msRegister = pc.lap();
    // Merge pre-build fields (typology_unfit) into the outcome response.
    for (auto it = response.begin(); it != response.end(); ++it) out.response[it.key()] = it.value();
    response = out.response;
    if (!out.ok) return response;
    const std::string objectId = out.objectId;
    const int posX = out.posX, posZ = out.posZ;

    // ------------------------------------------------------------------------
    // v2: the ENGINE decides furniture placement. FurniturePlacer derives
    // what/where/facing/clearance from each room's purpose + door positions —
    // hand-authored program fixtures are IGNORED. Pieces are parented to the
    // structure so they group and are removed with it.
    // ------------------------------------------------------------------------
    if (placedObjectManager && !objectId.empty()) {
        // Up-front coverage gate: flag furniture a room NEEDS but the catalog can't
        // supply — surfaced by ROOM, not silently dropped.
        auto templateLoaded = [&](const std::string& n) {
            return objectTemplateManager && objectTemplateManager->getTemplate(n) != nullptr;
        };
        auto coverage = validateFurnitureCoverage(templateLoaded);
        if (!coverage.ok()) {
            nlohmann::json gaps = nlohmann::json::array();
            for (const auto& g : coverage.gaps) {
                gaps.push_back({{"purpose", g.purpose}, {"type", g.type},
                                {"template", g.templateName}, {"message", g.message}});
                LOG_WARN("StructureBuild", "asset gap: " + g.message);
            }
            response["asset_gaps"] = gaps;
        }

        // Footprint-aware placement: each fixture type's real cube footprint from its
        // template's ACTUAL occupied cubes (metrics overall_max is transposed vs the
        // voxels; the rectangle from real extents keeps reservation == render).
        std::map<std::string, Footprint> fixtureFootprints;
        for (const auto& type : FurnitureCatalog::mappedTypes()) {
            const std::string tmpl = FurnitureCatalog::templateFor(type);
            if (tmpl.empty()) continue;
            Footprint fp;
            bool got = false;
            const auto* t = objectTemplateManager ? objectTemplateManager->getTemplate(tmpl)
                                                  : nullptr;
            if (t) {
                int mnx = INT_MAX, mnz = INT_MAX, mxx = INT_MIN, mxz = INT_MIN;
                int uMaxX = 0, uMaxZ = 0, uMaxY = 0;   // max MICRO index (true placed span + height)
                auto acc = [&](const glm::ivec3& cube, int microX, int microZ, int microY) {
                    mnx = std::min(mnx, cube.x); mxx = std::max(mxx, cube.x);
                    mnz = std::min(mnz, cube.z); mxz = std::max(mxz, cube.z);
                    uMaxX = std::max(uMaxX, microX); uMaxZ = std::max(uMaxZ, microZ);
                    uMaxY = std::max(uMaxY, microY);
                };
                for (const auto& c : t->cubes)
                    acc(c.relativePos, c.relativePos.x * 9 + 8, c.relativePos.z * 9 + 8,
                        c.relativePos.y * 9 + 8);
                for (const auto& s : t->subcubes)
                    acc(s.parentRelativePos,
                        s.parentRelativePos.x * 9 + s.subcubePos.x * 3 + 2,
                        s.parentRelativePos.z * 9 + s.subcubePos.z * 3 + 2,
                        s.parentRelativePos.y * 9 + s.subcubePos.y * 3 + 2);
                for (const auto& mc : t->microcubes)
                    acc(mc.parentRelativePos,
                        mc.parentRelativePos.x * 9 + mc.subcubePos.x * 3 + mc.microcubePos.x,
                        mc.parentRelativePos.z * 9 + mc.subcubePos.z * 3 + mc.microcubePos.z,
                        mc.parentRelativePos.y * 9 + mc.subcubePos.y * 3 + mc.microcubePos.y);
                if (mxx >= mnx) {
                    fp.width = mxx - mnx + 1;
                    fp.depth = mxz - mnz + 1;
                    fp.microW = uMaxX;   // real micro extents (0-anchored templates)
                    fp.microD = uMaxZ;
                    fp.microH = uMaxY + 1;   // micro HEIGHT (ceiling hang needs it)
                    got = true;
                }
            }
            if (!got) {   // fallback: metrics sidecar
                nlohmann::json m = loadAssetMetricsSidecar(tmpl);
                if (m.is_object() && m.contains("overall_max") &&
                    m["overall_max"].is_array() && m["overall_max"].size() >= 3) {
                    const double ex = m["overall_max"][0].get<double>();
                    const double ey = m["overall_max"][1].get<double>();
                    const double ez = m["overall_max"][2].get<double>();
                    fp = footprintFromExtents(ex, ez);
                    fp.microH = std::max(1, (int)std::lround(std::ceil(ey * 9.0)));
                    got = true;
                }
            }
            if (got) fixtureFootprints[type] = fp;
        }
        {
            auto bedIt = fixtureFootprints.find("bed");
            LOG_INFO_FMT("StructureBuild", "footprint-aware: loaded "
                         << fixtureFootprints.size() << " fixture footprints"
                         << (bedIt != fixtureFootprints.end()
                             ? " (bed=" + std::to_string(bedIt->second.width) + "x"
                               + std::to_string(bedIt->second.depth) + ")" : ""));
        }

        int fxSpawned = 0, fxSkipped = 0;
        std::vector<UnplacedFixture> unplaced;  // honest: pieces that didn't fit
        nlohmann::json fixturesJson = nlohmann::json::array();
        // Furniture quality B: data recipes (tier-filtered) + the typology's wealth tier.
        // Idempotent load; unknown purposes still fall back to the hardcoded map. A FAILED
        // load is surfaced loudly (auditor finding): the hardcoded fallback has no tiers and
        // no wall_lantern/chandelier, so silence here would quietly strip quality-B fixtures.
        const bool recipesLoaded =
            FurniturePlacer::loadRecipesFromFile("resources/furnishing_recipes.json");
        if (!recipesLoaded)
            LOG_WARN_FMT("StructureBuild",
                         "furnishing_recipes.json failed to load — falling back to the "
                         "hardcoded recipe map (no wealth tiers, no mounted fixtures)");
        response["furnishing_recipes_loaded"] = recipesLoaded;
        const std::string wealthTier = rp ? rp->wealthTier : "";
        for (size_t si = 0; si < program.stories.size(); ++si) {
            const auto& story = program.stories[si];
            // KI-2: per-story floor Y — else all furniture stacks on the ground floor.
            int storyFloorY = (si < floorYByStory.size()) ? floorYByStory[si] : floorY;
            auto placements = FurniturePlacer::furnish(
                story, glm::ivec3(posX, 0, posZ), storyFloorY, fixtureFootprints,
                &unplaced, extTMicro,    // extTMicro -> reserve the TRUE placed span
                wealthTier);
            // Semantic identity per fixture (room/purpose/ordinal/type), 1:1 with
            // placements — so a session can address "the 2nd bedroom's bed".
            auto labels = FurniturePlacer::labelFixtures(story, placements);
            for (size_t k = 0; k < placements.size(); ++k) {
                const auto& pl = placements[k];
                std::string tmpl = FurnitureCatalog::templateFor(pl.type);
                if (tmpl.empty()) { ++fxSkipped; continue; }
                // MICRO-PRECISE: inset off the wall + sit on the exact walkable surface —
                // except MOUNTED fixtures: a sconce hangs at the grounded 60 in wall height,
                // a chandelier below the ceiling with head clearance (mountedMicroY).
                const int surfMicroY = (si < surfaceMicroYByStory.size())
                    ? surfaceMicroYByStory[si] : storyFloorY * 9;
                const int ceilMicroY = surfMicroY + story.height * 9;
                auto fpIt2 = fixtureFootprints.find(pl.type);
                const int tmplMicroH = (fpIt2 != fixtureFootprints.end() &&
                                        fpIt2->second.microH > 0)
                    ? fpIt2->second.microH : 9;
                const int baseMicroY = FurniturePlacer::mountedMicroY(
                    pl.type, surfMicroY, ceilMicroY, tmplMicroH);
                const glm::ivec3 microPos =
                    FurniturePlacer::microWorldPos(pl, extTMicro, baseMicroY);
                std::string fid = placedObjectManager->placeTemplateMicro(
                    tmpl, microPos, pl.rotation, objectId);
                if (fid.empty()) { ++fxSkipped; continue; }
                ++fxSpawned;
                const auto& L = labels[k];
                nlohmann::json fx = {
                    {"structure", objectId}, {"room", L.room},
                    {"purpose", L.purpose}, {"purpose_index", L.purposeIndex},
                    {"type", L.type}, {"story", (int)si}};
                // Tag the placed object so the identity survives (persisted).
                placedObjectManager->setMetadata(fid, "fixture", fx);
                fx["id"] = fid;
                fx["position"] = {{"x", pl.worldPos.x}, {"y", pl.worldPos.y},
                                  {"z", pl.worldPos.z}};
                fx["rotation"] = pl.rotation;
                fixturesJson.push_back(fx);

                // place_chimney (#14): run a masonry stack from this VENTED hearth
                // up through the roof, clearing the ridge for draught (>= 2 ft).
                if ((pl.type == "fireplace" || pl.type == "forge_hearth" ||
                     pl.type == "oven_bread") &&
                    roofApexWorldMicro > 0) {
                    Footprint cfp;
                    auto fpIt = fixtureFootprints.find(pl.type);
                    if (fpIt != fixtureFootprints.end()) cfp = fpIt->second;
                    // Center the stack on the hearth's ACTUAL placed footprint (its
                    // registered world bbox) so a ROTATED hearth still gets its
                    // chimney directly overhead (V8 chimney_offset_from_hearth).
                    int ccx = microPos.x + std::max(1, cfp.width) * 9 / 2;
                    int ccz = microPos.z + std::max(1, cfp.depth) * 9 / 2;
                    if (const auto* hobj = placedObjectManager->get(fid)) {
                        ccx = (hobj->boundingMin.x + hobj->boundingMax.x + 1) * 9 / 2;
                        ccz = (hobj->boundingMin.z + hobj->boundingMax.z + 1) * 9 / 2;
                    }
                    // The stack RESTS ON the hearth top (mantel), not from the floor
                    // (V2 checkChimneyOnHearth). Hearth height from its .metrics.json.
                    int hearthH = 9;
                    {
                        nlohmann::json hm = loadAssetMetricsSidecar(
                            FurnitureCatalog::templateFor(pl.type));
                        if (hm.is_object() && hm.contains("overall_max") &&
                            hm["overall_max"].is_array() && hm["overall_max"].size() >= 2)
                            hearthH = std::max(1, (int)std::lround(
                                hm["overall_max"][1].get<double>() * 9.0));
                    }
                    const int baseY = microPos.y + hearthH;   // sit on the hearth top
                    // ridge clearance >= 2 ft (IRC R1003.9 / 3-2-10, shared constant)
                    const int topY = roofApexWorldMicro +
                        StructureGenerator::kChimneyRidgeClearanceMicro;
                    if (topY > baseY) {
                        auto chimney = StructureGenerator::planChimneyStack(
                            ccx, ccz, baseY, topY, "Bricks");
                        StructureGenerator::place(chunkManager, chimney);
                    }
                }
            }
            // Surface clutter: scatter mugs/bottles ON table tops. Deterministic per
            // table position so a rebuild is stable. Table top ~= floor + 1 cube.
            for (const auto& pl : placements) {
                if (pl.type.find("table") == std::string::npos) continue;
                Footprint fp = fixtureFootprints.count(pl.type)
                    ? fixtureFootprints[pl.type] : Footprint{1, 1};
                Rect surf{pl.worldPos.x, pl.worldPos.z, fp.width, fp.depth};
                unsigned cseed =
                    (static_cast<unsigned>(pl.worldPos.x) * 73856093u) ^
                    (static_cast<unsigned>(pl.worldPos.z) * 19349663u) ^ 0x9e3779b9u;
                auto clutter = FurniturePlacer::placeSurfaceClutter(
                    pl.room, surf, storyFloorY + 1, {"mug", "mug", "bottle"}, cseed);
                for (const auto& c : clutter) {
                    std::string ct = FurnitureCatalog::templateFor(c.type);
                    if (ct.empty()) continue;
                    if (!placedObjectManager->placeTemplate(
                            ct, c.worldPos, 0, objectId, /*snap=*/false).empty())
                        ++fxSpawned;
                }
            }
        }

        // place_signage (#47): hang a projecting trade sign over a BUSINESS's entrance
        // (board + wrought-iron bracket; the board IMAGE is the decal-system backlog).
        auto isBusiness = [](const std::string& typN, const std::string& fn) {
            return typN == "tavern" || typN == "blacksmith" || typN == "smithy" ||
                   typN == "inn" || typN == "general_store" || typN == "market" ||
                   typN == "apothecary" || typN == "butcher" || typN == "bakery" ||
                   fn == "shop" || fn == "tavern" || fn == "inn" || fn == "market" ||
                   fn == "bakery" || fn == "store" || fn == "smithy" ||
                   fn == "apothecary" || fn == "herbalist" || fn == "butcher";
        };
        const bool haveSign = objectTemplateManager &&
            objectTemplateManager->getTemplate("hanging_sign") != nullptr;
        if (haveSign && !program.stories.empty() &&
            isBusiness(program.typology, program.function)) {
            const int Wp = std::max(program.footprintW, 1);
            const int Dp = std::max(program.footprintD, 1);
            const int floorMicroY = !surfaceMicroYByStory.empty()
                ? surfaceMicroYByStory[0] : floorY * 9;
            // ground-floor exterior entry door (first = main entrance)
            const ProgStory& g = program.stories[0];
            const ProgPortal* door = nullptr;
            for (const auto& p : g.portals) {
                if (p.kind != "door") continue;
                if (p.a != "exterior" && p.b != "exterior") continue;
                door = &p; break;
            }
            // wall side -> rotation (asset front=+Z; rot maps front to the outward
            // normal), the wall's OUTER face micro-coord, the along-wall door center,
            // and the door head height.
            int rotation = 180;                       // default: -Z front wall
            int wallOuterMicro = oz * 9;              // -Z outer face
            int alongCenterMicro = (ox * 9) + (Wp * 9) / 2;
            int doorHeadMicroY = floorMicroY + 3 * 9; // default door height 3 cubes
            if (door) {
                doorHeadMicroY = floorMicroY + std::max(1, door->height) * 9;
                const int dw = std::max(1, door->width);
                if (door->px == 0) {              // -X wall
                    rotation = 90;
                    wallOuterMicro = ox * 9;
                    alongCenterMicro = (oz + door->pz) * 9 + dw * 9 / 2;
                } else if (door->px == Wp) {      // +X wall
                    rotation = 270;
                    wallOuterMicro = (ox + Wp) * 9;
                    alongCenterMicro = (oz + door->pz) * 9 + dw * 9 / 2;
                } else if (door->pz == Dp) {      // +Z wall
                    rotation = 0;
                    wallOuterMicro = (oz + Dp) * 9;
                    alongCenterMicro = (ox + door->px) * 9 + dw * 9 / 2;
                } else {                          // -Z wall (pz==0 or interior fallback)
                    rotation = 180;
                    wallOuterMicro = oz * 9;
                    alongCenterMicro = (ox + door->px) * 9 + dw * 9 / 2;
                }
            }
            const int SIGN_H = 7, PROJ = 7;   // asset y-extent + board projection (micro)
            // clear BOTH the 8 ft grade floor AND the door head; clamp under the apex.
            const int minBottom = std::max(floorMicroY + 22, doorHeadMicroY + 1);
            int boardBottom = minBottom;
            std::string skipReason;
            if (roofApexWorldMicro > 0 && boardBottom + SIGN_H > roofApexWorldMicro) {
                boardBottom = roofApexWorldMicro - SIGN_H;   // tuck under the eave
                if (boardBottom < minBottom)
                    skipReason = "no room above the door head under the eave "
                                 "(roof apex too low for a clearing sign)";
            }
            // VALIDATOR AS A GATE: the sign is placed ONLY if it clears head height,
            // stays within the projection cap, AND hangs above the lintel.
            ValidationReport sc;
            if (skipReason.empty()) {
                sc = RealizedStructureValidator::checkSignClearance(
                    boardBottom, floorMicroY, PROJ, /*minClear*/22,
                    /*maxProj*/11, /*doorHead*/doorHeadMicroY);
                if (!sc.ok()) skipReason = sc.summary();
            }
            if (!skipReason.empty()) {
                LOG_WARN("StructureBuild", "place_signage: SKIPPED — " + skipReason);
                response["signage_skipped"] = skipReason;
            } else {
                // min-corner of the rotated AABB so the bracket foot is flush on the
                // wall outer face and the board projects OUTWARD.
                glm::ivec3 sm(0, boardBottom, 0);
                switch (rotation) {
                    case 0:   sm.x = alongCenterMicro; sm.z = wallOuterMicro;     break; // +Z
                    case 180: sm.x = alongCenterMicro; sm.z = wallOuterMicro - 6; break; // -Z
                    case 270: sm.x = wallOuterMicro;   sm.z = alongCenterMicro;   break; // +X
                    case 90:  sm.x = wallOuterMicro - 6; sm.z = alongCenterMicro; break; // -X
                    default: break;
                }
                std::string sid = placedObjectManager->placeTemplateMicro(
                    "hanging_sign", sm, rotation, objectId);
                if (!sid.empty()) {
                    ++fxSpawned;
                    nlohmann::json sj = {
                        {"id", sid}, {"structure", objectId},
                        {"rotation", rotation},
                        {"board_bottom_micro_y", boardBottom},
                        {"clearance_micro", boardBottom - floorMicroY},
                        {"above_lintel_micro", boardBottom - doorHeadMicroY},
                        {"projection_micro", PROJ},
                        {"over_door", door != nullptr},
                        {"clearance_ok", true}};   // gated: only reached when ok
                    placedObjectManager->setMetadata(sid, "signage", sj);
                    response["signage"] = sj;
                    LOG_INFO_FMT("StructureBuild", "place_signage: hung sign over "
                                 << (door ? "entry door" : "front wall")
                                 << " (clearance " << (boardBottom - floorMicroY)
                                 << " micro, above lintel " << (boardBottom - doorHeadMicroY)
                                 << " micro, rot " << rotation << ")");
                } else {
                    LOG_WARN("StructureBuild", "place_signage: placeTemplateMicro failed");
                }
            }
        }
        response["fixtures_spawned"] = fxSpawned;
        response["fixtures"] = fixturesJson;   // labeled, addressable
        // Honest reporting: pieces the placer could NOT fit (never a silent drop).
        if (!unplaced.empty()) {
            nlohmann::json unfit = nlohmann::json::array();
            for (const auto& u : unplaced) {
                unfit.push_back({{"room", u.room}, {"type", u.type}});
                LOG_WARN_FMT("StructureBuild", "furniture did NOT fit: " << u.type
                             << " in room '" << u.room << "'");
            }
            response["fixtures_unplaced"] = unfit;
        }
        // Metadata tags persist with the next world save (atomic with the voxels).
        msFixtures = pc.lap();
        LOG_INFO_FMT("StructureBuild", "FurniturePlacer: engine placed " << fxSpawned
                     << " fixtures (" << fxSkipped << " skipped) into '" << objectId << "'");
    }

    // [no-frozen-engine] the phase distribution this build actually spent (main-thread ms).
    const long long msTotal = msSetup + msOverlap + msRealize + msPad + msExcav + msRegister +
                              msFixtures;
    LOG_INFO_FMT("StructureBuild", "[perf] phases ms: setup=" << msSetup << " overlap=" << msOverlap
                 << " realize=" << msRealize << " pad=" << msPad << " excav=" << msExcav
                 << " place+register=" << msRegister << " (place=" << out.msPlace << " grass="
                 << out.msGrass << " nav=" << out.msNav << ") fixtures=" << msFixtures
                 << " TOTAL=" << msTotal);
    response["timings_ms"] = {{"setup", msSetup}, {"overlap", msOverlap}, {"realize", msRealize},
                              {"pad", msPad}, {"excav", msExcav}, {"register", msRegister},
                              {"place", out.msPlace}, {"grass", out.msGrass}, {"nav", out.msNav},
                              {"fixtures", msFixtures}, {"total", msTotal}};
    return response;
}

} // namespace Core
} // namespace Phyxel
