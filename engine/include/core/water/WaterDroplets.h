#pragma once
// docs/WaterCore.md 21.2 / 21.11 - the droplet crown, made COSMETIC in 51.11 (the owner, 2026-10-11: "splash particles would
// not 'take' from the volume of the pond. it should jsut be a few splash particles for affect"): droplets show the water a
// splash throws, fly and land (driving ripples), and carry none - the pond's water never leaves the grid.
//
// BIRTH, two rules, per world cell (no chunk input):
//  (1) SCRAPS - a DETACHED run (a vertical run of cells holding water, f >= film, whose cell below is air:
//      f + s < film; outside the grid is solid) that holds sum f < 1 and touches, sideways, no SUPPORTED
//      water (water whose own run rests on water, a solid or a body), with nothing solid above it. The 19.7
//      pocket roofs (beside supported pond water) and crown rims (water below) never qualify; a sheet of
//      scraps touching each other is born together.
//  (2) SPRAY - at the top cell of a supported run, water leaving upward faster than v_c = sqrt(2 g h) (it
//      would rise more than one cell above the surface, which the grid can only carry as a slab) leaves as
//      droplets: f (v_up - v_c) dt / h of the cell per tick, at most its water above the film.
// The CPU reference (birthDroplets) and the GPU kernel (wc_column_ops mode 4) implement the same rules;
// both report one birth per column per call (volume-weighted position and velocity).

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace Phyxel::Core::Water {

class WaterGrid;

struct DetachedRun {
    glm::ivec3 bottom{0};   ///< grid-local cell of the run's lowest cell
    int cells = 0;          ///< run length (cells, upward from `bottom`)
    float sumF = 0.0f;      ///< the run's water in cell volumes (sum of f)
    bool isolated = false;  ///< nothing but air around it (the 21.2 draft's rule; measurement only)
};

/// Every detached run in a grid of `dims` (x fastest, then y, then z - WaterGrid::idx). `s` may be null (no bodies).
std::vector<DetachedRun> findDetachedRuns(const glm::ivec3& dims, const float* f, const float* s, float film);

struct DropletBirth {
    glm::vec3 pos{0.0f};     ///< world: volume-weighted centre of the water that left
    glm::vec3 vel{0.0f};     ///< m/s, volume-weighted
    float volume = 0.0f;     ///< m^3
    int column = 0;          ///< grid-local column index x + nx z (for the spread hash)
};
struct BirthReport { int scraps = 0, sprays = 0; double volume = 0.0; };

/// The CPU reference of both rules on `g` (one tick of `dt`): appends one birth per column that threw water (the volume the
/// droplets SHOW). 51.11 (the owner's decision, 2026-10-11): droplets are cosmetic - the grid's mass is unchanged. A spray cell keeps
/// its water; a scrap's water falls down its own column onto what holds it, the same tick.
BirthReport birthDroplets(WaterGrid& g, float dt, float gravity, std::vector<DropletBirth>& out);

/// The pool (21.2): world-space droplets, owned by WaterCoreManager. Cap 20 000 (§12 decision 5).
struct Droplet {
    glm::vec3 pos{0.0f}, vel{0.0f};
    float volume = 0.0f;   ///< m^3 it SHOWS (51.11: carries no water; sizes the cube and its landing's ripple kick)
    float edge = 0.0f;     ///< drawn cube edge = volume^(1/3)
    int volumeId = 0;      ///< the active volume it left (whose surface it lands on)
    float age = 0.0f;
};
class DropletPool {
public:
    static constexpr size_t kCap = 20000;
    /// Lower the cap (tests: D-T4 forces it to 10). Never above kCap - the cost bound (§12 decision 5).
    void setCap(size_t c) { m_cap = c < kCap ? c : kCap; }
    size_t cap() const { return m_cap; }
    /// Split a birth into droplets of (k h)^3 each (k = the size knob, clamped [1/27, 1]) at hashed points inside the
    /// cell around `b.pos`, velocity +-15 % zero-sum per birth. Returns the volume that did NOT fit the pool (shown by
    /// none - counted as refused; no water, 51.11).
    double spawn(const DropletBirth& b, int volumeId, float h, float sizeFraction, uint32_t tick);
    /// 26.31 look (the owner, 2026-10-18: "splashes way too round"): the share of the +-15 % velocity spread that goes SIDEWAYS
    /// to the birth's own velocity (1 = isotropic, the 21 default - each parcel bursts into a ball; < 1 = thrown along its
    /// direction). The pair stays zero-sum either way: the parcel's mean velocity is unchanged.
    static inline float s_lateralSpread = 1.0f;
    /// Advance every droplet by dt (gravity, quadratic air drag); `land(pos, prevPos, volumeId)` returns true when
    /// the droplet reached water or ground - the droplet is then removed and reported in `landed`.
    template <class LandFn> void step(float dt, float gravity, LandFn land, std::vector<Droplet>& landed);
    /// Remove every droplet of a volume (51.11: just dropped - they carry no water).
    void drop(int volumeId);
    /// A2i (47): drop every droplet (the world changed)
    void clear() { m_d.clear(); }
    const std::vector<Droplet>& droplets() const { return m_d; }
    bool anyFor(int volumeId) const { for (const Droplet& d : m_d) if (d.volumeId == volumeId) return true; return false; }
private:
    std::vector<Droplet> m_d;
    size_t m_cap = kCap;
};

/// Quadratic air drag on a water cube of edge e: a = 0.5 rho_air Cd e^2 |v| v / (rho_w e^3) (rho_air 1.2,
/// rho_w 1000, Cd 0.47 sphere-equivalent).
inline glm::vec3 dropletDrag(const glm::vec3& v, float edge) {
    const float k = 0.5f * 1.2f * 0.47f / (1000.0f * (edge > 1e-4f ? edge : 1e-4f));
    return -k * glm::length(v) * v;
}

template <class LandFn>
void DropletPool::step(float dt, float gravity, LandFn land, std::vector<Droplet>& landed) {
    size_t w = 0;
    for (size_t i = 0; i < m_d.size(); ++i) {
        Droplet d = m_d[i];
        const glm::vec3 prev = d.pos;
        d.vel += (glm::vec3(0.0f, -gravity, 0.0f) + dropletDrag(d.vel, d.edge)) * dt;
        d.pos += d.vel * dt;
        d.age += dt;
        if (land(d, prev)) { landed.push_back(d); continue; }
        m_d[w++] = d;
    }
    m_d.resize(w);
}

/// Droplet cubes for the renderer: xyz = centre, w = edge.
void dropletDrawList(const DropletPool& pool, std::vector<glm::vec4>& out);

}  // namespace Phyxel::Core::Water
