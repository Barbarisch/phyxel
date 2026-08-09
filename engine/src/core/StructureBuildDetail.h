#pragma once

// ============================================================================
// src-local internals shared between StructureBuildService.cpp (placement tail)
// and StructureForge.cpp (the staged v2 build orchestrator). NOT a public
// engine include — both consumers live in engine/src/core.
// ============================================================================

#include <chrono>
#include <climits>
#include <string>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include "core/StructureBuildService.h"
#include "core/VoxelTemplate.h"

namespace Phyxel {
namespace Core {
struct StructureResult;

namespace detail {

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
// clearing under the footprint, navgrid rebuild. Defined in StructureBuildService.cpp.
PlaceOutcome placeAndRegisterImpl(const StructureResult& structure, const nlohmann::json& params,
                                  const StructureBuildService::Deps& deps,
                                  const nlohmann::json& planMeta, bool doSnapshot);

// Template .metrics.json sidecar (footprints, hearth heights). Parse failures are
// treated as "no sidecar" — better to allow than to wrongly block.
// Defined in StructureBuildService.cpp.
nlohmann::json loadAssetMetricsSidecar(const std::string& templateName);

// MEASURED template extent in world units, across every voxel tier (fine grid,
// microcubes, subcubes, cubes). Callers that need a real asset's size must read
// it from the asset — assuming dimensions is how a 2 m sign board ends up
// planned as a 0.78 m one. False when the template has no geometry.
// Defined in StructureBuildService.cpp.
bool templateSizeUnits(const VoxelTemplate& tmpl, glm::vec3& outDims);

} // namespace detail
} // namespace Core
} // namespace Phyxel
