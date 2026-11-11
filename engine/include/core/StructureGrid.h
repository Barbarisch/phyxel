#pragma once
// docs/VoxelFoundationPlan.md §7a.3 step 3 — the structure generator's length unit.
//
// Every structure generator paints whole-metre plans (rooms, footprints, storeys in cubes = metres) into a
// fine canvas. Until 2026-11-11 the fine cell was hard-wired to 1/9 m (`* 9`, `/ 3` at ~300 sites). The grid
// is now a value: `legacy()` = 9 cells per metre (today's chunk store; output byte-identical, pinned by
// BuildingHarness.ForgeParityDigests), `foundation()` = 8 cells per metre (the voxel-foundation grid).
//
// Design constants that were AUTHORED in ninths are re-picked per grid here, with the metre value each one
// stands for. They are gameplay/grounding numbers, never a rescale of the ninth value.

#include <cmath>

namespace Phyxel {
namespace Core {

/// Floor division for b > 0 (negative a rounds toward -infinity). The one helper for cell -> metre index.
constexpr int floorDiv(int a, int b) {
    return (a >= 0) ? a / b : -((-a + b - 1) / b);
}
/// Non-negative remainder for b > 0.
constexpr int floorMod(int a, int b) {
    return a - floorDiv(a, b) * b;
}

struct StructureGrid {
    int perM = 9;   ///< cells per metre (one cube = one metre = perM cells)

    static constexpr StructureGrid legacy() { return StructureGrid{9}; }
    static constexpr StructureGrid foundation() { return StructureGrid{8}; }
    constexpr bool isLegacy() const { return perM == 9; }

    /// Nearest whole number of cells to `metres`.
    int m(double metres) const { return static_cast<int>(std::lround(metres * perM)); }
    /// Cells spanned by `n` whole metres (cubes).
    constexpr int cubes(int n) const { return n * perM; }
    /// The metre (cube) index containing cell `c`.
    constexpr int cubeOf(int c) const { return floorDiv(c, perM); }
    double toM(int cells) const { return static_cast<double>(cells) / perM; }

    // ---- re-picked design constants (cells), with the metres each stands for ----
    /// Character auto step-up. 9: 4 = 0.444 m (m_maxStepHeight). 8: 3 = 0.375 m, chosen BELOW today's
    /// controller (0.444 m) so stairs built on the new grid stay climbable by the character that exists;
    /// the new-grid controller's own step-up is an F7 decision (plan §7a.2 suggests 4/8 m).
    constexpr int stepUp() const { return perM == 9 ? 4 : 3; }
    /// Comfortable riser and shallowest tread. 9: 2 = 0.222 m. 8: 2 = 0.25 m.
    constexpr int comfortRiser() const { return 2; }
    constexpr int minTread() const { return 2; }
    /// Stairwell guard band. 9: 8 = 0.889 m. 8: 8 = 1.0 m (meets the IRC 36 in = 0.914 m minimum, which
    /// 0.889 m did not; 7 = 0.875 m would not).
    constexpr int guardHeight() const { return 8; }
    /// Walking agent. Half-width 0.25 m: 9 -> 2 (0.222), 8 -> 2 (0.25). Height 1.87 m (AgentProfile.h):
    /// 9 -> 17 (1.889), 8 -> 15 (1.875).
    constexpr int agentHalfWidth() const { return 2; }
    constexpr int agentHeight() const { return perM == 9 ? 17 : 15; }
};

}  // namespace Core
}  // namespace Phyxel
