#include "core/RippleField.h"

#include <algorithm>
#include <cmath>

namespace Phyxel {
namespace Core {

RippleField::RippleField(int cells, float pitch)
    : m_cells(std::max(8, cells)), m_pitch(pitch > 0.0f ? pitch : 0.5f) {
    const size_t n = static_cast<size_t>(m_cells) * m_cells;
    m_h.assign(n, 0.0f);
    m_hPrev.assign(n, 0.0f);
    m_hNext.assign(n, 0.0f);
    // Centre the initial window on the world origin (followTo re-places it immediately).
    m_origin = glm::vec2(-0.5f * windowSize());
}

void RippleField::tick(float dt) {
    if (m_asleep || dt <= 0.0f) return;
    // Fixed substeps: the CFL bound is enforced per-substep, so a slow frame never destabilises
    // the stencil — it just runs more substeps (capped to avoid a spiral after a long stall).
    m_accum += std::min(dt, 0.25f);
    int steps = 0;
    while (m_accum >= kSubStep && steps < 8) {
        m_accum -= kSubStep;
        substep();
        ++steps;
    }
    if (steps == 8) m_accum = 0.0f;   // dropped time after a stall — visual field, nothing owed

    // Sleep when the energy is gone: snap to zero so the renderer's last upload is exactly flat
    // (no sub-visible residue creeping through the normal path) and further ticks are free.
    if (totalAmplitude() < kSleepAmplitude) {
        std::fill(m_h.begin(), m_h.end(), 0.0f);
        std::fill(m_hPrev.begin(), m_hPrev.end(), 0.0f);
        m_asleep = true;
        ++m_version;
    }
}

void RippleField::substep() {
    // Damped wave equation, verlet form:
    //   h' = 2h − hPrev + c²·dt²·∇²h, then damped toward h (exponential energy loss).
    // CFL: c·dt/pitch must stay ≤ 1/√2 in 2D; clamp the effective speed so a config change
    // can't destabilise the stencil.
    const float maxC = 0.7071f * m_pitch / kSubStep;
    const float c = std::min(kWaveSpeed, maxC);
    const float k2 = (c * kSubStep / m_pitch) * (c * kSubStep / m_pitch);
    // Multiplicative verlet damping: h' = d·(2h − hPrev + …) has characteristic roots of
    // magnitude √d (r² − 2dr + d = 0, complex regime), so to realize an AMPLITUDE decay of
    // e^(−kDamping·dt) the factor must be its square. (Measured the hard way: with plain
    // e^(−k·dt) the field decayed at half the intended rate and never met its sleep window.)
    const float damp = std::exp(-2.0f * kDamping * kSubStep);

    const int n = m_cells;
    for (int z = 0; z < n; ++z)
        for (int x = 0; x < n; ++x) {
            const size_t i = idx(x, z);
            // Border cells clamp to their own height (zero-gradient) and are then windowed to
            // zero below, so the rim neither reflects a hard wall nor leaks NaNs.
            const float hl = m_h[idx(std::max(x - 1, 0), z)];
            const float hr = m_h[idx(std::min(x + 1, n - 1), z)];
            const float hd = m_h[idx(x, std::max(z - 1, 0))];
            const float hu = m_h[idx(x, std::min(z + 1, n - 1))];
            const float lap = hl + hr + hd + hu - 4.0f * m_h[i];
            float next = 2.0f * m_h[i] - m_hPrev[i] + k2 * lap;
            next *= damp;
            // Window the outer 2-cell rim to zero: waves fade out at the edge of the field
            // instead of reflecting off it (a reflected ring reads as a glitch).
            const int rim = std::min(std::min(x, z), std::min(n - 1 - x, n - 1 - z));
            if (rim < 2) next *= 0.5f * rim;   // rim 0 → 0, rim 1 → 0.5
            m_hNext[i] = next;
        }
    m_hPrev.swap(m_h);   // old h becomes hPrev
    m_h.swap(m_hNext);   // next becomes current (hNext now holds the stale prev — overwritten next pass)
    ++m_version;
}

void RippleField::addImpulse(const glm::vec2& worldXZ, float radius, float strength) {
    if (radius <= 0.0f || strength == 0.0f) return;
    const glm::vec2 local = (worldXZ - m_origin) / m_pitch;
    const float r = radius / m_pitch;
    if (local.x < -r || local.y < -r ||
        local.x > m_cells + r || local.y > m_cells + r) return;   // outside the window → no-op

    const int x0 = std::max(0, static_cast<int>(std::floor(local.x - r)));
    const int x1 = std::min(m_cells - 1, static_cast<int>(std::ceil(local.x + r)));
    const int z0 = std::max(0, static_cast<int>(std::floor(local.y - r)));
    const int z1 = std::min(m_cells - 1, static_cast<int>(std::ceil(local.y + r)));
    if (x0 > x1 || z0 > z1) return;

    bool touched = false;
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) {
            const float dx = (x + 0.5f) - local.x, dz = (z + 0.5f) - local.y;
            const float d = std::sqrt(dx * dx + dz * dz);
            if (d >= r) continue;
            // Cosine cap: smooth to zero at the rim — a hard-edged impulse rings the stencil.
            const float w = 0.5f * (1.0f + std::cos(3.14159265f * d / r));
            // Write into BOTH h and hPrev so the impulse starts as a displaced-at-rest cap that
            // collapses outward (a dropped ring), not a velocity spike.
            m_h[idx(x, z)] += strength * w;
            m_hPrev[idx(x, z)] += 0.5f * strength * w;
            touched = true;
        }
    if (touched) {
        m_asleep = false;
        ++m_version;
    }
}

bool RippleField::followTo(const glm::vec2& focusWorld, float hysteresis) {
    const glm::vec2 centre = m_origin + glm::vec2(0.5f * windowSize());
    const glm::vec2 drift = focusWorld - centre;
    if (std::abs(drift.x) <= hysteresis && std::abs(drift.y) <= hysteresis) return false;

    // Shift by WHOLE CELLS so surviving waves stay world-stationary.
    const glm::ivec2 cellDelta(static_cast<int>(std::lround(drift.x / m_pitch)),
                               static_cast<int>(std::lround(drift.y / m_pitch)));
    if (cellDelta.x == 0 && cellDelta.y == 0) return false;

    auto shifted = [&](const std::vector<float>& src) {
        std::vector<float> out(src.size(), 0.0f);
        for (int z = 0; z < m_cells; ++z)
            for (int x = 0; x < m_cells; ++x) {
                const int sx = x + cellDelta.x, sz = z + cellDelta.y;
                if (sx < 0 || sz < 0 || sx >= m_cells || sz >= m_cells) continue;
                out[idx(x, z)] = src[idx(sx, sz)];
            }
        return out;
    };
    if (!m_asleep) {
        m_h = shifted(m_h);
        m_hPrev = shifted(m_hPrev);
    }
    m_origin += glm::vec2(cellDelta) * m_pitch;
    ++m_version;
    return true;
}

float RippleField::heightAt(const glm::vec2& worldXZ) const {
    if (m_asleep) return 0.0f;
    const glm::vec2 local = (worldXZ - m_origin) / m_pitch - glm::vec2(0.5f);
    const int x0 = static_cast<int>(std::floor(local.x));
    const int z0 = static_cast<int>(std::floor(local.y));
    if (x0 < 0 || z0 < 0 || x0 + 1 >= m_cells || z0 + 1 >= m_cells) return 0.0f;
    const float fx = local.x - x0, fz = local.y - z0;
    const float a = m_h[idx(x0, z0)] * (1 - fx) + m_h[idx(x0 + 1, z0)] * fx;
    const float b = m_h[idx(x0, z0 + 1)] * (1 - fx) + m_h[idx(x0 + 1, z0 + 1)] * fx;
    return a * (1 - fz) + b * fz;
}

float RippleField::totalAmplitude() const {
    float sum = 0.0f;
    for (float v : m_h) sum += std::abs(v);
    return sum;
}

} // namespace Core
} // namespace Phyxel
