#include "core/RealizedStructureValidator.h"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <map>
#include <utility>

#include "core/BuildingProgram.h"
#include "core/StairPlanner.h"
#include "core/TraversalProbe.h"

namespace Phyxel {
namespace Core {

bool RealizedStructureValidator::isStoneFamily(const std::string& m) {
    return m == "Stone" || m == "Cobblestone" || m == "StoneBricks" || m == "Bricks" ||
           m == "Sandstone" || m == "Gravel";
}

bool RealizedStructureValidator::isEmissive(const std::string& m) {
    return m == "glow" || m == "glow_blue" || m == "glow_green";
}

// M1 — flora must not glow. Any emissive material in a plant template is a defect (glowing shrubs).
ValidationReport RealizedStructureValidator::checkFloraNoEmissive(
    const std::string& type, const std::vector<std::string>& materials) {
    ValidationReport rep;
    for (const auto& m : materials) {
        if (isEmissive(m)) {
            rep.addError("flora_emissive",
                "flora '" + type + "' contains emissive material '" + m + "' — plants (shrubs/bushes/"
                "trees) must not glow; remove the light-emitting blocks", type);
        }
    }
    return rep;
}

// M3 — a fireplace/chimney's masonry should be brick, not plain quarried stone. Only "fireplace"/
// "chimney" types are constrained (a forge/oven of stone is fine). Fuel Log + ember glow are allowed.
ValidationReport RealizedStructureValidator::checkHearthMasonryIsBrick(
    const std::string& type, const std::vector<std::string>& materials) {
    ValidationReport rep;
    std::string t = type;
    std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return std::tolower(c); });
    const bool isHearth = t.find("fireplace") != std::string::npos ||
                          t.find("chimney") != std::string::npos;
    if (!isHearth) return rep;
    for (const auto& m : materials) {
        if (m == "Stone" || m == "Cobblestone") {
            rep.addError("hearth_not_brick",
                type + " masonry uses '" + m + "' — a fireplace/chimney should be brick (Bricks), "
                "not plain quarried stone; replace the masonry", type);
        }
    }
    return rep;
}

// V1 — no hovering roof. A WALL column is a contiguous solid run from the bottom that reaches into the
// upper half of the structure; if there's then an AIR gap before the next solid (the roof) above it,
// the roof floats over the wall instead of resting on it. (Interior columns — floor only, low run —
// are skipped; the air above them is the room, not a hover.)
// maxGapMicro = 0: a roof must rest with NO air gap on the wall/ceiling top (any empty micro row at
// the perimeter eave is a hover). The realizer's eaveSub rounding leaves a 1-micro gap today.
ValidationReport RealizedStructureValidator::checkRoofEaveFlush(const MicroCanvas& canvas, int maxGapMicro) {
    ValidationReport rep;
    glm::ivec3 lo, hi;
    if (!canvas.microBounds(lo, hi)) return rep;
    const int totalH = hi.y - lo.y;
    if (totalH < 6) return rep;   // too short to carry a distinct roof

    std::map<std::pair<int, int>, std::vector<int>> cols;
    for (const auto& c : canvas.occupiedCells()) cols[{c.x, c.z}].push_back(c.y);

    int hoverCols = 0, worstGap = 0;
    std::pair<int, int> worstAt{0, 0};
    for (auto& kv : cols) {
        std::vector<int>& ys = kv.second;
        std::sort(ys.begin(), ys.end());
        // contiguous solid run upward from the lowest solid (the wall/floor mass)
        int runTop = ys[0];
        size_t i = 1;
        for (; i < ys.size() && ys[i] == runTop + 1; ++i) runTop = ys[i];
        if (runTop - lo.y < totalH / 2) continue;   // not a wall column (interior floor-only)
        if (i >= ys.size()) continue;               // wall contiguous to the top -> flush, no hover
        const int gap = ys[i] - runTop - 1;         // air cells between the wall top and the next solid
        if (gap > maxGapMicro) {
            ++hoverCols;
            if (gap > worstGap) { worstGap = gap; worstAt = kv.first; }
        }
    }
    if (hoverCols > 0) {
        rep.addError("roof_hover",
            "roof floats above the wall top at " + std::to_string(hoverCols) +
            " perimeter column(s) — worst air gap " + std::to_string(worstGap) +
            " micro at (" + std::to_string(worstAt.first) + "," + std::to_string(worstAt.second) +
            "); the eave must rest ON the wall/ceiling top");
    }
    return rep;
}

