#pragma once
// docs/VoxelFoundationPlan.md §6.7 -- bodies move and collide (F5.2a: the CPU reference body solver).
//
// The engine's AVBD solver (GpuParticlePhysics / solver_*.comp: primal 6x6 block solve per body per colour,
// dual per constraint, warm start by feature, static-friction anchors, start-at-rest, sleep) extended to what §6.7
// asks: a body is its VOXEL GRID (F5.1) with the full inertia tensor, and contacts are voxel-on-voxel and
// symmetric -- the surface lattice corners of X against the cells of Y, and Y's corners against X (the world is a
// static Y with the identity transform). Any body size; per-pair contact cap with every dropped candidate counted.
// Double precision, deterministic (Gauss-Seidel per colour, ascending body order). Prediction:
// docs/evidence/voxfound_f52_prediction.txt.
//
// Frames: world metres, world voxel (i, j, k) spans [i, i + 1) * voxelM. A body's local frame is centred on its
// centre of mass, axes = world axes at extraction (quat = identity then).

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/voxfound/Bodies.h"
#include "core/voxfound/FractureLab.h"
#include "core/voxfound/VoxelGrid.h"

namespace Phyxel::Core::VoxFound {

struct BodySimParams {
    double dt = 1.0 / 60.0;
    int iterations = 8;
    glm::dvec3 gravity{0.0, -9.81, 0.0};
    double alpha = 0.99, beta = 1e5, gamma = 0.999;
    double penaltyMin = 1.0, penaltyMax = 1e10;
    double margin = 0.02;                     // m: speculative band
    double coldScale = 1.0;                   // a new world contact's penalty = coldScale * m / dt^2 (the engine's)
    double coldScaleBody = 0.2;               // a new body-body IMPACT contact's: coldScaleBody * reduced mass / dt^2
    double impactSpeed = 0.5;                 // m/s closing speed at the contact that counts as an impact
    int contactCap = 8;                       // per contact pair (body-world or body-body)
    double restSpeed = 0.1;                   // m/s: start-at-rest below this
    double sleepSpeed = 0.05;                 // m/s (angular as surface speed)
    int sleepTicks = 30;
    double wakeSpeed = 0.5;                   // m/s: an awake body this fast touching a sleeper wakes it
    bool groundBelow = true;                  // world y < 0 is solid
    double hardContact = 0.01;                // m: overlap beyond this is projected out (the engine's HC_THRESH)
};

struct SimBody {
    Body src;                                 // the voxels + mass properties (F5.1)
    glm::dvec3 pos{0.0};                      // centre of mass, m
    glm::dquat quat{1.0, 0.0, 0.0, 0.0};
    glm::dvec3 vel{0.0}, angVel{0.0};
    double invMass = 0.0;
    glm::dmat3 inertiaBody{0.0};              // kg m^2 about the centre, body axes
    double friction = 0.5;                    // the body's majority material
    std::vector<glm::dvec3> corners;          // surface lattice corners per exposed face, body-local metres
    std::vector<uint8_t> cornerDir;           // the outward normal (kDir index) of the face each corner belongs to
    bool asleep = false;
    int slowTicks = 0;
    // solver scratch
    glm::dvec3 initial{0.0}, inertial{0.0}, cumAng{0.0};
    glm::dquat initialQuat{1.0, 0.0, 0.0, 0.0}, inertialQuat{1.0, 0.0, 0.0, 0.0};
};

struct BodySimStats {
    size_t contacts = 0;                      // constraints this tick
    size_t candidates = 0;                    // contact candidates before the cap
    size_t capped = 0;                        // candidates dropped by the cap (counted, never silent)
    size_t deepPoints = 0;                    // points with no empty face neighbour (no contact made)
    size_t pairsTested = 0;                   // body-body AABB overlaps
    size_t awake = 0;
    size_t hardFires = 0;                     // hard-contact projections this tick (non-physical clamp; counted)
    double maxPenetration = 0.0;              // m, at contact detection
    double ms = 0.0;
};

class BodySimRef {
public:
    BodySimRef(const VoxelGrid& world, const std::vector<FractureMaterial>& mats, BodySimParams p = {});
    /// Adds a body at its extraction pose (pos = com, identity rotation); returns its index.
    size_t add(const Body& b, glm::dvec3 vel = glm::dvec3(0.0), glm::dvec3 angVel = glm::dvec3(0.0));
    void step();
    bool implemented() const;

    std::vector<SimBody>& bodies() { return m_bodies; }
    const std::vector<SimBody>& bodies() const { return m_bodies; }
    const BodySimStats& stats() const { return m_stats; }
    struct Constraint;
    const std::vector<Constraint>& constraints() const { return m_cons; }
    const BodySimParams& params() const { return m_p; }
    /// The deepest penetration of any body voxel corner into the world or another body right now (m), and the
    /// lowest corner y of any body (tunnelling check).
    double currentMaxPenetration() const;
    double lowestCornerY() const;
    /// The deepest contact right now, described (pair, normal, arm, point) -- for failure messages.
    std::string worstPenetrationReport() const;

    struct Constraint {
        size_t a = 0, b = 0;                  // b == kStatic: the world
        glm::dvec3 n{0.0}, t1{0.0}, t2{0.0};  // n pushes a out of b
        glm::dvec3 rA{0.0}, rB{0.0};          // body-local arms (rB: world point when b is static)
        double cInitN = 0.0, cInitT1 = 0.0, cInitT2 = 0.0;
        double lambdaN = 0.0, lambdaT1 = 0.0, lambdaT2 = 0.0;
        double penN = 1.0, penT1 = 1.0, penT2 = 1.0;
        double mu = 0.5;
        glm::dvec3 anchor{0.0};               // static friction anchor, world
        uint64_t key = 0;
        bool stick = false;
    };
    static constexpr size_t kStatic = ~size_t(0);

private:
    struct Warm {
        double lambdaN, lambdaT1, lambdaT2, penN, penT1, penT2;
        glm::dvec3 anchor;
        bool stick;
    };
    const VoxelGrid& m_world;
    std::vector<FractureMaterial> m_mats;
    BodySimParams m_p;
    std::vector<SimBody> m_bodies;
    std::vector<Constraint> m_cons;
    std::unordered_map<uint64_t, Warm> m_warm;
    BodySimStats m_stats;
};

}  // namespace Phyxel::Core::VoxFound
