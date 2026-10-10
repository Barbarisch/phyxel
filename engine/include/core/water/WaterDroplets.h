#pragma once
// docs/WaterCore.md 21.2 / 21.11 - the droplet crown: water the grid cannot hold leaves it as droplets,
// flies, and lands back in - mass exact (grid + pool is conserved).
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

/// The CPU reference of both rules on `g` (one tick of `dt`): zeroes / reduces the cells, appends one birth
/// per column that lost water. Mass removed from the grid == sum of the births' volumes.
BirthReport birthDroplets(WaterGrid& g, float dt, float gravity, std::vector<DropletBirth>& out);

/// Put `volume` (m^3) into `g` at the world point (the cell holding it, lifted to the first non-solid cell at
/// or above it); a cell's overflow above 1 - s carries up its column. Returns what did not fit (m^3).
double depositIntoGrid(WaterGrid& g, const glm::vec3& world, double volume);

/// The pool (21.2): world-space droplets, owned by WaterCoreManager. Cap 20 000 (§12 decision 5).
struct Droplet {
    glm::vec3 pos{0.0f}, vel{0.0f};
    float volume = 0.0f;   ///< m^3 it carries
    float edge = 0.0f;     ///< drawn cube edge = volume^(1/3): drawn volume = carried mass
    int volumeId = 0;      ///< the active volume it left (and lands back in)
    float age = 0.0f;
};
class DropletPool {
public:
    static constexpr size_t kCap = 20000;
    /// Split a birth into droplets of (k h)^3 each (k = the size knob, clamped [1/27, 1]) at hashed points inside the
    /// cell around `b.pos`, velocity +-15 % zero-sum per birth. Returns the volume that did NOT fit the pool (the
    /// caller puts it straight back - never dropped).
    double spawn(const DropletBirth& b, int volumeId, float h, float sizeFraction, uint32_t tick);
    /// Advance every droplet by dt (gravity, quadratic air drag); `land(pos, prevPos, volumeId)` returns true when
    /// the droplet reached water or ground - the droplet is then removed and reported in `landed`.
    template <class LandFn> void step(float dt, float gravity, LandFn land, std::vector<Droplet>& landed);
    /// Remove every droplet of a volume (it is flushed into its grid by the caller).
    std::vector<Droplet> take(int volumeId);
    const std::vector<Droplet>& droplets() const { return m_d; }
    double volumeInFlight() const { double v = 0.0; for (const Droplet& d : m_d) v += d.volume; return v; }
    double volumeInFlight(int volumeId) const { double v = 0.0; for (const Droplet& d : m_d) if (d.volumeId == volumeId) v += d.volume; return v; }
    bool anyFor(int volumeId) const { for (const Droplet& d : m_d) if (d.volumeId == volumeId) return true; return false; }
private:
    std::vector<Droplet> m_d;
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