// V3 — material variety. If one material covers more than `maxFraction` of the realized cells, the
// structure reads as a single-material blob (walls + floor + roof all the same).
ValidationReport RealizedStructureValidator::checkMaterialContrast(const MicroCanvas& canvas,
                                                                   double maxFraction) {
    ValidationReport rep;
    std::map<std::string, int> counts;
    int total = 0;
    for (const auto& c : canvas.occupiedCells()) {
        counts[canvas.materialAt(c.x, c.y, c.z)]++;
        ++total;
    }
    if (total == 0) return rep;
    std::string dominant;
    int domCount = 0;
    for (const auto& kv : counts)
        if (kv.second > domCount) { domCount = kv.second; dominant = kv.first; }
    const double frac = static_cast<double>(domCount) / total;
    if (frac > maxFraction) {
        const int pct = static_cast<int>(frac * 100.0 + 0.5);
        rep.addError("material_monotony",
            "'" + dominant + "' is " + std::to_string(pct) + "% of the structure (> " +
            std::to_string(static_cast<int>(maxFraction * 100)) + "% threshold) — walls/floor/roof "
            "need contrasting materials, not one blob");
    }
    return rep;
}

// V4 — per-type material plausibility. A bed's bedding can't be hard stone or granular Sand.
ValidationReport RealizedStructureValidator::checkFurnitureMaterialPlausibility(
    const std::string& type, const std::vector<std::string>& materials) {
    ValidationReport rep;
    std::string t = type;
    std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return std::tolower(c); });
    const bool isBed = t.find("bed") != std::string::npos;
    if (isBed) {
        for (const auto& m : materials) {
            if (isStoneFamily(m) || m == "Sand") {
                rep.addError("implausible_material",
                    "bed is built with '" + m + "' — bedding (mattress/pillow/blanket) must be a SOFT "
                    "material (linen/wool/cloth), never stone or granular sand", type);
            }
        }
    }
    return rep;
}

// V5 — footprint diversity across a generated set. Flag a corpus that is 100% rectangles.
ValidationReport RealizedStructureValidator::checkFootprintDiversity(
    const std::vector<std::string>& footprintShapes) {
    ValidationReport rep;
    if (footprintShapes.empty()) return rep;
    int nonRect = 0;
    for (const auto& s : footprintShapes) {
        std::string t = s;
        std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return std::tolower(c); });
        if (!t.empty() && t != "rect" && t != "rectangle") ++nonRect;
    }
    if (nonRect == 0) {
        rep.addError("footprint_monotony",
            "all " + std::to_string(footprintShapes.size()) + " buildings are perfect rectangles — "
            "the generator produces no L/T/U/articulated footprint variety");
    }
    return rep;
}

// V2 — the chimney must rest ON the hearth top, integral to it: not a column diving through the hearth
// body / firebox (overlap), nor floating above. The defect was a stack built from the FLOOR, centered,
// punching up through the firebox.
ValidationReport RealizedStructureValidator::checkChimneyOnHearth(int hearthBaseMicroY,
                                                                 int hearthHeightMicro,
                                                                 int chimneyBaseMicroY, int tolMicro) {
    ValidationReport rep;
    const int hearthTopY = hearthBaseMicroY + std::max(0, hearthHeightMicro);
    if (chimneyBaseMicroY < hearthTopY - tolMicro) {
        rep.addError("chimney_overlaps_hearth",
            "chimney base at micro-Y " + std::to_string(chimneyBaseMicroY) + " dives " +
            std::to_string(hearthTopY - chimneyBaseMicroY) + " micro INTO the hearth body (top at " +
            std::to_string(hearthTopY) + ") — the stack overlaps the firebox instead of resting on the "
            "hearth; start it at the hearth top");
    } else if (chimneyBaseMicroY > hearthTopY + tolMicro) {
        rep.addError("chimney_floats_above_hearth",
            "chimney base at micro-Y " + std::to_string(chimneyBaseMicroY) + " floats " +
            std::to_string(chimneyBaseMicroY - hearthTopY) + " micro above the hearth top (" +
            std::to_string(hearthTopY) + ") — not contiguous with the hearth");
    }
    return rep;
}

