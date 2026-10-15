#include <gtest/gtest.h>

#include "core/water/SeaSwell.h"
#include "core/water/WaterCore.h"

#include <cmath>
#include <functional>

// WaterCore Phase G (docs/WaterCore.md 18.2): the ocean boundary. A prescribed column is the sea:
// its surface is written every tick, the solver moves the water, and the exchange is counted.

namespace Phyxel::Core::Water {
namespace {

constexpr float kDt = 1.0f / 60.0f;

// Airy (linear) progressive wave in water of depth d: elevation and the orbital velocity at the
// surface, horizontal in phase with the elevation, vertical in quadrature. Deep-water dispersion
// (the sheet's c = sqrt(g/k)) so the test drives exactly what the band will.
struct Airy {
    float a, L, d;
    float k() const { return 6.28318530718f / L; }
    float omega() const { return k() * std::sqrt(9.81f / k()); }
    float eta(float x, float t) const { return a * std::sin(k() * x - omega() * t); }
    float uSurf(float x, float t) const { const float kd = k() * d; return a * omega() * std::cosh(kd) / std::sinh(kd) * std::sin(k() * x - omega() * t); }
    float wSurf(float x, float t) const { return a * omega() * std::cos(k() * x - omega() * t); }
    float period() const { return 6.28318530718f / omega(); }
    float cPhase() const { return omega() / k(); }
};
void prescribe(WaterSolver& s, int x, float still, const Airy& w, float t) {
    BoundarySpec& b = s.setBoundary({x, 0}, still + w.eta(static_cast<float>(x), t));
    b.uSurface = glm::vec2(w.uSurf(static_cast<float>(x), t), 0.0f);
    b.wSurface = w.wSurf(static_cast<float>(x), t);
    b.k = w.k();
}

struct Tank {
    WaterGrid grid;
    std::function<Occ(const glm::ivec3&)> extra;
    Tank(int nx, int ny, int nz, float h = 1.0f) : grid(GridSpec{glm::ivec3(0), glm::ivec3(nx, ny, nz), h}) {}
    SolidQuery query() const {
        const GridSpec s = grid.spec(); auto ex = extra;
        return [s, ex](const glm::ivec3& cw) -> Occ {
            const glm::ivec3 c = cw - s.origin;
            if (c.x < 0 || c.y < 0 || c.z < 0 || c.x >= s.dims.x || c.y >= s.dims.y || c.z >= s.dims.z) return Occ::Solid;
            return ex ? ex(c) : Occ::Air; };
    }
};

TEST(SeaSwellTest, PortMatchesTheFourComponentSum) {
    SeaSwellParams p; p.amplitude = 0.45f; p.wavelength = 14.0f; p.windRad = 0.6f;
    SeaSwellComponent c[4]; seaSwellComponents(p, c);
    EXPECT_NEAR(c[0].amplitude, 0.45f, 1e-6f); EXPECT_NEAR(c[1].amplitude, 0.45f * 0.52f, 1e-6f);
    EXPECT_NEAR(c[3].wavelength, 70.0f, 1e-4f);
    const float x = 12.3f, z = -4.1f, t = 2.7f;
    float ref = 0.0f;
    for (int i = 0; i < 4; ++i) { const float k = 6.28318530718f / c[i].wavelength; ref += c[i].amplitude * std::sin(k * (c[i].dir.x * x + c[i].dir.y * z - std::sqrt(9.81f / k) * t)); }
    EXPECT_NEAR(seaSwellHeight(p, x, z, t), ref, 1e-5f);
    // bounded by the summed amplitude, zero when calm
    EXPECT_LE(std::abs(seaSwellHeight(p, x, z, t)), 0.45f * (1.0f + 0.52f + 0.28f + 0.70f) + 1e-6f);
    p.amplitude = 0.0f;
    EXPECT_EQ(seaSwellHeight(p, x, z, t), 0.0f);
}

TEST(WaterCoreBoundaryTest, BoundaryExchangeIsExact) {
    // a 1 m channel 20 long, 3 deep of water; the end column is pushed to 3.4 then pulled to 2.6:
    // the mass change of the grid equals the exchange reported, to 1e-9
    Tank t(20, 6, 1);
    t.grid.fillBox({0, 0, 0}, {19, 2, 0}, 1.0f);
    WaterSolver s(t.grid, t.query());
    const double m0 = t.grid.totalMass();
    s.setBoundary({0, 0}, 3.4f);
    const double d1 = s.applyBoundaries();
    EXPECT_NEAR(d1, 0.4, 1e-6);   // float32 fills
    EXPECT_NEAR(t.grid.totalMass() - m0, d1, 1e-6);
    EXPECT_NEAR(t.grid.f(0, 3, 0), 0.4f, 1e-6f);
    s.setBoundary({0, 0}, 2.6f);
    const double d2 = s.applyBoundaries();
    EXPECT_NEAR(d2, -0.8, 1e-6);
    EXPECT_NEAR(s.boundaries()[0].exchanged, -0.4, 1e-6) << "cumulative per column";
    EXPECT_NEAR(t.grid.f(0, 2, 0), 0.6f, 1e-6f);
    EXPECT_EQ(t.grid.f(0, 3, 0), 0.0f);
}

// The swell-carrying tests moved to ShoreSolverTest: the 3-D core measured unable to carry a swell
// at any affordable resolution (docs/WaterCore.md 18.4). The boundary stays as an inlet primitive.

}  // namespace
}  // namespace Phyxel::Core::Water
