#pragma once

// DebrisSettleAnalyzer — measures whether GPU debris actually LOSES energy and comes to rest.
//
// docs/DebrisSettlingPlan.md §3. Pure CPU, no Vulkan: GpuParticlePhysics feeds it one
// sample per body per physics tick (read back from the particle SSBO), plus the solver's
// own counters. Kept free of GPU types so it can be unit-tested with synthetic ticks.
//
// The two headline measurements are physical invariants, not heuristics:
//   * INJECTED ENERGY — total mechanical energy E = Σ m(½|v|² + g·y) + ½Iω² of a closed
//     pile can only go DOWN (contacts/friction/damping dissipate; gravity is inside E).
//     Any tick where ΣE rises is energy the solver created. Reported cumulatively and as
//     an equivalent lift height (J / Σm·g) so it reads in metres.
//   * REBOUNDS — a body that was not rising (vy ≤ 0.05 m/s) and is now rising faster than
//     0.3 m/s was launched by a contact. Real rubble rebounds a few times per piece; a
//     "bubbling" pile rebounds continuously long after impact.
// Plus: sideways kicks, speed distribution, awake/asleep, and whether bodies fell asleep
// CLEANLY (already < strictSpeed when frozen) or were FORCE-frozen by the lax sleep tier
// while still churning (the tier that hides the problem).

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

/// One body's state at the END of a physics tick.
struct SettleBodySample {
    glm::vec3 position{0.0f};
    glm::vec3 prevPosition{0.0f};   // position at tick start (velocity = (pos-prev)/dt)
    glm::vec3 angularVel{0.0f};
    glm::vec4 rotation{0.0f, 0.0f, 0.0f, 1.0f};  // quaternion (x,y,z,w)
    uint32_t  flags = 0;            // GpuParticle flags (bit0 ACTIVE, bit1 SLEEPING, age bits 16-23)
    float     mass   = 1.0f;        // solver mass (materials[].mass — size-independent today)
    float     radius = 0.5f;        // max half-extent (solver's sphere-inertia radius)
    uint32_t  color  = 0;           // graph colour assigned this tick (0xFFFFFFFF = uncoloured)
    uint32_t  constraintCount = 0;  // constraints touching this body this tick
};

/// Raw solver counters for one tick (from the solver-state header).
struct SettleSolverCounters {
    uint32_t constraintsEmitted   = 0;  // SS_CONSTRAINT_COUNT (keeps counting past the cap)
    uint32_t constraintCap        = 0;  // MAX_CONSTRAINTS
    uint32_t hardContactFires     = 0;  // bodies the post-solve push-out moved
    float    hardContactMaxDepth  = 0;  // metres, deepest push-out this tick
    uint32_t wakeRequests         = 0;  // wake bits set this tick (narrowphase + character)
    uint32_t warmstartHits        = 0;
    uint32_t maxColors            = 12; // colours the primal loop dispatches
    bool     uncoloredSolved      = false; // true: UNCOLORED bodies get a Jacobi fallback sweep
};

struct SettleTickStats {
    uint32_t tick = 0;
    float    t    = 0.0f;               // seconds since analyzer reset
    uint32_t active = 0, awake = 0, asleep = 0;
    double   energy = 0.0;              // ΣE (J) over active bodies
    double   kinetic = 0.0;             // Σ KE lin+rot (J)
    double   injected = 0.0;            // max(0, ΔΣE) over bodies present both ticks (J)
    double   cumInjected = 0.0;
    float    maxSpeed = 0.0f, p95Speed = 0.0f;
    uint32_t churning = 0;              // awake bodies faster than churnSpeed
    uint32_t rebounds = 0;              // bodies launched upward this tick
    uint32_t kicks = 0;                 // slow bodies suddenly accelerated (any direction)
    uint32_t launches = 0;              // bodies thrown upward faster than 1 m/s
    uint32_t sleptClean = 0, sleptForced = 0, woke = 0;  // transitions this tick
    uint32_t colorSkipped = 0;          // awake bodies WITH constraints the primal never solves
    uint32_t uncoloredJacobi = 0;       // awake bodies solved by the Jacobi fallback sweep
    uint32_t belowFloor = 0;            // bodies > 1 m below Config::floorY (tunnelled)
    float    minY = 0.0f;               // lowest body centre this tick
    SettleSolverCounters solver;
};