// V6 — projecting shop sign clearance + projection. A hanging sign over an entrance must clear heads
// (board bottom ≥ minClearance above grade) and not jut too far from the wall (≤ maxProjection). The
// values default to the historic projecting-sign code (≥ 8 ft clearance, ≤ 48 in projection).
ValidationReport RealizedStructureValidator::checkSignClearance(int boardBottomMicroY, int groundMicroY,
                                                               int projectionMicro,
                                                               int minClearanceMicro,
                                                               int maxProjectionMicro,
                                                               int doorHeadMicroY) {
    ValidationReport rep;
    const int clearance = boardBottomMicroY - groundMicroY;
    if (clearance < minClearanceMicro) {
        rep.addError("sign_too_low",
            "hanging sign board bottom is only " + std::to_string(clearance) + " micro above grade (need "
            + std::to_string(minClearanceMicro) + " ≈ 2.44 m / 8 ft) — a person or horse would hit it; "
            "hang it higher over the entrance");
    }
    if (projectionMicro > maxProjectionMicro) {
        rep.addError("sign_over_projects",
            "hanging sign projects " + std::to_string(projectionMicro) + " micro from the wall (max "
            + std::to_string(maxProjectionMicro) + " ≈ 1.22 m / 48 in) — shorten the bracket");
    }
    if (doorHeadMicroY != INT_MIN && boardBottomMicroY < doorHeadMicroY) {
        rep.addError("sign_below_door_head",
            "hanging sign board bottom (micro-Y " + std::to_string(boardBottomMicroY) + ") is below the "
            "door head (" + std::to_string(doorHeadMicroY) + ") — it obscures the doorway instead of "
            "crowning it; hang it above the lintel");
    }
    return rep;
}

ValidationReport RealizedStructureValidator::checkShellTraversal(
        const MicroCanvas& canvas, const std::vector<int>& floorTopByStory,
        const BuildingProgram& program) {
    ValidationReport rep;
    if (program.stories.empty() || program.stories[0].rooms.empty()) return rep;
    glm::ivec3 lo, hi;
    if (!canvas.microBounds(lo, hi)) {
        rep.addError("empty_canvas", "realized canvas has no cells to traverse");
        return rep;
    }
    // Same agent the engine grounds characters with (AgentBox mirrors
    // AnimatedVoxelCharacter; step-up = the ONE shared constant).
    TraversalProbe probe([&](int x, int y, int z) { return canvas.occupiedMicro(x, y, z); },
                         AgentBox{2, 16, kCharacterStepUpMicro});
    // Start at the entrance room's centre on the ground floor (room 0 of story 0 —
    // the program gate separately guarantees an exterior door reaches it).
    const Rect& r0 = program.stories[0].rooms[0].rect;
    const glm::ivec3 start((r0.x + r0.w / 2) * 9 + 4, floorTopByStory.empty() ? 0 : floorTopByStory[0],
                           (r0.z + r0.d / 2) * 9 + 4);
    // Whole-canvas bounds: upper-story goals force the BFS through the built stairs.
    const glm::ivec3 bLo(lo.x - 9, lo.y - 2, lo.z - 9), bHi(hi.x + 9, hi.y + 9, hi.z + 9);
    for (size_t s = 0; s < program.stories.size() && s < floorTopByStory.size(); ++s) {
        const int floorY = floorTopByStory[s];
        for (size_t k = 0; k < program.stories[s].rooms.size(); ++k) {
            if (s == 0 && k == 0) continue;   // the start room
            const Rect& rc = program.stories[s].rooms[k].rect;
            // Goal = ANY standable spot in the room's interior at its floor level
            // (a centre-point goal can land inside a stairwell shaft — a room whose
            // centre is the well is still reached at its emergence tread).
            const glm::ivec3 gLo(rc.x * 9 + 2, floorY - 1, rc.z * 9 + 2);
            const glm::ivec3 gHi(rc.x1() * 9 - 3, floorY + 1, rc.z1() * 9 - 3);
            if (!probe.reachable(start, gLo, gHi, bLo, bHi))
                rep.addError("room_unreachable_realized",
                             "a character-box cannot physically reach room '" +
                             program.stories[s].rooms[k].id +
                             "' on the BUILT shell (blocked opening or unbuilt stair)",
                             "story " + std::to_string(s));
        }
    }
    return rep;
}

} // namespace Core
} // namespace Phyxel
