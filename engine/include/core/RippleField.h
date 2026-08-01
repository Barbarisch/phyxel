#pragma once

#include <glm/glm.hpp>
#include <vector>

namespace Phyxel {
namespace Core {

// ── Ripple / disturbance heightfield (small-scale water plan Phase 3) ─────────────────────────
//
// A small damped 2D wave-equation heightfield that follows the player and carries the water
// surface's LOCAL DYNAMIC disturbances — impact rings, footstep wakes, splash rims — the thing
// neither the CA (mass, no surface dynamics) nor the shader animation (global, time-driven,
// nothing to poke) can express. Entities inject impulses (WaterManager::addRipple); the renderer
// samples the field by WORLD XZ as a height offset + normal gradient on the water surface.
//
// PURELY VISUAL BY DESIGN: it never touches CA mass, so conservation, settling and the
// active-set invariants are unaffected by construction.
//
// CPU on purpose (not compute): 128×128 cells at half-voxel pitch is a ~16K-cell stencil —
// microseconds per tick — and a CPU field is unit-testable in the same discipline as the CA.
// The renderer uploads the height grid to a small R32F texture each frame it changed.
//
// Recentring follows the water sim region's pattern: the window shifts by WHOLE CELLS so crests
// stay world-stationary; content shifted past the edge is dropped (a ripple leaving the window
// is over anyway). All-zero fields sleep: tick() early-outs until the next impulse.
class RippleField {
public:
    // cells = grid resolution per axis; pitch = world units per cell (0.5 = half-voxel).
    // Default window: 128 × 0.5 = 64 world units — matches the CA region footprint.
    RippleField(int cells = 128, float pitch = 0.5f);

    // Advance the wave equation by dt (seconds). Substeps internally so stability never depends
    // on the caller's frame rate. No-op (O(1)) while the field is asleep.
    void tick(float dt);

    // Inject a smooth (cosine-cap) impulse at a world position: `strength` is the peak height
    // (world units, may be negative for a trough), `radius` in world units. Points outside the
    // window are ignored. Wakes the field.
    void addImpulse(const glm::vec2& worldXZ, float radius, float strength);

    // Keep the window centred on `focusWorld` (XZ), recentring only when the focus drifts more
    // than `hysteresis` world units from the window centre. Shifts by whole cells so existing
    // waves stay world-stationary. Returns true if it recentred.
    bool followTo(const glm::vec2& focusWorld, float hysteresis);

    // Height at a world position (bilinear); 0 outside the window or while asleep.
    float heightAt(const glm::vec2& worldXZ) const;

    // True when the field holds no energy (tick() is O(1)).
    bool asleep() const { return m_asleep; }

    // Sum of |h| over the grid — the energy proxy tests assert decays monotonically.
    float totalAmplitude() const;

    int cells() const { return m_cells; }
    float pitch() const { return m_pitch; }
    // World-space origin (min corner) of the window.
    glm::vec2 origin() const { return m_origin; }
    float windowSize() const { return m_cells * m_pitch; }

    // Raw height grid (row-major, cells×cells) for the renderer's texture upload, plus a
    // monotonically increasing version stamp so the uploader can skip unchanged frames.
    const std::vector<float>& heights() const { return m_h; }
    unsigned long long version() const { return m_version; }

    // Wave tuning (world units / seconds). Speed must stay below pitch/dtSub·(1/√2) for
    // stability; the ctor clamps it. Damping is exponential energy loss per second.
    static constexpr float kWaveSpeed = 3.0f;   // ~3 units/s crest travel — reads as water
    static constexpr float kDamping   = 1.6f;   // ~e^-1.6 amplitude per second → rings die in ~2-3 s
    static constexpr float kSubStep   = 1.0f / 60.0f;
    // Below this total |h| the field snaps to zero and sleeps (well under one visible ripple).
    static constexpr float kSleepAmplitude = 1e-3f;

private:
    size_t idx(int x, int z) const { return static_cast<size_t>(z) * m_cells + x; }
    void substep();

    int m_cells;
    float m_pitch;
    glm::vec2 m_origin{0.0f};   // min-corner world XZ
    std::vector<float> m_h;     // current heights
    std::vector<float> m_hPrev; // previous step (verlet)
    std::vector<float> m_hNext; // scratch
    float m_accum = 0.0f;       // substep accumulator
    bool m_asleep = true;
    unsigned long long m_version = 0;
};

} // namespace Core
} // namespace Phyxel
