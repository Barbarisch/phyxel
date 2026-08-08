#include "core/StructureBuildService.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <cmath>
#include <filesystem>
#include <fstream>
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
#include "core/LocationRegistry.h"
#include "core/NPCManager.h"
#include "core/ObjectTemplateManager.h"
#include "core/ItemPropManager.h"
#include "core/PlacedObjectManager.h"
#include "core/RealizedStructureValidator.h"
#include "core/RoomLayout.h"
#include "core/RoomProgram.h"
#include "core/StructureForge.h"
#include "core/StructureGenerator.h"
#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"
#include "utils/Logger.h"

namespace fs = std::filesystem;

namespace Phyxel {
namespace Core {
namespace detail {

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

// Shared placement tail: undo snapshot (optional), place, honest-zero check,
// location + placed-object registration (+ assembly_plan metadata), grass
// clearing under the footprint, navgrid rebuild. (PhaseClock/PlaceOutcome live
// in StructureBuildDetail.h, shared with StructureForge.cpp.)
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
        // Snap each anchor to a standable cell BEFORE registering: derived anchors
        // can land in dead columns (eave overhang, fence line) where the NavGraph
        // cannot resolve a goal node and every scheduled NPC gets no_route.
        if (chunkManager) {
            auto solidAt = [&](const glm::ivec3& p) {
                return chunkManager->hasVoxelAt(p);
            };
            // Never snap INTO the structure's own XZ footprint: interior floor is
            // standable but the NavGraph can't route exterior->interior yet, so an
            // indoor anchor is a dead schedule target (measured: 12x no_route).
            auto insideBuilding = [&](const glm::ivec3& p) {
                return p.x >= out.smin.x && p.x <= out.smax.x &&
                       p.z >= out.smin.z && p.z <= out.smax.z;
            };
            for (auto& loc : locations) {
                const glm::ivec3 cell(static_cast<int>(std::floor(loc.position.x)),
                                      static_cast<int>(std::floor(loc.position.y)),
                                      static_cast<int>(std::floor(loc.position.z)));
                const glm::ivec3 snapped = StructureBuildService::snapToStandable(
                    solidAt, cell, 5, insideBuilding);
                loc.position = glm::vec3(snapped.x + 0.5f, static_cast<float>(snapped.y),
                                         snapped.z + 0.5f);
            }
        }
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
        if (const auto* structObj = placedObjectManager->get(out.objectId))
            out.response["object_uuid"] = structObj->uuid;  // stable id for later query/move/remove
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

} // namespace detail

using detail::PhaseClock;
using detail::PlaceOutcome;
using detail::placeAndRegisterImpl;
using detail::loadAssetMetricsSidecar;

nlohmann::json StructureBuildService::placeAndRegister(const StructureResult& structure,
                                                       const nlohmann::json& params,
                                                       const Deps& deps,
                                                       const nlohmann::json& planMeta) {
    if (!deps.chunkManager) return {{"error", "ChunkManager not available"}};
    if (structure.voxels.empty())
        return {{"error", "Failed to generate structure (unknown type or invalid params)"}};
    return placeAndRegisterImpl(structure, params, deps, planMeta, /*doSnapshot=*/true).response;
}

glm::ivec3 StructureBuildService::snapToStandable(
    const std::function<bool(const glm::ivec3&)>& solidAt,
    const glm::ivec3& cell, int radius,
    const std::function<bool(const glm::ivec3&)>& avoid) {
    // Standable = solid floor underfoot + 2 cells of clear air (feet + head).
    auto standable = [&](const glm::ivec3& p) {
        return solidAt(glm::ivec3(p.x, p.y - 1, p.z)) &&
               !solidAt(p) && !solidAt(glm::ivec3(p.x, p.y + 1, p.z));
    };
    // Same-level FIRST across the whole radius, then |dy| outward: a lateral step to
    // open ground must beat climbing onto whatever capped the column (an eave/roof at
    // dy=+3 is "standable" but unreachable — the exact dead-anchor case).
    static constexpr int kDy[] = {0, 1, -1, 2, -2, 3, -3};
    for (int dy : kDy) {
        for (int r = 0; r <= radius; ++r) {
            for (int dx = -r; dx <= r; ++dx) {
                for (int dz = -r; dz <= r; ++dz) {
                    if (std::max(std::abs(dx), std::abs(dz)) != r) continue;   // ring shell only
                    const glm::ivec3 p(cell.x + dx, cell.y + dy, cell.z + dz);
                    if (avoid && avoid(p)) continue;
                    if (standable(p)) return p;
                }
            }
        }
    }
    return cell;   // nothing standable in range — leave unchanged (honest no-op)
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
    // M1 restage: the full staged pipeline lives in StructureForge (same wire
    // format, same Deps; response additionally carries "gates").
    return StructureForge::run(params, deps);
}

} // namespace Core
} // namespace Phyxel
