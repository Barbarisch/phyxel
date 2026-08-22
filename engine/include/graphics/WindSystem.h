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
        // DEFAULTS ARE THE 2026-08-05 APPROVED LOOK, tuned live against the wind debug view
        // (/api/debug/shadow {"mode":2}) and signed off by eye. Changing any of them changes the
        // shipped wind; the derivations below are calibrated so THESE values land on it.
        float dirDegrees = 15.0f;  ///< mean direction in the XZ plane (0 = +X, CCW toward +Z)
        float speed      = 0.35f;  ///< 0 = dead calm (vegetation perfectly still) .. 1 = storm
        float gustiness  = 0.45f;  ///< 0 = steady laminar flow .. 1 = strongly gusting
        /// TUNING OVERRIDES (negative = derive from speed/gustiness as normal).
        /// gustScale/gustSpeed are normally recomputed every update() from speed+gustiness, which
        /// means a value poked into State lasts exactly one frame — long enough to look like the
        /// knob works and short enough that it never actually did anything. These live in the
        /// SETTINGS, which persist, so an override survives.
        /// Needed because the derived gustScale only spans 0.055..0.033 (see the curve in
        /// WindSystem.cpp tick()) — field scale by design. Tuning against
        /// tools/wind_field_probe.py requires reaching outside that range.
        float gustScaleOverride = -1.0f;
        float gustSpeedOverride = -1.0f;
    };

    /// Per-frame derived state consumed by the vegetation shaders via push constants.
    struct State {
        glm::vec2 dir       {1.0f, 0.0f};  ///< unit XZ wind direction (slowly wandering)
        float     base      = 0.0f;        ///< steady bend strength (normalized, ~0..1)
        float     gustAmp   = 0.0f;        ///< gust bend amplitude riding on top of base
        /// Gust spatial frequency (1 / world units). RE-DERIVED every tick() as
        /// 0.055 - 0.022*gustiness (0.045 at defaults: fronts ~22u deep, ~110u crosswind with
        /// aniso) — this initializer never survives a frame; use Settings::gustScaleOverride to
        /// actually pin it. (A 2026-08-05 pass lowered the curve for 42-71u fronts and was tuned
        /// back by eye; the .cpp comment carries that verdict.)
        float     gustScale = 0.045f;
        float     gustSpeed = 6.0f;        ///< gust front travel speed (world units / second)
        /// How many times longer a gust front is CROSSWIND than along-wind. 1 = isotropic blobs
        /// (what shipped before 2026-08-05 - scrolled lumps, which is why wind never read as
        /// sweeping across a field). Higher stretches fronts into bands that cross the meadow.
        /// MEASURED with tools/wind_field_probe.py, which evaluates windGustAt on the CPU:
        /// aniso 1.0 -> 0.92x measured; aniso 5.0 at gustScale 0.018 -> 3.40x, i.e. fronts about
        /// 31u deep and 107u wide, taking ~4.5s to pass at gustSpeed 7.
        float     aniso     = 5.0f;
        /// ACCUMULATED gust-field scroll, world units. Integrated as dir*gustSpeed*dt every tick.
        /// ⚑THIS MUST NOT BE RECOMPUTED AS dir*gustSpeed*t. The wind DIRECTION wanders (+/-18 deg
        ///  at default gustiness), and multiplying a wandering direction by ELAPSED TIME means a
        ///  small heading change displaces the whole field by dTheta*gustSpeed*t — proportional to
        ///  how long the engine has been running. Measured: 0.05 noise cells/s of slew at 10s
        ///  uptime, 0.96 at 3 minutes, 3.2 at 10, 9.6 at 30. That is the "smooth for a while then
        ///  springs rapidly" the wind was reported for, and no temporal filter can fix it because
        ///  it is a bulk translation of the field, not high-frequency content.
        ///  Integrating makes a heading change affect only future motion. (2026-08-05)
        glm::vec2 scroll    = glm::vec2(0.0f);
        /// Local blade quiver, Hz. This is a DIFFERENT frequency from gustSpeed and they are
        /// easily confused:
        ///   gustSpeed   - how fast a gust FRONT crosses the field (a wave travelling past you)
        ///   flutterFreq - how fast an individual blade oscillates IN PLACE
        /// Slow-travelling waves with zero flutter read as static grass that occasionally leans;
        /// the flutter is what makes it look alive between fronts. Was hardcoded at 0.6 Hz.
        /// ⚑Its PHASE is dominantly SPATIAL (see grass.vert): a per-blade random phase makes
        ///  neighbours flutter in opposition, which is the classic "boiling grass" shimmer. Raise
        ///  the frequency if you want more life; do NOT reintroduce per-blade phase.
        float     flutterFreq = 1.8f;
    };

    Settings&       settings()       { return m_settings; }
    const Settings& settings() const { return m_settings; }
    const State&    state()    const { return m_state; }
    /// Mutable state for debug probes (/api/debug/wind). ⚑gustScale/gustSpeed/base/gustAmp are
    /// RE-DERIVED from speed+gustiness on every update(), so writes to them last one frame — they
    /// are for A/B probing against tools/wind_field_probe.py. `aniso` is not derived and persists.
    State&          mutableState()   { return m_state; }

    /// Advance the drift model to `timeSeconds` — pass UBO.elapsedTime.
    /// ⚑NO LONGER A PURE FUNCTION OF (Settings, timeSeconds). `State::scroll` is INTEGRATED from
    ///  dt, so State now depends on tick history. That is deliberate and is the fix for the
    ///  time-amplified slew described on State::scroll — a pure dir*gustSpeed*t formulation is
    ///  exactly what made a wandering heading tear the field apart on long sessions. Everything
    ///  else here remains a pure function of (Settings, timeSeconds).
    ///  Consequence: reproducing a given wind state requires replaying ticks, not just setting t.
    void tick(float timeSeconds);

private:
    static float hash1(int i);      // deterministic integer hash -> [0,1)
    static float noise1(float x);   // smooth 1D value noise in [0,1]

    Settings m_settings;
    State    m_state;
    float    m_lastT = -1.0f;   ///< previous tick time for dt; -1 = uninitialised (dt treated as 0)
};

} // namespace Graphics
} // namespace Phyxel
