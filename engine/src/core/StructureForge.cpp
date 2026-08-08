#include "core/StructureForge.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <map>
#include <set>
#include <vector>

#include "StructureBuildDetail.h"

#include "core/BuildingProgram.h"
#include "core/BuildingProgramValidator.h"
#include "core/ChunkManager.h"
#include "core/DamageSystem.h"
#include "core/FurnitureCatalog.h"
#include "core/FurniturePlacer.h"
#include "core/ItemPropManager.h"
#include "core/ObjectTemplateManager.h"
#include "core/PlacedObjectManager.h"
#include "core/RealizedStructureValidator.h"
#include "core/RoomLayout.h"
#include "core/RoomProgram.h"
#include "core/StructureGenerator.h"
#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"
#include "utils/Logger.h"

namespace Phyxel {
namespace Core {

using detail::PhaseClock;
using detail::PlaceOutcome;

// The one artifact threaded through the stages. Every member is a former
// buildV2 local, promoted verbatim; stages read/write exactly what the
// corresponding monolith block did.
struct StructureForge::Context {
    const nlohmann::json& params;
    const StructureBuildService::Deps& deps;
    nlohmann::json response = nlohmann::json::object();

    // intake ->
    BuildingProgram program;
    RoomProgramRegistry roomReg;          // owns *rp
    std::string typ;
    const RoomProgram* rp = nullptr;
    StyleProfile style;
    int ox = 0, oz = 0, reqY = 16;

    // realize ->
    StructureRealizer::ShellResult shell;

    // place ->
    int oy = 16;
    StructureResult structure;
    nlohmann::json planMeta;
    int floorY = 0;
    std::vector<int> floorYByStory;        // legacy cube Y per story (back-compat)
    std::vector<int> surfaceMicroYByStory; // EXACT walkable surface micro-Y per story
    int extTMicro = 0;
    int roofApexWorldMicro = 0;
    PlaceOutcome out;
    std::string objectId;
    int posX = 0, posZ = 0;

