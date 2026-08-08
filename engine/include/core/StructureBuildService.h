#pragma once

// ============================================================================
// StructureBuildService — the ONE engine entry point for building a generated
// structure into the live world (Structure Generation v2).
//
// Extracted from the editor's build_structure command handler so every caller
// (HTTP API command, GameDefinitionLoader "structures", build_settlement) runs
// the SAME pipeline:
//   BuildingProgram -> autofillRoomLayout(typology) -> validation gate ->
//   context-aware placement (overlap removal) -> prepare_pad / excavate_basement
//   -> StructureRealizer (subcube shell) -> place -> register + assembly_plan
//   metadata -> engine-decided furniture / chimneys / signage.
//
// The legacy v1 composite generators (house/tavern/tower, BuildingSpec) are
// REMOVED; plain house/tavern requests are aliased onto v2 typologies by the
// callers. Primitive placements (wall, staircase, furniture pieces...) reuse
// placeAndRegister so their registration/undo/navgrid behavior stays identical.
// ============================================================================

#include <functional>
#include <string>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace Phyxel {
class ChunkManager;
class ObjectTemplateManager;

namespace Core {
class PlacedObjectManager;
class LocationRegistry;
class NPCManager;
class ItemPropManager;
struct StructureResult;

class StructureBuildService {
public:
    struct Deps {
        ChunkManager* chunkManager = nullptr;            ///< REQUIRED
        PlacedObjectManager* placedObjects = nullptr;    ///< optional: no registration without it
        ObjectTemplateManager* templates = nullptr;      ///< optional: furniture/signage need it
        LocationRegistry* locations = nullptr;           ///< optional: location auto-registration
        NPCManager* npcs = nullptr;                      ///< optional: navgrid rebuild
        ItemPropManager* itemProps = nullptr;            ///< optional: surface items become
                                                         ///< pickable props (else clutter templates)
        /// Optional undo hook (the editor snapshots the region before destructive ops).
        std::function<void(const glm::ivec3& minCorner, const glm::ivec3& maxCorner,
                           const std::string& label)> pushUndo;
        /// M5 place_lights (#18): register a real engine point light for a lamp/hearth
        /// fixture. A CALLBACK because engine/core must not depend on graphics/ —
        /// the editor wires it to LightManager::addPointLight. Returns the light id,
        /// or -1 at capacity. Null (tests/headless) = fixtures are still placed, but
        /// nothing illuminates; the build reports lights_registered = 0 honestly.
        std::function<int(const glm::vec3& pos, const glm::vec3& color,
                          float intensity, float radius)> addPointLight;
    };

    /// Full v2 build from build_structure params (expects footprint/stories/...;
    /// the "schema":"v2" tag is the caller's routing concern, not validated here).
    /// Returns the API response json ({"success":true,...} or {"error":...}).
    static nlohmann::json buildV2(const nlohmann::json& params, const Deps& deps);

    /// Shared placement tail for an already-generated StructureResult (primitives:
    /// wall/staircase/furniture/...): undo snapshot, place, honest-zero check,
    /// location + placed-object registration, grass clearing, navgrid rebuild.
    /// `planMeta` (optional) is persisted as metadata "assembly_plan".
    static nlohmann::json placeAndRegister(const StructureResult& structure,
                                           const nlohmann::json& params, const Deps& deps,
                                           const nlohmann::json& planMeta = nullptr);

    /// Map a legacy v1 composite request ({"type":"house"|"tavern", width/depth/
    /// stories/...}) onto v2 build params (typology + style + footprint + stories).
    /// Returns nullptr json if the type has no v2 alias (e.g. "tower").
    static nlohmann::json aliasLegacyParams(const nlohmann::json& params);

    /// Snap a location anchor to the nearest STANDABLE cell (solid floor below,
    /// 2 cells of air) within `radius`, searching XZ rings outward and Y within
    /// +/-3 of the anchor. Derived anchors can land in dead columns (under an eave
    /// overhang, on a fence line) where pathfinding cannot resolve a goal —
    /// measured live: an unsnapped tavern anchor left 12 residents `no_route`.
    /// Returns the standing cell (feet position), or `cell` unchanged if nothing
    /// standable is found in range. Pure over `solidAt` — unit-testable.
    /// `avoid` (optional) rejects candidate cells — e.g. the structure's own XZ
    /// footprint, so an anchor never snaps INSIDE the building (interiors are
    /// standable floor, but the NavGraph cannot yet route exterior->interior, so
    /// an indoor anchor is a dead schedule target).
    static glm::ivec3 snapToStandable(const std::function<bool(const glm::ivec3&)>& solidAt,
                                      const glm::ivec3& cell, int radius = 5,
                                      const std::function<bool(const glm::ivec3&)>& avoid = {});
};

} // namespace Core
} // namespace Phyxel
