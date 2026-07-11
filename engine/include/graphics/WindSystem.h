#pragma once

#include <glm/glm.hpp>

namespace Phyxel {
namespace Graphics {

/// Global procedural wind state shared by every wind consumer (grass + foliage today; trees,
/// particles, weather later — see docs/VegetationWindPlan.md Phase 1).
///
/// CPU side is a few flops per frame: user-facing Settings (mean direction, speed, gustiness)
/// are drifted through low-frequency 1D value noise into a per-frame State. RenderCoordinator
/// ticks this once per drawFrame and writes the SAME State into both vegetation pipelines'
/// params, so grass and foliage can never see diverging wind. The shaders then evaluate the
/// actual travelling gust field analytically from these scalars (shaders/wind.glsl) — the
/// field itself never touches the CPU or the shared UniformBufferObject.
class WindSystem {
public:
    /// User-facing knobs (POST /api/debug/wind). These are drift targets, not per-frame values.
    struct Settings {
        float dirDegrees = 25.0f;  ///< mean direction in the XZ plane (0 = +X, CCW toward +Z)
        float speed      = 0.5f;   ///< 0 = dead calm (vegetation perfectly still) .. 1 = storm
        float gustiness  = 0.55f;  ///< 0 = steady laminar flow .. 1 = strongly gusting
    };

    /// Per-frame derived state consumed by the vegetation shaders via push constants.
    struct State {
        glm::vec2 dir       {1.0f, 0.0f};  ///< unit XZ wind direction (slowly wandering)
        float     base      = 0.0f;        ///< steady bend strength (normalized, ~0..1)
        float     gustAmp   = 0.0f;        ///< gust bend amplitude riding on top of base
        float     gustScale = 0.045f;      ///< gust spatial frequency (1 / world units)
        float     gustSpeed = 6.0f;        ///< gust front travel speed (world units / second)
    };

    Settings&       settings()       { return m_settings; }
    const Settings& settings() const { return m_settings; }
    const State&    state()    const { return m_state; }

    /// Advance the drift model to `timeSeconds` — pass UBO.elapsedTime, the same clock the
    /// shaders scroll the gust field with. Deterministic: State is a pure function of
    /// (Settings, timeSeconds), so identical inputs reproduce identical wind.
    void tick(float timeSeconds);

private:
    static float hash1(int i);      // deterministic integer hash -> [0,1)
    static float noise1(float x);   // smooth 1D value noise in [0,1]

    Settings m_settings;
    State    m_state;
};

} // namespace Graphics
} // namespace Phyxel
