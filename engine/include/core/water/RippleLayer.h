#pragma once
// docs/WaterCore.md 22 - ripples: the sub-cell wave layer of one active volume. Surface DISPLACEMENT r (m) on a
// world-aligned lattice of pitch d = 1/9 m (the microcube), zero mean, no mass: it moves no water, it changes how
// the surface looks at the scale the 1/3 m grid cannot hold (waves 22 cm - 2/3 m).
//
// Evolution: Tessendorf's iWave (Interactive Water Surfaces, 2004) -
//     r'' + 2 a r' + g (K * r) = 0, stepped centred: r(t+dt) = [2 r - (1 - a dt) r(t-dt) - g dt^2 (K * r)] / (1 + a dt)
// K is the vertical-derivative kernel (13 x 13): the lattice convolution whose spectrum is
// L(k) = k (1 + sigma k^2 / (rho g)), so every wavelength runs at its own speed (omega^2 = g L(k)) and a
// disturbance spreads into a TRAIN of rings. Masked cells (solid, dry) hold r = 0: rings reflect off walls.
// Sources: an IMPULSE per unit area I (kg m/s / m^2, downward positive) - Cauchy-Poisson: the surface starts
// moving with dr/dt = -(K * I) / rho (a droplet's momentum, 22.3.1); a KINEMATIC velocity (m/s) the surface
// must follow this frame (a body's sharp-edge remainder, 22.3.2).

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace Phyxel::Core::Water {

class RippleLayer {
public:
    static constexpr int kRadius = 6;                       ///< kernel half-width (13 x 13), Tessendorf's
    static constexpr float kPitch = 1.0f / 9.0f;            ///< m - the microcube (owner, 2026-10-10)
    static constexpr float kMaxStep = 1.0f / 30.0f;         ///< s - the stability bound's step (omega_max dt < 2: 0.12 s at this pitch); the layer steps at kStep
    /// s - the FIXED substep. The centred leapfrog stores the previous height, so a step whose length differs from the
    /// last one rescales the implied velocity; stepping the frame's dt (4-25 ms jitter) pumped energy in until the pond
    /// was a 1 m checkerboard (owner's screenshot 2026-10-10; WaterRippleTest.UnevenFramesStayBounded). Frame time is
    /// accumulated and spent in whole steps.
    static constexpr float kStep = 1.0f / 120.0f;
    static constexpr int kMaxSubsteps = 4;                  ///< per call: a longer stall drops time - a visual field owes none

    /// `origin` = the lattice cell (world / kPitch, integer) of cell (0, 0); nx x nz cells.
    RippleLayer(const glm::ivec2& origin, int nx, int nz, float gravity = 9.81f);

    int nx() const { return m_nx; }
    int nz() const { return m_nz; }
    const glm::ivec2& origin() const { return m_origin; }
    size_t idx(int x, int z) const { return static_cast<size_t>(x) + static_cast<size_t>(m_nx) * z; }
    bool inBounds(int x, int z) const { return x >= 0 && z >= 0 && x < m_nx && z < m_nz; }
    /// World xz -> lattice cell (may be out of bounds).
    glm::ivec2 cellAt(float wx, float wz) const;

    /// 1 = water (evolves), 0 = masked (solid / dry: r held at 0). Set before step(); default all water.
    std::vector<uint8_t>& mask() { return m_mask; }
    /// Cells on the layer's border that are water continue past the volume: absorbed over 2 cells (no reflection).
    void setOpenBorder(bool open) { m_openBorder = open; }

    /// A downward impulse per unit area I (kg m/s / m^2) on one cell, applied at the next step.
    void addImpulse(float wx, float wz, float impulsePerArea);
    /// The surface over this cell moves at `velocity` (m/s, up positive) during the next step.
    void setKinematic(int x, int z, float velocity);

    void step(float dt);
    bool asleep() const { return m_asleep; }
    void wake() { m_asleep = false; }
    const std::vector<float>& heights() const { return m_r; }
    float height(int x, int z) const { return inBounds(x, z) ? m_r[idx(x, z)] : 0.0f; }
    float maxAbs() const;
    double energyProxy() const;   ///< sum r^2 + (dr)^2 (for sleep and the tests)

    /// Damping a (1/s): the inextensible-film rate k sqrt(nu omega / 8) at the 20 cm band centre (22.2).
    static float filmDamping(float gravity = 9.81f);
    /// The kernel's own spectrum on the lattice at wavenumber k (1/m) along x - what a plane wave sees (R-T2).
    float kernelResponse(float k) const;
    /// The target spectrum L(k) = k (1 + sigma k^2 / (rho g)).
    static float targetResponse(float k, float gravity = 9.81f);
    const std::vector<float>& kernel() const { return m_kernel; }   ///< (2R+1)^2, row-major (dz, dx)

private:
    void buildKernel();
    void convolve(const std::vector<float>& in, std::vector<float>& out) const;
    int labelComponents();                 ///< 4-connected bodies of water in the mask (cached until the mask changes)
    std::vector<int> m_label;              ///< per cell: its body, -1 = masked
    std::vector<uint8_t> m_maskSeen;
    int m_nComp = 0;
    int m_nx, m_nz;
    glm::ivec2 m_origin;
    float m_g, m_a;
    std::vector<float> m_kernel;
    std::vector<float> m_r, m_prev, m_next, m_tmp, m_impulse, m_kin;
    std::vector<uint8_t> m_mask, m_kinSet;
    bool m_openBorder = true, m_asleep = true, m_anyImpulse = false, m_anyKin = false;
    int m_quietSteps = 0;
    float m_accum = 0.0f;   ///< frame time not yet spent in whole kStep steps
};

}  // namespace Phyxel::Core::Water
