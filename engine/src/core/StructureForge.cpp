#include "core/StructureForge.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <map>
#include <set>
#include <vector>

#include "StructureBuildDetail.h"

#include "core/AssetRequestLedger.h"
#include "core/BuildingProgram.h"
#include "core/BuildingProgramValidator.h"
#include "core/ChunkManager.h"
#include "core/DamageSystem.h"
#include "core/FurnitureCatalog.h"
#include "core/FurniturePlacer.h"
#include "core/ItemPropManager.h"
#include "core/ItemRegistry.h"
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

    // floorplan -> (M3 repair state: the PRE-autofill story list + the seed used.
    // The program gate's one bounded repair restores this snapshot and re-rolls
    // the autofill with a salted seed — authored rooms are never touched, and
    // stories the autofill GREW (typology story count) are regrown consistently.)
    unsigned floorplanSeed = 1;
    bool anyAutofill = false;
    std::vector<ProgStory> preAutofillStories;

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
        "intake", "floorplan", "validate_program", "validate_assets", "footprint",
        "realize", "validate_realized", "place", "furnish", "emit"};
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
        {"validate_assets",   &StructureForge::stageValidateAssets},
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
            // produced) + the gates so far and the refusing stage for diagnosability.
            nlohmann::json refusal = std::move(r.refusal);
            refusal["refused_at"] = name;
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
    // M8 PERIOD AXIS: medieval is the shipped default and lives in the legacy file;
    // any other era is a DATA PACK under resources/room_programs/<period>.json. No
    // code change adds an era.
    const std::string period = ctx.params.value("period", std::string("medieval"));
    if (period != "medieval")
        ctx.roomReg.loadPeriodPack("resources/room_programs/" + period + ".json", period);
    ctx.typ = ctx.program.typology.empty()
        ? RoomProgramRegistry::defaultTypologyForFunction(ctx.program.function)
        : ctx.program.typology;
    ctx.rp = ctx.typ.empty() ? nullptr : ctx.roomReg.get(ctx.typ, period);
    // An unknown period REFUSES rather than silently building a medieval house and
    // calling it Victorian — the same no-substitution rule the asset gate enforces.
    if (!ctx.typ.empty() && !ctx.rp && ctx.roomReg.get(ctx.typ)) {
        const auto known = ctx.roomReg.periods();
        rep.action = StageReport::Action::Refused;
        rep.refusal = {{"error", "no '" + ctx.typ + "' room program for period '" + period +
                                 "' — author it as a data pack under resources/room_programs/ "
                                 "(the engine never substitutes another era)"},
                       {"requested_period", period},
                       {"known_periods", known}};
        return rep;
    }

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
    ctx.anyAutofill = emptyBefore > 0;
    if (ctx.anyAutofill) ctx.preAutofillStories = ctx.program.stories;   // M3 repair snapshot
    ctx.floorplanSeed = seed ? seed : 1u;
    const bool typologyApplied = autofillRoomLayout(ctx.program, ctx.floorplanSeed, ctx.rp);
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
// validate_program — pre-build validation gate. M3: REPAIR-THEN-REFUSE — error
// severity gets ONE bounded repair (restore the pre-autofill stories and re-roll
// the layout with a salted seed; authored rooms are never touched), then a
// still-failing program REFUSES with the structured report. Warnings stay
// advisory. {"allow_invalid": true} skips ENFORCEMENT (test/debug escape,
// mirroring allow_ungrounded) — issues are still logged.
// ---------------------------------------------------------------------------
StructureForge::StageReport StructureForge::stageValidateProgram(Context& ctx) {
    StageReport rep;
    ValidationReport vr = BuildingProgramValidator::validate(ctx.program, {}, ctx.rp);
    if (vr.ok()) {
        LOG_INFO_FMT("StructureBuild", "program validation: OK"
                     << (ctx.rp ? " [typology " + ctx.typ + "]" : ""));
    } else if (ctx.params.value("allow_invalid", false)) {
        LOG_WARN_FMT("StructureBuild", "program validation FAILED (allow_invalid set,"
                     " building anyway): " << vr.summary());
    } else {
        // One bounded repair: re-roll the engine-authored layout. Only possible
        // when the layout WAS engine-authored; a hand-authored invalid program
        // has nothing the engine may legitimately rewrite.
        if (ctx.anyAutofill) {
            ctx.program.stories = ctx.preAutofillStories;
            autofillRoomLayout(ctx.program, ctx.floorplanSeed ^ 0x9E3779B9u, ctx.rp);
            vr = BuildingProgramValidator::validate(ctx.program, {}, ctx.rp);
            if (vr.ok()) {
                LOG_WARN_FMT("StructureBuild", "program validation failed on the first "
                             "layout; REPAIRED by re-rolling the autofill (salted seed)");
                rep.action = StageReport::Action::Repaired;
            }
        }
        if (!vr.ok()) {
            LOG_WARN_FMT("StructureBuild", "REFUSING build: program validation failed"
                         " (repair-then-refuse): " << vr.summary());
            rep.action = StageReport::Action::Refused;
            rep.refusal = {{"error", "program validation failed: " +
                                     std::to_string(vr.errorCount()) + " error(s)"},
                           {"validation", vr.toJson()}};
            return rep;
        }
    }
    ctx.msSetup = ctx.pc.lap();
    return rep;
}

