#pragma once

// Engine-wide world-scale constants shared across subsystems. A value lives here when TWO OR MORE
// subsystems must agree on it and drift between their private copies is a latent bug.

namespace Phyxel {
namespace Core {

// Global sea-level Y — the SINGLE declaration (docs/TerrainGenerationV2.md §P0 grounded-values
// table). Chosen for engineering continuity (Flat worlds stay at Y=16), NOT a geographic figure:
// world Y is an arbitrary unbounded origin (auditor-flagged as an unsourceable convenience, so the
// rationale is declared instead). Consumers: WorldGenerator (terrain/hydrology bake),
// WaterManager (sim ocean level), RenderCoordinator (flat sea-plane height). Per-world overrides
// come from game.json `water.seaLevel`; all defaults MUST route through this constant —
// re-declaring a private default is how the render-16-vs-sim-0 drift happened (WaterSystemV2
// Phase A wrap-up, 2026-07-10).
inline constexpr float kSeaLevelY = 16.0f;

} // namespace Core
} // namespace Phyxel