class DebrisSettleAnalyzer {
public:
    struct Config {
        float dt             = 1.0f / 60.0f;
        float gravity        = 9.81f;   // magnitude
        float reboundPrevMax = 0.05f;   // vy before (m/s) — "not already rising"
        float reboundUp      = 0.3f;    // vy after (m/s) — launched
        float launchUp       = 1.0f;
        float kickPrevMax    = 0.15f;   // |v| before
        float kickDelta      = 0.3f;    // |v| gain in one tick
        float churnSpeed     = 0.15f;   // the lax sleep tier's bound — above it never sleeps
        float strictSpeed    = 0.05f;   // the strict sleep tier's bound
        uint32_t strictTicks = 30;      // (informational) the GPU strict tier's tick count
        // Pass/fail criteria for summary() (docs/DebrisSettlingPlan.md §3 A2 invariants).
        float settleWindow   = 1.0f;    // s — earliest start of the judged window
        float impactSpeed    = 2.0f;    // m/s — anything faster is still in the impact phase;
                                        // the judged window starts 0.5 s after the last such tick
        float maxReboundsPerBodyAfter = 0.01f;   // mean, after settleWindow
        float maxInjectedLiftAfter    = 0.001f;  // m (J / Σm·g), after settleWindow
        float allAsleepBy    = 4.0f;    // s
        // Optional lowest legitimate rest surface (e.g. the pit floor). A body whose centre
        // drops more than 1 m below it has tunnelled through solid ground. NaN = unchecked.
        float floorY         = std::numeric_limits<float>::quiet_NaN();
    };

    DebrisSettleAnalyzer() = default;
    explicit DebrisSettleAnalyzer(const Config& c) : m_cfg(c) {}

    void reset();
    void setConfig(const Config& c) { m_cfg = c; }
    const Config& config() const { return m_cfg; }

    /// Feed one physics tick. `bodies` is indexed by particle SLOT; inactive slots carry
    /// flags without the ACTIVE bit. A slot whose spawn age drops is treated as a NEW body.
    void addTick(const std::vector<SettleBodySample>& bodies, const SettleSolverCounters& c);

    const std::vector<SettleTickStats>& ticks() const { return m_ticks; }
    uint32_t tickCount() const { return static_cast<uint32_t>(m_ticks.size()); }

    /// Whole-run summary: per-window breakdown, settle times, sleep quality, solver
    /// failures, and the pass/fail verdict against Config's criteria.
    nlohmann::json summary() const;
    /// Per-tick series as JSON rows (optionally only the last `lastN`).
    nlohmann::json series(uint32_t lastN = 0) const;
    bool writeCsv(const std::string& path) const;
    /// Per-body view of the most recent tick: the `maxN` fastest AWAKE bodies with their
    /// slot, position, velocity, graph colour, constraint count and rebound history.
    nlohmann::json awakeBodies(uint32_t maxN = 16) const;

private:
    struct BodyTrack {
        bool      present = false;
        uint32_t  age = 0;
        glm::vec3 vel{0.0f};
        double    energy = 0.0;
        bool      asleep = false;
        uint32_t  strictRun = 0;
        uint32_t  rebounds = 0, reboundsAfter = 0, kicks = 0;
        float     firstSeen = 0.0f;
        // Last kHist ticks (ring): to measure how far / how fast a body was still moving in
        // the half-second before it was frozen — the visible meaning of a "forced" sleep.
        static constexpr uint32_t kHist = 30;
        glm::vec3 histPos[kHist];
        float     histSpeed[kHist] = {};
        uint32_t  histN = 0, histHead = 0;
    };
    // Per sleep event: displacement over the preceding kHist ticks (m) and peak speed (m/s).
    std::vector<float> m_creepClean, m_creepForced, m_vmaxClean, m_vmaxForced;

    Config m_cfg;
    std::vector<BodyTrack> m_bodies;
    std::vector<SettleTickStats> m_ticks;
    std::vector<SettleBodySample> m_lastSamples;   // last tick, for awakeBodies()
    uint32_t m_tick = 0;
    double   m_cumInjected = 0.0;
    double   m_totalMassG = 0.0;     // Σ m·g over every body ever seen (for lift height)
    uint32_t m_bodiesSeen = 0;
    uint32_t m_sleptCleanTotal = 0, m_sleptForcedTotal = 0, m_wokeTotal = 0;
};

} // namespace Core
} // namespace Phyxel