// ---------------------------------------------------------------------------
// validate_assets (M3.5) — the ASSET GATE. Every fixture type this building's
// OWN rooms need must resolve to a real, loadable asset. A gap is recorded as a
// structured AssetRequest (persisted to resources/asset_requests.json, and
// returned as response["asset_requests"]) and the build REFUSES.
//
// The standing rule: the generator never invents or substitutes an asset, and
// never ships a half-furnished building. Vocabulary growth is assets-first —
// demand is discovered ahead of time by tools/asset_requests.py --scan, so a
// refusal here means someone shipped a recipe before its asset.
//
// Runs BEFORE the footprint/realize/place stages, so a refusal never leaves a
// placed shell behind. Recipes are loaded here (idempotent) because the gate
// must read the SAME recipe set the furnish pass will.
// {"allow_missing_assets": true} degrades to a warning (test/debug escape).
// ---------------------------------------------------------------------------
StructureForge::StageReport StructureForge::stageValidateAssets(Context& ctx) {
    StageReport rep;
    FurniturePlacer::loadRecipesFromFile("resources/furnishing_recipes.json");

    std::vector<std::string> purposes;
    for (const auto& st : ctx.program.stories)
        for (const auto& rm : st.rooms) purposes.push_back(rm.purpose);
    if (purposes.empty()) return rep;

    auto* otm = ctx.deps.templates;
    std::function<bool(const std::string&)> templateExists;
    if (otm) templateExists = [otm](const std::string& n) { return otm->getTemplate(n) != nullptr; };
    // No template manager (headless/tests) => only MAPPING coverage is checkable;
    // asset existence is unknowable, so it is not asserted (never a false refusal).
    auto coverage = validateFurnitureCoverageFor(purposes, templateExists);
    if (coverage.ok()) return rep;

    std::vector<AssetRequest> requests;
    for (const auto& g : coverage.gaps)
        requests.push_back({g.type, "furniture", g.purpose, ctx.typ,
                            g.templateName.empty() ? "unmapped" : "template_missing",
                            g.message});
    ctx.response["asset_requests"] = AssetRequestLedger::toJson(requests);

    // Record the demand so it can be burned down (dev builds; a read-only
    // resources dir just means the response is the only record — never fatal).
    const nlohmann::json merged = AssetRequestLedger::merge(
        AssetRequestLedger::load(), requests, ctx.params.value("today", std::string("unknown")));
    AssetRequestLedger::save(merged);

    for (const auto& g : coverage.gaps)
        LOG_WARN_FMT("StructureBuild", "asset request: " << g.message);

    if (ctx.params.value("allow_missing_assets", false)) {
        LOG_WARN_FMT("StructureBuild", "asset gate: " << coverage.gaps.size()
                     << " unsatisfied request(s) (allow_missing_assets set, building anyway)");
        return rep;
    }
    LOG_WARN_FMT("StructureBuild", "REFUSING build: " << coverage.gaps.size()
                 << " asset request(s) unsatisfied — author the asset(s) first "
                    "(tools/asset_requests.py --list)");
    rep.action = StageReport::Action::Refused;
    rep.refusal = {{"error", "unsatisfied asset requests: " +
                             std::to_string(coverage.gaps.size()) +
                             " fixture type(s) this building needs have no engine asset"},
                   {"asset_requests", AssetRequestLedger::toJson(requests)}};
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
// validate_realized — the L3 gate on the BUILT geometry (M3): a character-box
// must physically reach every room on every story of the realized canvas
// (TraversalProbe via RealizedStructureValidator::checkShellTraversal). The
// program gate proves the PLAN links up; this proves the carves and stairs the
// realizer actually painted do. No repair here — the realizer already repairs
// stairs; a failed flood means a geometry defect that must refuse, not ship.
// {"allow_invalid": true} skips enforcement.
// ---------------------------------------------------------------------------
StructureForge::StageReport StructureForge::stageValidateRealized(Context& ctx) {
    StageReport rep;
    ValidationReport tv = RealizedStructureValidator::checkShellTraversal(
        ctx.shell.canvas, ctx.shell.floorTopByStory, ctx.program);
    if (tv.ok()) return rep;
    if (ctx.params.value("allow_invalid", false)) {
        LOG_WARN_FMT("StructureBuild", "realized-shell traversal FAILED (allow_invalid"
                     " set, building anyway): " << tv.summary());
        return rep;
    }
    LOG_WARN_FMT("StructureBuild", "REFUSING build: realized shell is not traversable: "
                 << tv.summary());
    rep.action = StageReport::Action::Refused;
    rep.refusal = {{"error", "realized shell failed traversal: " +
                             std::to_string(tv.errorCount()) + " unreachable room(s)"},
                   {"validation", tv.toJson()}};
    return rep;
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

            // SEAT THE SUBSTRUCTURE BELOW GRADE (#34). A foundation belongs IN the
            // ground, not stacked on top of it: dig out what it displaces and let the
            // ground floor land at the surface, so only the floor layer reads above
            // grade. This was applied to basements only, which is why every crawlspace
            // building sat on a visible plinth like a model on a base.
            //
            // Same geometry for both substructures — the difference is semantic (a
            // cellar is occupiable, a crawlspace is not), and a "slab" has zero
            // substructure height so it is untouched.
            // Depth comes from what the realizer BUILT: the foundation course's top,
            // recorded per-cell in the plan. crawlHeightCubes is 0 for a crawlspace —
            // it measures an occupiable void, not the masonry course — so keying off
            // it sank basements only and left every crawlspace building on a plinth.
            int subDepth = ctx.shell.crawlHeightCubes;
            for (const auto& f : ctx.shell.plan.foundation)
                subDepth = std::max(subDepth, f.topY);
            if (subDepth > 0 && (ctx.program.substructure == "basement" ||
                                 ctx.program.substructure == "crawlspace")) {
                const int depth = subDepth;                     // substructure height (cubes)
                ctx.oy = padLevel + 1 - depth;                  // ground floor stays at grade
                std::vector<glm::ivec3> dig;
                for (int x = ox; x < ox + W; ++x)
                    for (int z = oz; z < oz + D; ++z)
                        for (int y = ctx.oy; y <= padLevel; ++y)
                            dig.push_back(glm::ivec3(x, y, z));
                StructureGenerator::removeVoxels(chunkManager, dig);   // bulk-end rebuilds collision
                LOG_INFO_FMT("StructureBuild", "excavate_substructure: sank the "
                             << ctx.program.substructure << " " << depth
                             << " cubes below grade (" << dig.size() << " voxels dug)");
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
// planSignMount — where a trade-sign board hangs, chosen BY FIT.
//
// The grounded projecting-sign code (checkSignClearance) caps how far a sign may
// jut from the wall at 48 in / 11 micro. A board narrower than that swings on a
// bracket — the authentic medieval trade-sign form. A board WIDER than that is
// not thrown away: it mounts flush to the facade, which is an equally real
// signage form and keeps the painted face square to anyone approaching. Only a
// roof with no room above the lintel refuses.
//
// Board local frame (tools/gen_items.py _flat_projected, axis=z): X = width,
// Y = height, Z = thickness; the painted face normal is local +-Z.
// Yaw about +Y maps local +Z -> (sin, 0, cos) and local +X -> (cos, 0, -sin).
// ---------------------------------------------------------------------------
StructureForge::SignMount StructureForge::planSignMount(
        WallSide side, int wallOuterMicro, int alongCenterMicro,
        int floorMicroY, int doorHeadMicroY, int roofApexMicroY,
        float boardW, float boardH, float boardT) {
    SignMount m;
    // Grounded limits (see RealizedStructureValidator::checkSignClearance).
    constexpr int kMinClearMicro = 22;   // >= 8 ft above grade
    // PROJECTION CAP — the MEDIEVAL limit, not the modern one. The validator's
    // 48-in default is late sign code; a medieval tavern sign is an ALESTAKE, a
    // pole-and-board projecting over the street, and the period limit on it is
    // the 1375 City of London ordinance restricting ale-stakes to 7 ft over the
    // King's highway. 7 ft = 2.13 m = 19 micro. Using the modern 1.22 m here was
    // a grounding error on my part: it forced the 2 m Prancing Pony board flat
    // against the facade, when the whole point of an inn sign is that it hangs
    // OUT so it reads from along the road.
    constexpr int kMaxProjMicro  = 19;   // <= 7 ft (1375 London ale-stake ordinance)

    const int boardHMicro = std::max(1, (int)std::ceil(boardH * 9.0f));
    const int minBottom = std::max(floorMicroY + kMinClearMicro, doorHeadMicroY + 1);
    m.boardBottomMicroY = minBottom;
    if (roofApexMicroY > 0 && m.boardBottomMicroY + boardHMicro > roofApexMicroY) {
        m.boardBottomMicroY = roofApexMicroY - boardHMicro;   // tuck under the eave
        if (m.boardBottomMicroY < minBottom) {
            m.skipReason = "no room above the door head under the eave "
                           "(roof apex too low for a clearing sign)";
            return m;
        }
    }

    // Outward normal of the wall the door sits in.
    const bool onX = (side == WallSide::MinusX || side == WallSide::PlusX);
    const float nSign = (side == WallSide::MinusX || side == WallSide::MinusZ)
                        ? -1.0f : 1.0f;

    // FORM BY FIT: bracket first, facade as the bounded repair.
    const int projProjecting = std::max(1, (int)std::ceil(boardW * 9.0f));
    const int projFlush      = std::max(1, (int)std::ceil(boardT * 9.0f));
    float outOffset;   // board center offset from the wall face, along the normal
    if (projProjecting <= kMaxProjMicro) {
        m.form = "projecting";
        m.projectionMicro = projProjecting;
        outOffset = boardW * 0.5f;          // juts out half its width
        // local +X -> outward normal
        switch (side) {
            case WallSide::PlusX:  m.rotationDeg = 0;   break;
            case WallSide::MinusZ: m.rotationDeg = 90;  break;
            case WallSide::MinusX: m.rotationDeg = 180; break;
            case WallSide::PlusZ:  m.rotationDeg = 270; break;
        }
    } else {
        m.form = "flush";
        m.projectionMicro = projFlush;
        outOffset = boardT * 0.5f + 0.01f;  // barely proud of the cladding
        // local +Z (the painted face) -> outward normal
        switch (side) {
            case WallSide::PlusZ:  m.rotationDeg = 0;   break;
            case WallSide::PlusX:  m.rotationDeg = 90;  break;
            case WallSide::MinusZ: m.rotationDeg = 180; break;
            case WallSide::MinusX: m.rotationDeg = 270; break;
        }
    }

    // GATE (not a guess): the same validator the furniture board answers to.
    const ValidationReport sc = RealizedStructureValidator::checkSignClearance(
        m.boardBottomMicroY, floorMicroY, m.projectionMicro, kMinClearMicro,
        kMaxProjMicro, doorHeadMicroY);
    if (!sc.ok()) { m.skipReason = sc.summary(); return m; }

    const float face  = wallOuterMicro / 9.0f;
    const float along = alongCenterMicro / 9.0f;
    m.worldPos.y = m.boardBottomMicroY / 9.0f;
    if (onX) { m.worldPos.x = face + nSign * outOffset; m.worldPos.z = along; }
    else     { m.worldPos.z = face + nSign * outOffset; m.worldPos.x = along; }
    m.ok = true;
    return m;
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
        // M4 chimney pass state: hearths that BURN, collected during placement and
        // served after it (see the chimney pass below).
        struct VentedHearth { std::string type; std::string objectId; glm::ivec3 microPos; };
        std::vector<VentedHearth> ventedHearths;
        int chimneysBuilt = 0;
        nlohmann::json fluelessRemoved = nlohmann::json::array();
        // M5 lighting pass state: every placed fixture that EMITS light, collected
        // during placement and registered as real engine point lights afterwards.
        struct Emitting { std::string type; std::string room; std::string objectId;
                          glm::ivec3 microPos; };
        std::vector<Emitting> emitters;
        // M7: every placed fixture's TRUE world AABB, for the doorway-clearance scan.
        std::vector<RealizedStructureValidator::PlacedBox> placedBoxes;
        // ...and what it takes to PUT ONE BACK somewhere else. The M7 repair used to
        // delete a door-blocking piece outright; the Prancing Pony lost a bench, a
        // stool and a bed that way. A blocked doorway is still worse than a missing
        // stool, but "slide it clear" beats "throw it away" whenever a clear spot
        // exists, so the room stays furnished.
        struct Reseat { std::string tmpl; glm::ivec3 microPos; int rotation; };
        std::map<std::string, Reseat> reseat;   // objectId -> how to re-place it
        nlohmann::json unblockedDoors = nlohmann::json::array();
        // Destructive writes by passes that run AFTER the shell was validated.
        nlohmann::json displacedByPost = nlohmann::json::array();
        int lightsRegistered = 0;
        std::set<std::string> litRooms;   // rooms that got at least one light source
        nlohmann::json darkRooms = nlohmann::json::array();
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
            // Keyed by objectId so a fixture that later MOVES or is REMOVED takes its
            // tableware with it. Items are spawned at the very end of the stage, but
            // the pose recorded here was the one captured at placement — when the
            // doorway repair slid a table aside, its tankards were still scattered
            // onto the pose it used to have and hung in the air where the table no
            // longer was.
            struct PlacedSurface {
                std::string room; std::string type; const VoxelTemplate* tmpl;
                glm::ivec3 microPos; int rotation; std::string objectId;
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
                                                  pl.rotation, fid});
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

                // M4: VENTED hearths are collected here and served by the chimney
                // pass AFTER the story's fixtures are placed — the stack used to be
                // emitted inline, in the middle of the furniture loop, which made
                // "did every hearth get a flue?" unanswerable.
                if (FurniturePlacer::isVentedFixture(pl.type))
                    ventedHearths.push_back({pl.type, fid, microPos});
                // M5: collect light SOURCES for the lighting pass below.
                if (FurniturePlacer::emitterFor(pl.type).emits)
                    emitters.push_back({pl.type, pl.room, fid, microPos});
                // M7: record the REGISTERED world bbox (cubes -> micro). This is the
                // render-accurate extent including the wall-inset micro-spill, which
                // is exactly the spill the plan-time cell reservation cannot see.
                if (const auto* obj = placedObjectManager->get(fid)) {
                    placedBoxes.push_back({pl.type, pl.room, fid,
                                           obj->boundingMin * 9,
                                           (obj->boundingMax + glm::ivec3(1)) * 9});
                    reseat[fid] = {tmpl, microPos, pl.rotation};
                }
            }

            // ---- place_chimney (#14), M4: its own pass over the story's vented
            // hearths. Each gets a masonry stack resting on its mantel and clearing
            // the ridge for draught (>= 2 ft, IRC R1003.9). A hearth that CANNOT be
            // vented (unknown roof apex, or no room above the mantel) is REMOVED and
            // reported: a fireplace with no flue is a defect, not a decoration.
            for (const auto& vh : ventedHearths) {
                std::string why;
                if (roofApexWorldMicro <= 0) {
                    why = "roof apex unknown (canvas microBounds failed)";
                } else {
                    // Centre the stack on the hearth's ACTUAL placed bbox so a ROTATED
                    // hearth still gets its chimney directly overhead (V8).
                    Footprint cfp;
                    auto fpIt = fixtureFootprints.find(vh.type);
                    if (fpIt != fixtureFootprints.end()) cfp = fpIt->second;
                    int ccx = vh.microPos.x + std::max(1, cfp.width) * 9 / 2;
                    int ccz = vh.microPos.z + std::max(1, cfp.depth) * 9 / 2;
                    if (const auto* hobj = placedObjectManager->get(vh.objectId)) {
                        ccx = (hobj->boundingMin.x + hobj->boundingMax.x + 1) * 9 / 2;
                        ccz = (hobj->boundingMin.z + hobj->boundingMax.z + 1) * 9 / 2;
                    }
                    // The stack RESTS ON the hearth top (mantel), never diving through
                    // the firebox (V2 checkChimneyOnHearth). Height from .metrics.json.
                    int hearthH = 9;
                    nlohmann::json hm = detail::loadAssetMetricsSidecar(
                        FurnitureCatalog::templateFor(vh.type));
                    if (hm.is_object() && hm.contains("overall_max") &&
                        hm["overall_max"].is_array() && hm["overall_max"].size() >= 2)
                        hearthH = std::max(1, (int)std::lround(
                            hm["overall_max"][1].get<double>() * 9.0));
                    const int baseY = vh.microPos.y + hearthH;
                    const int topY = roofApexWorldMicro +
                        StructureGenerator::kChimneyRidgeClearanceMicro;
                    if (topY <= baseY) {
                        why = "no room for a stack between the mantel and the ridge clearance";
                    } else {
                        auto chimney = StructureGenerator::planChimneyStack(
                            ccx, ccz, baseY, topY, "Bricks");
                        // A pass running AFTER the shell writes into a building the
                        // shell-side gates already certified. Every cell it DISPLACES
                        // is structure it just ate, and nothing downstream re-checks
                        // the shell — so account for it here rather than discover it
                        // in a screenshot.
                        const auto res = StructureGenerator::place(chunkManager, chimney);
                        // Punch the flue AFTER the masonry: the shaft must be air all
                        // the way up, through every floor slab and the roof deck it
                        // crosses. `clears` are world MICRO cells; removeMicroCells
                        // refines any coarser voxel it lands in rather than nuking the
                        // whole cube (that mistake is what put bays in the walls).
                        int flueOpened = 0;
                        for (const auto& mc : chimney.clears) {
                            const glm::ivec3 cube(mc.x / 9, mc.y / 9, mc.z / 9);
                            const glm::ivec3 rem(mc.x % 9, mc.y % 9, mc.z % 9);
                            chunkManager->ensureChunkAt(cube);
                            if (Chunk* ck = chunkManager->getChunkAtFast(cube)) {
                                // Refine-then-remove: write the cell (subdividing any
                                // coarser voxel, preserving the rest), then erase it.
                                const glm::ivec3 lp =
                                    Utils::CoordinateUtils::worldToLocalCoord(cube);
                                const glm::ivec3 sub(rem.x / 3, rem.y / 3, rem.z / 3);
                                const glm::ivec3 mic(rem.x % 3, rem.y % 3, rem.z % 3);
                                ck->addMicrocube(lp, sub, mic, "Bricks");
                                if (ck->removeMicrocube(lp, sub, mic)) ++flueOpened;
                            }
                        }
                        LOG_INFO_FMT("StructureBuild", "place_chimney: opened "
                                     << flueOpened << " flue cell(s) through the stack");
                        if (res.displaced > 0) {
                            nlohmann::json where = nlohmann::json::array();
                            for (const auto& p : res.displacedSample)
                                where.push_back({p.x, p.y, p.z});
                            LOG_WARN_FMT("StructureBuild", "place_chimney DISPLACED "
                                         << res.displaced << " existing voxel(s) building the "
                                         << "stack at (" << ccx / 9 << "," << ccz / 9
                                         << ") — it is cutting through structure that was "
                                            "already built");
                            displacedByPost.push_back({{"pass", "chimney"},
                                                       {"cells", res.displaced},
                                                       {"sample", where}});
                        }
                        ++chimneysBuilt;
                    }
                }
                if (why.empty()) continue;
                LOG_WARN_FMT("StructureBuild", "place_chimney: cannot vent " << vh.type
                             << " (" << why << ") — REMOVING the hearth rather than "
                                "leaving it flueless");
                placedObjectManager->remove(vh.objectId);
                --fxSpawned;
                fluelessRemoved.push_back({{"type", vh.type}, {"reason", why}});
            }
            ventedHearths.clear();

            // ---- place_lights (#18), M5: the LIGHTING pass. Fixtures that emit
            // (candles, sconces, chandeliers, and the hearth fire itself) were placed
            // by the furnishing lighting pass; here each becomes a REAL engine point
            // light at its flame height. Before M5 these were glow-material props:
            // they self-lit their own voxels and illuminated nothing, so a "lit"
            // tavern was pitch black at night.
            for (const auto& em : emitters) {
                litRooms.insert(em.room);
                const auto e = FurniturePlacer::emitterFor(em.type);
                if (!ctx.deps.addPointLight) continue;   // headless: fixtures only
                const glm::vec3 pos(em.microPos.x / 9.0f + 0.5f,
                                    (em.microPos.y + e.emitMicroY) / 9.0f,
                                    em.microPos.z / 9.0f + 0.5f);
                const int id = ctx.deps.addPointLight(pos, glm::vec3(e.r, e.g, e.b),
                                                      e.intensity, e.radius);
                if (id < 0) {
                    LOG_WARN_FMT("StructureBuild", "place_lights: light capacity reached — "
                                 << em.type << " in '" << em.room << "' is UNLIT");
                    continue;
                }
                ++lightsRegistered;
                // Record the id on the fixture so a rebuild/removal can tear the light
                // down with it. NOTE (StructurePipelineGaps): LightManager lights are
                // NOT world-persisted, so they do not survive save/load — recorded, not
                // faked.
                if (!em.objectId.empty())
                    placedObjectManager->setMetadata(em.objectId, "light",
                                                     {{"id", id}, {"type", em.type}});
            }
            emitters.clear();

            // Dark-room check (checklist K8): a habitable room wants SOME light —
            // daylight through a window, a hearth, or a lamp. Reported, not refused:
            // a windowless store or byre is legitimately dark, and refusing a build
            // over ambience would be the gate overreaching.
            for (const auto& rm : story.rooms) {
                if (litRooms.count(rm.id)) continue;
                bool hasWindow = false;
                for (const auto& p : story.portals)
                    if (p.kind == "window" && (p.a == rm.id || p.b == rm.id)) { hasWindow = true; break; }
                if (hasWindow) continue;
                darkRooms.push_back({{"room", rm.id}, {"purpose", rm.purpose},
                                     {"story", static_cast<int>(si)}});
            }
            litRooms.clear();

            // ---- M7 DOORWAY CLEARANCE: the realized check. Furniture avoids doors
            // in CUBE cells at plan time, but a piece renders at micro precision and
            // spills past its reserved footprint, so a piece that "fits" beside a
            // door can still stand in it. Scan the real placed AABBs against the real
            // carved openings, and REPAIR by removing the offender — a blocked
            // doorway is a defect, and a room you cannot walk into is worse than a
            // room missing a stool.
            {
                auto blocked = RealizedStructureValidator::checkDoorwayClearance(
                    ctx.shell.plan, glm::ivec3(posX, ctx.oy, posZ), placedBoxes);
                if (!blocked.ok()) {
                    std::set<std::string> offenders;
                    for (const auto& is : blocked.issues()) offenders.insert(is.where);
                    for (auto& b : placedBoxes) {
                        const std::string key = b.room.empty() ? b.objectId : b.room;
                        if (!offenders.count(key) || b.objectId.empty()) continue;
                        // Only the pieces actually overlapping a doorway are removed —
                        // re-run the check for THIS box alone to avoid evicting a
                        // room-mate that merely shares the room label.
                        auto one = RealizedStructureValidator::checkDoorwayClearance(
                            ctx.shell.plan, glm::ivec3(posX, ctx.oy, posZ), {b});
                        if (one.ok()) continue;
                        // REPAIR, then refuse: try to SLIDE it clear before deleting.
                        // Candidate offsets walk out from the doorway along both
                        // horizontal axes in whole cubes; a candidate is accepted
                        // only if it clears every doorway AND overlaps nothing else
                        // already placed. Bounded (a few cubes), deterministic, and
                        // it never moves a piece into another piece.
                        const auto rs = reseat.find(b.objectId);
                        bool moved = false;
                        // The slid piece must stay INSIDE its own room. Without this
                        // a "2 cubes clear" slide pushed an upstairs bed straight
                        // through the gable wall, leaving it hanging in mid-air over
                        // the grass — visible from outside, and a worse defect than
                        // the blocked doorway it was fixing.
                        const ProgRoom* homeRoom = nullptr;
                        for (const auto& rm : story.rooms)
                            if (rm.id == b.room) { homeRoom = &rm; break; }
                        if (rs != reseat.end() && homeRoom) {
                            const glm::ivec3 span = b.hi - b.lo;
                            const int roomLoX = (posX + homeRoom->rect.x) * 9;
                            const int roomLoZ = (posZ + homeRoom->rect.z) * 9;
                            const int roomHiX = roomLoX + homeRoom->rect.w * 9;
                            const int roomHiZ = roomLoZ + homeRoom->rect.d * 9;
                            for (int step = 1; step <= 3 && !moved; ++step) {
                                for (const glm::ivec3& dir : {glm::ivec3(1, 0, 0),
                                                              glm::ivec3(-1, 0, 0),
                                                              glm::ivec3(0, 0, 1),
                                                              glm::ivec3(0, 0, -1)}) {
                                    const glm::ivec3 d = dir * (step * 9);   // whole cubes
                                    RealizedStructureValidator::PlacedBox cand = b;
                                    cand.lo = b.lo + d;
                                    cand.hi = cand.lo + span;
                                    // Must stay within its own room's footprint —
                                    // never slid out through a wall.
                                    if (cand.lo.x < roomLoX || cand.hi.x > roomHiX ||
                                        cand.lo.z < roomLoZ || cand.hi.z > roomHiZ)
                                        continue;
                                    // Must not walk into another placed fixture.
                                    bool hits = false;
                                    for (const auto& o : placedBoxes) {
                                        if (o.objectId.empty() || o.objectId == b.objectId) continue;
                                        if (cand.lo.x < o.hi.x && cand.hi.x > o.lo.x &&
                                            cand.lo.y < o.hi.y && cand.hi.y > o.lo.y &&
                                            cand.lo.z < o.hi.z && cand.hi.z > o.lo.z) {
                                            hits = true; break;
                                        }
                                    }
                                    if (hits) continue;
                                    // Must actually clear the doorway it was blocking.
                                    if (!RealizedStructureValidator::checkDoorwayClearance(
                                            ctx.shell.plan, glm::ivec3(posX, ctx.oy, posZ),
                                            {cand}).ok())
                                        continue;
                                    // Commit: re-place the SAME template at the slid pose.
                                    placedObjectManager->remove(b.objectId);
                                    const std::string nid = placedObjectManager->placeTemplateMicro(
                                        rs->second.tmpl, rs->second.microPos + d,
                                        rs->second.rotation, objectId);
                                    if (nid.empty()) {      // re-place failed: it stays gone
                                        LOG_WARN_FMT("StructureBuild", "doorway repair: could not "
                                                     "re-place " << b.type << " in '" << b.room
                                                     << "' — it is removed");
                                        break;
                                    }
                                    LOG_INFO_FMT("StructureBuild", "doorway blocked by " << b.type
                                                 << " in '" << b.room << "' — SLID it "
                                                 << (step) << " cube(s) clear (a blocked doorway is "
                                                    "a defect; an empty room is a worse fix)");
                                    unblockedDoors.push_back({{"type", b.type}, {"room", b.room},
                                                              {"action", "relocated"},
                                                              {"cubes", step}});
                                    // CARRY WHAT IT HOLDS. A surface fixture keeps its
                                    // recorded top-surface pose so the tableware
                                    // scattered onto it at the end of this stage lands
                                    // on the wood, not where the wood used to be.
                                    int carried = 0;
                                    for (auto& ps : placedSurfaces)
                                        if (ps.objectId == b.objectId) {
                                            ps.microPos += d;
                                            ps.objectId = nid;
                                            ++carried;
                                        }
                                    if (carried > 0)
                                        LOG_INFO_FMT("StructureBuild", "  ...carrying "
                                                     << carried << " laid surface(s) with it");
                                    b.objectId = nid;
                                    b.lo = cand.lo;
                                    b.hi = cand.hi;
                                    reseat[nid] = {rs->second.tmpl, rs->second.microPos + d,
                                                   rs->second.rotation};
                                    moved = true;
                                    break;
                                }
                            }
                        }
                        if (moved) continue;
                        LOG_WARN_FMT("StructureBuild", "doorway blocked by " << b.type
                                     << " in '" << b.room << "' — no clear spot within 3 cubes, "
                                        "REMOVING it (a blocked doorway is worse than a missing "
                                        "fixture)");
                        // Deleting a surface fixture deletes what it was going to hold —
                        // otherwise its tableware spawns onto a table that is not there.
                        int dropped = 0;
                        for (auto it = placedSurfaces.begin(); it != placedSurfaces.end();)
                            if (it->objectId == b.objectId) { it = placedSurfaces.erase(it); ++dropped; }
                            else ++it;
                        placedObjectManager->remove(b.objectId);
                        --fxSpawned;
                        unblockedDoors.push_back({{"type", b.type}, {"room", b.room},
                                                  {"action", "removed"},
                                                  {"laid_surfaces_dropped", dropped}});
                        if (dropped > 0)
                            LOG_WARN_FMT("StructureBuild", "  ...it was a laid surface; "
                                         << dropped << " item set(s) dropped with it");
                        b.objectId.clear();
                        b.lo = b.hi = glm::ivec3(0);   // no longer occupies anything
                    }
                }
            }
            placedBoxes.clear();
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
        // WHICH sign a trade hangs is DATA (RoomProgram::signItem), never a name
        // chosen here. The asset is a fine-voxel ITEM carrying a real painted
        // board — `hanging_sign` (a blank furniture board that cannot say
        // anything) is only the fallback for trades whose sign is not authored
        // yet, and those record an asset request instead of borrowing another
        // trade's sign.
        std::string signItemId = ctx.rp ? ctx.rp->signItem : std::string();
        const VoxelTemplate* signTmpl = nullptr;
        if (!signItemId.empty() && itemPropManager) {
            if (const auto* def = ItemRegistry::instance().getItem(signItemId)) {
                if (def->holdable && !def->templateFile.empty())
                    signTmpl = itemPropManager->resolveItemTemplate(def->templateFile);
            }
            if (!signTmpl) {
                LOG_WARN_FMT("StructureBuild", "place_signage: declared sign item '"
                             << signItemId << "' did not resolve — falling back to the "
                             "blank board");
                signItemId.clear();
            }
        } else {
            signItemId.clear();
        }
        const bool haveSign = signTmpl || (objectTemplateManager &&
            objectTemplateManager->getTemplate("hanging_sign") != nullptr);
        if (!signTmpl && !program.stories.empty() &&
            isBusiness(program.typology, program.function)) {
            // Honest demand, not a silent blank: this trade has no sign asset,
            // so it hangs the nameless board and the gap is RECORDED. Signage is
            // decorative, so this records rather than refuses (unlike the
            // fixture gate) — but it is never invented and never borrowed from
            // another trade.
            const std::string trade = program.typology.empty() ? program.function
                                                               : program.typology;
            const std::vector<AssetRequest> reqs = {
                {"sign_" + trade, "item", "signage", trade, "unmapped",
                 "typology '" + trade + "' hangs a trade sign but has no authored "
                 "sign asset (room_program.json \"sign_item\") — it is showing the "
                 "blank hanging_sign board"}};
            AssetRequestLedger::save(AssetRequestLedger::merge(
                AssetRequestLedger::load(), reqs,
                ctx.params.value("today", std::string("unknown"))));
            response["signage_asset_request"] = AssetRequestLedger::toJson(reqs);
            LOG_WARN_FMT("StructureBuild", "asset request: " << reqs[0].message);
        }
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
            WallSide side = WallSide::MinusZ;         // default: -Z front wall
            int rotation = 180;                       // default: -Z front wall
            int wallOuterMicro = oz * 9;              // -Z outer face
            int alongCenterMicro = (ox * 9) + (Wp * 9) / 2;
            int doorHeadMicroY = floorMicroY + 3 * 9; // default door height 3 cubes
            if (door) {
                doorHeadMicroY = floorMicroY + std::max(1, door->height) * 9;
                const int dw = std::max(1, door->width);
                if (door->px == 0) {              // -X wall
                    side = WallSide::MinusX;
                    rotation = 90;
                    wallOuterMicro = ox * 9;
                    alongCenterMicro = (oz + door->pz) * 9 + dw * 9 / 2;
                } else if (door->px == Wp) {      // +X wall
                    side = WallSide::PlusX;
                    rotation = 270;
                    wallOuterMicro = (ox + Wp) * 9;
                    alongCenterMicro = (oz + door->pz) * 9 + dw * 9 / 2;
                } else if (door->pz == Dp) {      // +Z wall
                    side = WallSide::PlusZ;
                    rotation = 0;
                    wallOuterMicro = (oz + Dp) * 9;
                    alongCenterMicro = (ox + door->px) * 9 + dw * 9 / 2;
                } else {                          // -Z wall (pz==0 or interior fallback)
                    side = WallSide::MinusZ;
                    rotation = 180;
                    wallOuterMicro = oz * 9;
                    alongCenterMicro = (ox + door->px) * 9 + dw * 9 / 2;
                }
            }
            // Board dimensions are MEASURED from the asset, never assumed — the
            // mount form (bracket vs facade) is decided by whether the real board
            // fits the grounded projection cap.
            float boardW = 7.0f / 9.0f, boardH = 7.0f / 9.0f, boardT = 2.0f / 9.0f;
            if (signTmpl) {
                glm::vec3 dims(0.0f);
                if (detail::templateSizeUnits(*signTmpl, dims)) {
                    boardW = dims.x; boardH = dims.y; boardT = dims.z;
                }
            }
            const SignMount mount = planSignMount(
                side, wallOuterMicro, alongCenterMicro, floorMicroY, doorHeadMicroY,
                roofApexWorldMicro, boardW, boardH, boardT);
            if (!mount.ok) {
                LOG_WARN("StructureBuild", "place_signage: SKIPPED — " + mount.skipReason);
                response["signage_skipped"] = mount.skipReason;
            } else {
                std::string sid;
                if (signTmpl) {
                    // The real painted board: a fine-voxel ITEM prop (static-first,
                    // so a hung sign costs no physics), parented to the structure.
                    sid = itemPropManager->spawnProp(
                        signItemId, mount.worldPos, (float)mount.rotationDeg,
                        /*snapToGround=*/false, /*instanceUuid=*/"",
                        glm::vec3(0.0f), /*dynamic=*/false);
                    if (!sid.empty()) {
                        placedObjectManager->setParent(sid, objectId);
                        ++itemsSpawned;
                    }
                } else {
                    // Fallback: the blank furniture board on its bracket. Its
                    // min-corner convention differs from the item's center anchor.
                    glm::ivec3 sm(0, mount.boardBottomMicroY, 0);
                    switch (rotation) {
                        case 0:   sm.x = alongCenterMicro; sm.z = wallOuterMicro;     break;
                        case 180: sm.x = alongCenterMicro; sm.z = wallOuterMicro - 6; break;
                        case 270: sm.x = wallOuterMicro;   sm.z = alongCenterMicro;   break;
                        case 90:  sm.x = wallOuterMicro - 6; sm.z = alongCenterMicro; break;
                        default: break;
                    }
                    sid = placedObjectManager->placeTemplateMicro("hanging_sign", sm,
                                                                  rotation, objectId);
                    if (!sid.empty()) ++fxSpawned;
                }
                if (!sid.empty()) {
                    nlohmann::json sj = {
                        {"id", sid}, {"structure", objectId},
                        {"asset", signTmpl ? signItemId : std::string("hanging_sign")},
                        {"realized_as", signTmpl ? "item" : "template"},
                        {"mount", mount.form},
                        {"rotation", signTmpl ? mount.rotationDeg : rotation},
                        {"board_bottom_micro_y", mount.boardBottomMicroY},
                        {"clearance_micro", mount.boardBottomMicroY - floorMicroY},
                        {"above_lintel_micro", mount.boardBottomMicroY - doorHeadMicroY},
                        {"projection_micro", mount.projectionMicro},
                        {"over_door", door != nullptr},
                        {"clearance_ok", true}};   // gated: only reached when ok
                    placedObjectManager->setMetadata(sid, "signage", sj);
                    response["signage"] = sj;
                    LOG_INFO_FMT("StructureBuild", "place_signage: hung "
                                 << (signTmpl ? signItemId : std::string("hanging_sign"))
                                 << " (" << mount.form << ") over "
                                 << (door ? "entry door" : "front wall")
                                 << " (clearance " << (mount.boardBottomMicroY - floorMicroY)
                                 << " micro, above lintel "
                                 << (mount.boardBottomMicroY - doorHeadMicroY)
                                 << " micro, rot " << mount.rotationDeg << ")");
                } else {
                    LOG_WARN("StructureBuild", "place_signage: sign spawn failed");
                }
            }
        }
        response["fixtures_spawned"] = fxSpawned;
        response["items_spawned"] = itemsSpawned;   // pickable surface props (2026-08-07)
        response["chimneys_built"] = chimneysBuilt;                 // M4 chimney pass
        if (!displacedByPost.empty())
            response["displaced_existing_voxels"] = displacedByPost;   // destructive-write ledger
        if (!fluelessRemoved.empty())
            response["flueless_hearths_removed"] = fluelessRemoved; // never ship a flueless hearth
        response["lights_registered"] = lightsRegistered;           // M5 lighting pass
        if (!unblockedDoors.empty())
            response["doorway_blockers_removed"] = unblockedDoors;  // M7 clearance repair
        if (!darkRooms.empty()) {
            response["dark_rooms"] = darkRooms;                     // no window, hearth or lamp
            LOG_WARN_FMT("StructureBuild", "place_lights: " << darkRooms.size()
                         << " room(s) have no window, hearth or lamp");
        }
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