    // [no-frozen-engine] per-phase ms; logged + returned so the async triage is data-driven.
    PhaseClock pc;
    long long msSetup = 0, msOverlap = 0, msRealize = 0, msPad = 0, msExcav = 0,
              msRegister = 0, msFixtures = 0;
};

const std::vector<std::string>& StructureForge::stageNames() {
    static const std::vector<std::string> kNames = {
        "intake", "floorplan", "validate_program", "footprint", "realize",
        "validate_realized", "place", "furnish", "emit"};
    return kNames;
}

nlohmann::json StructureForge::run(const nlohmann::json& params,
                                   const StructureBuildService::Deps& deps) {
    Context ctx{params, deps};

    using StageFn = StageReport (*)(Context&);
    static const std::pair<const char*, StageFn> kStages[] = {
        {"intake",            &StructureForge::stageIntake},
        {"floorplan",         &StructureForge::stageFloorplan},
        {"validate_program",  &StructureForge::stageValidateProgram},
        {"footprint",         &StructureForge::stageFootprint},
        {"realize",           &StructureForge::stageRealize},
        {"validate_realized", &StructureForge::stageValidateRealized},
        {"place",             &StructureForge::stagePlace},
        {"furnish",           &StructureForge::stageFurnish},
        {"emit",              &StructureForge::stageEmit},
    };

    nlohmann::json gates = nlohmann::json::array();
    PhaseClock stageClock;
    for (const auto& [name, fn] : kStages) {
        StageReport r = fn(ctx);
        const char* outcome = r.action == StageReport::Action::Refused   ? "refused"
                            : r.action == StageReport::Action::Repaired  ? "repaired"
                                                                         : "proceeded";
        gates.push_back({{"stage", name}, {"outcome", outcome}, {"ms", stageClock.lap()}});
        if (r.action == StageReport::Action::Refused) {
            // Refusal jsons are returned VERBATIM (same error shapes the monolith
            // produced) + the gates so far for diagnosability.
            nlohmann::json refusal = std::move(r.refusal);
            refusal["gates"] = gates;
            return refusal;
        }
    }
    ctx.response["gates"] = gates;
    return ctx.response;
}

// ---------------------------------------------------------------------------
// intake — parse BuildingProgram, resolve typology + style, parse position.
// ---------------------------------------------------------------------------
StructureForge::StageReport StructureForge::stageIntake(Context& ctx) {
    StageReport rep;
    if (!ctx.deps.chunkManager) {
        rep.action = StageReport::Action::Refused;
        rep.refusal = {{"error", "ChunkManager not available"}};
        return rep;
    }

    ctx.program = BuildingProgram::fromJson(ctx.params);

    // Resolve the grounded room-program typology ONCE: it drives both the purposed
    // room layout (autofill) and the validation gate. Declared typology wins, else
    // a coarse function default (croft/hall_house/...).
    ctx.roomReg.loadFromFile("resources/room_program.json");
    ctx.typ = ctx.program.typology.empty()
        ? RoomProgramRegistry::defaultTypologyForFunction(ctx.program.function)
        : ctx.program.typology;
    ctx.rp = ctx.typ.empty() ? nullptr : ctx.roomReg.get(ctx.typ);

    StyleProfileRegistry styleReg;
    styleReg.loadFromFile("resources/structure_styles.json");
    const StyleProfile* sp = styleReg.get(ctx.program.style);
    ctx.style = sp ? *sp : StyleProfile{};

    if (ctx.params.contains("position")) {
        ctx.ox   = ctx.params["position"].value("x", 0);
        ctx.oz   = ctx.params["position"].value("z", 0);
        ctx.reqY = ctx.params["position"].value("y", 16);
    }
    return rep;
}

// ---------------------------------------------------------------------------
// floorplan — generate_room_layout (#05): auto-fill interiors for any story that
// authored no rooms. Deterministic in a seed derived from the build position.
// ---------------------------------------------------------------------------
StructureForge::StageReport StructureForge::stageFloorplan(Context& ctx) {
    StageReport rep;
    unsigned seed = ctx.params.value("seed", 0u);
    if (seed == 0u && ctx.params.contains("position")) {
        int sx = ctx.params["position"].value("x", 0);
        int sz = ctx.params["position"].value("z", 0);
        seed = (static_cast<unsigned>(sx) * 73856093u) ^
               (static_cast<unsigned>(sz) * 19349663u) ^ 0x9e3779b9u;
    }
    int emptyBefore = 0;
    for (const auto& st : ctx.program.stories) if (st.rooms.empty()) ++emptyBefore;
    const bool typologyApplied = autofillRoomLayout(ctx.program, seed ? seed : 1u, ctx.rp);
    if (emptyBefore > 0) {
        int total = 0;
        for (const auto& st : ctx.program.stories) total += (int)st.rooms.size();
        LOG_INFO_FMT("StructureBuild", "generate_room_layout: auto-filled "
                     << emptyBefore << " story(ies) -> " << total << " rooms total"
                     << (ctx.rp ? " [typology " + ctx.typ + "]" : " [generic]"));
        // Surface silent degradation: a typology was resolved but the footprint
        // couldn't fit it, so the ground floor is a generic box.
        if (ctx.rp && !typologyApplied) {
            const int need = 2 * (int)ctx.rp->rooms.size();
            LOG_WARN_FMT("StructureBuild", "typology " << ctx.typ << " did NOT fit footprint "
                         << ctx.program.footprintW << "x" << ctx.program.footprintD
                         << " (need long axis >= " << need
                         << ") — ground floor used a GENERIC layout (no purposed rooms)");
            ctx.response["typology_unfit"] = {
                {"typology", ctx.typ}, {"need_long_axis", need},
                {"footprint", {ctx.program.footprintW, ctx.program.footprintD}}};
        }
    }
    return rep;
}

// ---------------------------------------------------------------------------
// validate_program — pre-build validation gate (M1: WARN-BUT-ALLOW, exactly the
// monolith's policy; teeth land in M3 as repair-then-refuse).
// ---------------------------------------------------------------------------
StructureForge::StageReport StructureForge::stageValidateProgram(Context& ctx) {
    StageReport rep;
    ValidationReport vr = BuildingProgramValidator::validate(ctx.program, {}, ctx.rp);
    if (vr.ok())
        LOG_INFO_FMT("StructureBuild", "program validation: OK"
                     << (ctx.rp ? " [typology " + ctx.typ + "]" : ""));
    else
        LOG_WARN_FMT("StructureBuild", "program validation FAILED (warn-but-allow,"
                     " building anyway): " << vr.summary());
    ctx.msSetup = ctx.pc.lap();
    return rep;
}

// ---------------------------------------------------------------------------
// footprint — CONTEXT-AWARE PLACEMENT: remove any existing structure whose
// footprint overlaps this one BEFORE seating (no stacking), and the object-wise
// half of the vegetation gate (placed trees on the lot removed whole).
// ---------------------------------------------------------------------------
StructureForge::StageReport StructureForge::stageFootprint(Context& ctx) {
    StageReport rep;
    auto* chunkManager = ctx.deps.chunkManager;
    auto* placedObjectManager = ctx.deps.placedObjects;
    if (placedObjectManager) {
        const int fw = std::max(ctx.program.footprintW, 1);
        const int fd = std::max(ctx.program.footprintD, 1);
        const bool keepVeg = ctx.params.value("keep_vegetation", false);
        int vegObjectsRemoved = 0;
        std::vector<std::string> toRemove;
        for (const auto& obj : placedObjectManager->list()) {
            const bool overlapXZ =
                obj.boundingMin.x <= ctx.ox + fw - 1 && obj.boundingMax.x >= ctx.ox &&
                obj.boundingMin.z <= ctx.oz + fd - 1 && obj.boundingMax.z >= ctx.oz;
            if (!overlapXZ) continue;
            if (obj.category == "structure") {
                LOG_INFO_FMT("StructureBuild", "removing overlapping structure '"
                             << obj.id << "' before rebuild (no stacking)");
                toRemove.push_back(obj.id);
            } else if (obj.category == "template" && !keepVeg && chunkManager) {
                // VEGETATION GATE, object-wise half: a placed TREE/BUSH template
                // on the lot is removed WHOLE (remove() clears its region AND
                // its registry entry — the voxel flood alone left a ghost entry
                // and could strip a neighbor's interlocked canopy). Tree-ness is
                // decided by CONTENT (Log*/Leaf* cells in its bbox), not name.
                bool treeMatter = false;
                int budget = 4096;   // trees are small; bail on huge objects
                for (int x = obj.boundingMin.x; x <= obj.boundingMax.x && !treeMatter && budget > 0; ++x)
                    for (int y = obj.boundingMin.y; y <= obj.boundingMax.y && !treeMatter && budget > 0; ++y)
                        for (int z = obj.boundingMin.z; z <= obj.boundingMax.z && !treeMatter; --budget, ++z)
                            if (DamageSystem::isTreeMatterCell(chunkManager, {x, y, z}))
                                treeMatter = true;
                if (treeMatter) {
                    LOG_INFO_FMT("StructureBuild", "vegetation gate: removing placed tree '"
                                 << obj.id << "' (" << obj.templateName
                                 << ") from the lot");
                    toRemove.push_back(obj.id);
                    ++vegObjectsRemoved;
                }
            }
        }
        for (const auto& id : toRemove) placedObjectManager->remove(id);
        if (vegObjectsRemoved > 0)
            ctx.response["vegetation_objects_removed"] = vegObjectsRemoved;
    }
    ctx.msOverlap = ctx.pc.lap();
    return rep;
}

// ---------------------------------------------------------------------------
// realize — StructureRealizer::realizeShell: program + style -> MicroCanvas +
// AssemblyPlan.
// ---------------------------------------------------------------------------
StructureForge::StageReport StructureForge::stageRealize(Context& ctx) {
    StageReport rep;
    ctx.shell = StructureRealizer::realizeShell(ctx.program, ctx.style);
    if (!ctx.shell.ok) {
        rep.action = StageReport::Action::Refused;
        rep.refusal = {{"error", "realize failed: " + ctx.shell.error}};
        return rep;
    }
    ctx.msRealize = ctx.pc.lap();
    return rep;
}

// ---------------------------------------------------------------------------
// validate_realized — L2/L3 gates on the realized canvas. M1: empty anchor
// stage (the M3 milestone populates it with the TraversalProbe reachability
// flood + the dormant realized detectors).
// ---------------------------------------------------------------------------
StructureForge::StageReport StructureForge::stageValidateRealized(Context&) {
    return StageReport{};
}

// ---------------------------------------------------------------------------
// place — prepare_pad (#2) + grounding gate + voxel-wise vegetation gate +
// excavate_basement (#34), then toStructureResult, locations, assembly-plan
// metadata, fixture-pass context, surgical excavation, placeAndRegisterImpl,
// habitation metadata.
// ---------------------------------------------------------------------------
StructureForge::StageReport StructureForge::stagePlace(Context& ctx) {
    StageReport rep;
    auto* chunkManager = ctx.deps.chunkManager;
    auto* placedObjectManager = ctx.deps.placedObjects;
    const nlohmann::json& params = ctx.params;
    const int ox = ctx.ox, oz = ctx.oz, reqY = ctx.reqY;

    // prepare_pad (#2): LEVEL the bumpy terrain under the footprint to a flat build
    // pad — cut the high side, fill the low side to the median grade — then seat the
    // foundation on it. Replaces bare median-seating.
    int W = std::max(ctx.program.footprintW, 1), D = std::max(ctx.program.footprintD, 1);
    ctx.oy = reqY;
    {
        std::vector<int> tops;
        std::vector<glm::ivec2> cells;
        std::vector<glm::ivec3> vegSeeds;   // tree matter found inside the footprint
        const int scanTop = reqY + 64;
        for (int x = ox; x < ox + W; ++x)
            for (int z = oz; z < oz + D; ++z) {
                int top = -1;
                for (int y = scanTop; y >= 0; --y) {
                    const glm::ivec3 wp(x, y, z);
                    // TREE MATTER is not terrain: a trunk/canopy cell must never
                    // set the pad level (the old scan took the canopy top as
                    // "ground" and seated the house against the tree).
                    if (DamageSystem::isTreeMatterCell(chunkManager, wp)) {
                        vegSeeds.push_back(wp);
                        continue;
                    }
                    if (chunkManager->hasVoxelAt(wp)) { top = y; break; }
                }
                if (top >= 0) { tops.push_back(top); cells.push_back(glm::ivec2(x, z)); }
            }
        // GROUNDING GATE: every footprint column must have terrain under it. Columns
        // with no terrain used to be silently SKIPPED by the pad-leveler and the
        // building seated on air (found live: a whole village east of the generated
        // chunk hung in the void). Refuse by default — {"allow_ungrounded": true}
        // overrides for tests/special cases.
        const int missingCols = W * D - static_cast<int>(tops.size());
        if (missingCols > 0 && !params.value("allow_ungrounded", false)) {
            LOG_WARN_FMT("StructureBuild", "REFUSING ungrounded build at (" << ox << "," << oz
                         << "): " << missingCols << "/" << (W * D)
                         << " footprint columns have no terrain (allow_ungrounded overrides)");
            rep.action = StageReport::Action::Refused;
            rep.refusal = {{"error", "ungrounded footprint: " + std::to_string(missingCols) + " of " +
                              std::to_string(W * D) + " columns have no terrain below y=" +
                              std::to_string(scanTop) +
                              " - generate terrain there first, or pass allow_ungrounded:true"},
                    {"ungrounded_columns", missingCols},
                    {"footprint_columns", W * D}};
            return rep;
        }
        // VEGETATION GATE: a build must never generate THROUGH a tree. Tree
        // matter (Log*/Leaf*, any granularity) inside the footprint is cleared
        // as WHOLE trees — flooded outward from the footprint cells so the
        // trunk/canopy overhanging the lot goes too, never a half-tree fused
        // into a wall. {"keep_vegetation": true} refuses the build instead.
        if (!vegSeeds.empty()) {
            if (params.value("keep_vegetation", false)) {
                LOG_WARN_FMT("StructureBuild", "REFUSING build at (" << ox << "," << oz
                             << "): " << vegSeeds.size()
                             << " tree cells in the footprint (keep_vegetation)");
                rep.action = StageReport::Action::Refused;
                rep.refusal = {{"error", "vegetation in footprint: " +
                                  std::to_string(vegSeeds.size()) +
                                  " tree cells - refused (keep_vegetation is set); "
                                  "move the build or drop keep_vegetation"},
                        {"vegetation_cells", static_cast<int>(vegSeeds.size())}};
                return rep;
            }
            const glm::ivec3 lo(ox - 16, 0, oz - 16);
            const glm::ivec3 hi(ox + W + 16, scanTop + 48, oz + D + 16);
            static constexpr size_t kMaxVegFlood = 60000;   // runaway-forest backstop
            std::set<std::array<int, 3>> seen;
            std::vector<glm::ivec3> frontier, toClear;
            auto push = [&](const glm::ivec3& p) {
                if (p.x < lo.x || p.x > hi.x || p.y < lo.y || p.y > hi.y ||
                    p.z < lo.z || p.z > hi.z) return;
                if (!seen.insert({p.x, p.y, p.z}).second) return;
                if (!DamageSystem::isTreeMatterCell(chunkManager, p)) return;
                frontier.push_back(p);
                toClear.push_back(p);
            };
            for (const auto& s : vegSeeds) push(s);
            while (!frontier.empty() && toClear.size() < kMaxVegFlood) {
                const glm::ivec3 p = frontier.back();
                frontier.pop_back();
                static const glm::ivec3 N6[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
                                                 {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
                for (const auto& d : N6) push(p + d);
            }
            // Bulk-clear per chunk (cube AND subdivision content — canopy cells
            // are often subcube/micro-only, which removeCube alone leaves behind).
            std::map<Chunk*, bool> vegTouched;
            for (const auto& p : toClear)
                if (Chunk* c = chunkManager->getChunkAtFast(p))
                    if (vegTouched.emplace(c, true).second) c->beginBulkOperation();
            for (const auto& p : toClear) {
                chunkManager->removeCube(p);
                if (Chunk* c = chunkManager->getChunkAtFast(p))
                    c->clearSubdivisionAt(ChunkManager::worldToLocalCoord(p));
            }
            for (auto& [c, _] : vegTouched) c->endBulkOperation();
            LOG_INFO_FMT("StructureBuild", "vegetation gate: cleared " << toClear.size()
                         << " tree cells from the lot (" << vegSeeds.size()
                         << " seeds in footprint)");
            ctx.response["vegetation_cleared_cells"] = static_cast<int>(toClear.size());
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
            ctx.oy = padLevel + 1;    // foundation bottom rests on the flat pad
            LOG_INFO_FMT("StructureBuild", "prepare_pad: leveled footprint to y=" << padLevel
                         << " (cut " << cut.size() << ", fill " << fill.voxels.size() << ")");

            // excavate_basement (#34): a basement seats BELOW grade — the ground floor
            // lands at the surface and the cellar is DUG OUT beneath it. The realizer's
            // foundation ring becomes the retaining walls; the ground-floor slab is the
            // cellar ceiling.
            if (ctx.program.substructure == "basement" && ctx.shell.crawlHeightCubes > 0) {
                const int depth = ctx.shell.crawlHeightCubes;   // cellar height (cubes)
                ctx.oy = padLevel + 1 - depth;                  // ground floor stays at grade
                std::vector<glm::ivec3> dig;
                for (int x = ox; x < ox + W; ++x)
                    for (int z = oz; z < oz + D; ++z)
                        for (int y = ctx.oy; y <= padLevel; ++y)
                            dig.push_back(glm::ivec3(x, y, z));
                StructureGenerator::removeVoxels(chunkManager, dig);   // bulk-end rebuilds collision
                LOG_INFO_FMT("StructureBuild", "excavate_basement: dug cellar " << depth
                             << " cubes below grade (" << dig.size() << " voxels)");
            }
        }
    }
    ctx.msPad = ctx.pc.lap();
    const int oy = ctx.oy;
    ctx.structure = StructureRealizer::toStructureResult(ctx.shell, glm::ivec3(ox, oy, oz));
    if (ctx.structure.voxels.empty()) {
        rep.action = StageReport::Action::Refused;
        rep.refusal = {{"error", "Failed to generate structure (unknown type or invalid params)"}};
        return rep;
    }

    // Playable-town: every building carries its schedule-target LocationMarker
    // (Home/Work/Tavern at the front door). placeAndRegisterImpl auto-registers
    // whatever StructureResult.locations holds — v2 populated none until now.
    ctx.structure.locations = StructureRealizer::deriveLocations(
        ctx.program, ctx.typ, ctx.shell.plan, glm::ivec3(ox, oy, oz), ctx.shell.floorTopMicro);

    // Persist the assembly plan with its placement origin: featureAt(local) + origin =
    // a post-build structural-feature query (wall/floor/ceiling/...) that no consumer
    // has to re-derive from voxel materials.
    ctx.planMeta = {{"origin", {ox, oy, oz}}, {"plan", ctx.shell.plan.toJson()}};

    // Fixture-pass context: the floor sits one cube above the foundation top.
    ctx.floorY = oy + ctx.shell.crawlHeightCubes;
    for (int ft : ctx.shell.floorTopByStory) {
        ctx.floorYByStory.push_back(oy + ft / 9);
        ctx.surfaceMicroYByStory.push_back(oy * 9 + ft);
    }
    // Exterior-wall thickness in micro — MUST equal what the REALIZER built (its
    // converter CLAMPS to [1,9]), not the raw style value: a stone_keep authors 3.0 m
    // and an unclamped 27-micro inset pushes furniture out of narrow rooms (dropped).
    // Claims Ledger increment 3: derived from the PLAN's recorded walls (what was
    // built), through the same clamped converter — no longer re-derived from style.
    ctx.extTMicro = FurniturePlacer::planExteriorThicknessMicro(ctx.shell.plan);
    // Roof apex (world micro) for place_chimney (#14): the stack must clear it.
    {
        glm::ivec3 cLo, cHi;
        if (ctx.shell.canvas.microBounds(cLo, cHi))
            ctx.roofApexWorldMicro = oy * 9 + cHi.y;
        else
            LOG_WARN("StructureBuild", "place_chimney: canvas microBounds failed -> "
                     "roof apex unknown; chimneys will be SKIPPED for this build");
    }

    // Snapshot BEFORE the excavation below so undo restores the pre-build terrain.
    glm::ivec3 smin(INT_MAX), smax(INT_MIN);
    for (const auto& v : ctx.structure.voxels) {
        smin = glm::min(smin, v.position);
        smax = glm::max(smax, v.position);
    }
    if (ctx.deps.pushUndo)
        ctx.deps.pushUndo(smin, smax, "build_structure:" + params.value("type", std::string("v2")));

    // P2 excavation: clear exactly the structure's cube cells so its voxels can't fail
    // against pre-existing terrain / other structures. Surgical (the building's own
    // footprint, not a bbox); every cell is at y >= oy = grade+1 so this never removes
    // the ground the building rests on.
    {
        std::vector<glm::ivec3> cells;
        cells.reserve(ctx.structure.voxels.size());
        for (const auto& v : ctx.structure.voxels) cells.push_back(v.position);
        StructureGenerator::removeVoxels(chunkManager, cells);
    }

    ctx.msExcav = ctx.pc.lap();
    ctx.out = detail::placeAndRegisterImpl(ctx.structure, params, ctx.deps, ctx.planMeta,
                                           /*doSnapshot=*/false);
    ctx.msRegister = ctx.pc.lap();
    // Merge pre-build fields (typology_unfit) into the outcome response.
    for (auto it = ctx.response.begin(); it != ctx.response.end(); ++it)
        ctx.out.response[it.key()] = it.value();
    ctx.response = ctx.out.response;
    if (!ctx.out.ok) {
        rep.action = StageReport::Action::Refused;
        rep.refusal = ctx.response;
        return rep;
    }
    ctx.objectId = ctx.out.objectId;
    ctx.posX = ctx.out.posX;
    ctx.posZ = ctx.out.posZ;

    // Persist habitation semantics on the placed object (saved with the chunks, like
    // assembly_plan): typology + purposed rooms. assembly_plan records geometry only —
    // without this, "which building is the bakery / where is the chamber" is
    // unanswerable after the build response is gone.
    if (placedObjectManager && !ctx.objectId.empty()) {
        nlohmann::json roomsJ = nlohmann::json::array();
        for (size_t si = 0; si < ctx.program.stories.size(); ++si)
            for (const auto& rm : ctx.program.stories[si].rooms)
                roomsJ.push_back({{"story", static_cast<int>(si)},
                                  {"purpose", rm.purpose},
                                  {"rect", rm.rect.toJson()}});
        placedObjectManager->setMetadata(ctx.objectId, "building",
            {{"typology", ctx.typ}, {"function", ctx.program.function},
             {"style", ctx.program.style}, {"rooms", roomsJ}});
    }
    return rep;
}

// ---------------------------------------------------------------------------
// furnish — v2: the ENGINE decides furniture placement. FurniturePlacer derives
// what/where/facing/clearance from each room's purpose + door positions —
// hand-authored program fixtures are IGNORED. Pieces are parented to the
// structure so they group and are removed with it. Includes place_chimney (#14),
// surface item props, and place_signage (#47). (The heavy/light/lighting/clutter
// pass split is the M4 milestone; M1 moves the block whole.)
// ---------------------------------------------------------------------------
StructureForge::StageReport StructureForge::stageFurnish(Context& ctx) {
    StageReport rep;
    auto* chunkManager = ctx.deps.chunkManager;
    auto* placedObjectManager = ctx.deps.placedObjects;
    auto* objectTemplateManager = ctx.deps.templates;
    auto* itemPropManager = ctx.deps.itemProps;
    nlohmann::json& response = ctx.response;
    const BuildingProgram& program = ctx.program;
    const std::string& objectId = ctx.objectId;
    const int posX = ctx.posX, posZ = ctx.posZ;
    const int floorY = ctx.floorY;
    const std::vector<int>& floorYByStory = ctx.floorYByStory;
    const std::vector<int>& surfaceMicroYByStory = ctx.surfaceMicroYByStory;
    const int extTMicro = ctx.extTMicro;
    const int roofApexWorldMicro = ctx.roofApexWorldMicro;
    const int ox = ctx.ox, oz = ctx.oz;

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
                nlohmann::json m = detail::loadAssetMetricsSidecar(tmpl);
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

        int fxSpawned = 0, fxSkipped = 0, itemsSpawned = 0;
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
        const std::string wealthTier = ctx.rp ? ctx.rp->wealthTier : "";
        for (size_t si = 0; si < program.stories.size(); ++si) {
            const auto& story = program.stories[si];
            // KI-2: per-story floor Y — else all furniture stacks on the ground floor.
            int storyFloorY = (si < floorYByStory.size()) ? floorYByStory[si] : floorY;
            // Claims Ledger increment 3: furnish FROM THE PLAN. Wall thicknesses
            // (exterior span reservation + KI-5b interior partition insets) and the
            // KI-5d stair reservations all derive from the recorded AssemblyPlan —
            // what the realizer BUILT — inside furnishFromPlan; this call site no
            // longer computes side-channels. Placement equivalence pinned
            // field-by-field by FurnishPlanEquivalenceTest.
            auto placements = FurniturePlacer::furnishFromPlan(
                story, static_cast<int>(si), glm::ivec3(posX, 0, posZ), storyFloorY,
                ctx.shell.plan, fixtureFootprints, &unplaced, wealthTier);
            // Semantic identity per fixture (room/purpose/ordinal/type), 1:1 with
            // placements — so a session can address "the 2nd bedroom's bed".
            auto labels = FurniturePlacer::labelFixtures(story, placements);
            // Surfaces that receive item props: the ACTUAL placed pose (micro pos
            // INCLUDING wall inset + rotation), captured at placement — the plan
            // cell alone put items on table edges / hovering beside the table.
            struct PlacedSurface {
                std::string room; std::string type; const VoxelTemplate* tmpl;
                glm::ivec3 microPos; int rotation;
            };
            std::vector<PlacedSurface> placedSurfaces;
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
                // `as:"item"` recipe pieces realize as PICKABLE ITEM PROPS at the
                // planned spot (rugs on the floor, ...) — same placement, different
                // realization; baked templates remain the default.
                const std::string itemForm = FurniturePlacer::itemFormFor(pl.type);
                if (!itemForm.empty() && itemPropManager) {
                    // Center the prop on its reserved footprint (microPos is the
                    // anchor corner; props spawn about their own center).
                    Footprint iFp = fixtureFootprints.count(pl.type)
                        ? fixtureFootprints[pl.type] : Footprint{1, 1};
                    const bool iRot = (((pl.rotation % 360) + 360) % 360 == 90) ||
                                      (((pl.rotation % 360) + 360) % 360 == 270);
                    const float halfW = std::max(1, iRot ? iFp.depth : iFp.width) * 0.5f;
                    const float halfD = std::max(1, iRot ? iFp.width : iFp.depth) * 0.5f;
                    const glm::vec3 ipos(microPos.x / 9.0f + halfW,
                                         microPos.y / 9.0f + 0.005f,
                                         microPos.z / 9.0f + halfD);
                    std::string pid = itemPropManager->spawnProp(
                        itemForm, ipos, static_cast<float>(pl.rotation),
                        /*snapToGround=*/false, /*instanceUuid=*/"",
                        glm::vec3(0.0f), /*dynamic=*/false);
                    if (pid.empty()) { ++fxSkipped; continue; }
                    placedObjectManager->setParent(pid, objectId);
                    placedObjectManager->setMetadata(pid, "fixture", {
                        {"structure", objectId}, {"room", pl.room},
                        {"kind", "item"}, {"type", pl.type}, {"story", (int)si}});
                    ++itemsSpawned;
                    continue;
                }
                std::string fid = placedObjectManager->placeTemplateMicro(
                    tmpl, microPos, pl.rotation, objectId);
                if (fid.empty()) { ++fxSkipped; continue; }
                ++fxSpawned;
                // Tables + the bar counter get surface item props (see below).
                if (itemPropManager && objectTemplateManager &&
                    (pl.type.find("table") != std::string::npos ||
                     pl.type == "tavern_bar" || pl.type == "back_bar" ||
                     pl.type.find("counter") != std::string::npos)) {
                    if (const auto* ttm = objectTemplateManager->getTemplate(tmpl))
                        placedSurfaces.push_back({pl.room, pl.type, ttm, microPos,
                                                  pl.rotation});
                }
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
                        nlohmann::json hm = detail::loadAssetMetricsSidecar(
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
            // Surface items (ItemPlacementPlan.md, 2026-08-07): scatter PICKABLE
            // ITEM PROPS on table tops — per-purpose sets from the recipes
            // ("surface_items"), placed at the table's MEASURED top surface
            // (template geometry — the old pass guessed floor+1 CUBE and used
            // baked clutter templates; that path remains the no-ItemPropManager
            // fallback). Deterministic per table position; props spawn STATIC
            // (exact pose, zero physics) and are PARENTED to the structure so a
            // rebuild removes them (no duplicate accumulation).
            if (itemPropManager) {
                // Spots come from the ACTUAL placed surface (template geometry at
                // the placed microPos + rotation, top-surface rect measured) — the
                // plan cell + unrotated catalog footprint missed the real tabletop
                // (wall inset + rotation), leaving items on the edge or hovering
                // beside the table at tabletop height.
                for (const auto& ps : placedSurfaces) {
                    unsigned cseed =
                        (static_cast<unsigned>(ps.microPos.x) * 73856093u) ^
                        (static_cast<unsigned>(ps.microPos.z) * 19349663u) ^ 0x9e3779b9u;
                    // Fixture-keyed set first (a back bar wants BOTTLES on its
                    // shelves, not plates) — falls through to the room's set.
                    const auto items = FurniturePlacer::surfaceItemsFor(
                        ps.type == "back_bar" ? ps.type : ps.room);
                    auto spots = FurniturePlacer::placeSurfaceItems(
                        ps.room, *ps.tmpl, ps.microPos, ps.rotation, items, cseed);
                    for (const auto& c : spots) {
                        std::string pid = itemPropManager->spawnProp(
                            c.type, c.worldPos, c.yawDeg, /*snapToGround=*/false,
                            /*instanceUuid=*/"", glm::vec3(0.0f), /*dynamic=*/false);
                        if (pid.empty()) { ++fxSkipped; continue; }
                        placedObjectManager->setParent(pid, objectId);
                        placedObjectManager->setMetadata(pid, "fixture", {
                            {"structure", objectId}, {"room", ps.room},
                            {"kind", "item"}, {"type", c.type}, {"story", (int)si}});
                        ++itemsSpawned;
                    }
                }
            } else {
                for (const auto& pl : placements) {
                    if (pl.type.find("table") == std::string::npos) continue;
                    Footprint fp = fixtureFootprints.count(pl.type)
                        ? fixtureFootprints[pl.type] : Footprint{1, 1};
                    Rect surf{pl.worldPos.x, pl.worldPos.z, fp.width, fp.depth};
                    unsigned cseed =
                        (static_cast<unsigned>(pl.worldPos.x) * 73856093u) ^
                        (static_cast<unsigned>(pl.worldPos.z) * 19349663u) ^ 0x9e3779b9u;
                    // Legacy fallback: baked clutter templates at the cube guess.
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
        response["items_spawned"] = itemsSpawned;   // pickable surface props (2026-08-07)
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
        ctx.msFixtures = ctx.pc.lap();
        LOG_INFO_FMT("StructureBuild", "FurniturePlacer: engine placed " << fxSpawned
                     << " fixtures (" << fxSkipped << " skipped) into '" << objectId << "'");
    }
    return rep;
}

// ---------------------------------------------------------------------------
// emit — [no-frozen-engine] the phase distribution this build actually spent.
// ---------------------------------------------------------------------------
StructureForge::StageReport StructureForge::stageEmit(Context& ctx) {
    StageReport rep;
    const long long msTotal = ctx.msSetup + ctx.msOverlap + ctx.msRealize + ctx.msPad +
                              ctx.msExcav + ctx.msRegister + ctx.msFixtures;
    LOG_INFO_FMT("StructureBuild", "[perf] phases ms: setup=" << ctx.msSetup
                 << " overlap=" << ctx.msOverlap
                 << " realize=" << ctx.msRealize << " pad=" << ctx.msPad
                 << " excav=" << ctx.msExcav
                 << " place+register=" << ctx.msRegister << " (place=" << ctx.out.msPlace
                 << " grass=" << ctx.out.msGrass << " nav=" << ctx.out.msNav
                 << ") fixtures=" << ctx.msFixtures << " TOTAL=" << msTotal);
    ctx.response["timings_ms"] = {{"setup", ctx.msSetup}, {"overlap", ctx.msOverlap},
                              {"realize", ctx.msRealize},
                              {"pad", ctx.msPad}, {"excav", ctx.msExcav},
                              {"register", ctx.msRegister},
                              {"place", ctx.out.msPlace}, {"grass", ctx.out.msGrass},
                              {"nav", ctx.out.msNav},
                              {"fixtures", ctx.msFixtures}, {"total", msTotal}};
    return rep;
}

} // namespace Core
} // namespace Phyxel
