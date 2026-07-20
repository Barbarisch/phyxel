#include "BenchmarkFixture.h"
#include "physics/PhysicsWorld.h"
#include "physics/VoxelDynamicsWorld.h"
#include "physics/VoxelOccupancyGrid.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <memory>
#include <vector>

namespace Phyxel {
namespace Testing {

class PhysicsBenchmarks : public BenchmarkFixture {
protected:
    void SetUp() override {
        physicsWorld = std::make_unique<Physics::PhysicsWorld>();
        physicsWorld->initialize();
    }

    void TearDown() override {
        physicsWorld.reset();
    }

    std::unique_ptr<Physics::PhysicsWorld> physicsWorld;

    Physics::VoxelRigidBody* makeBody(const glm::vec3& pos, float mass = 1.0f) {
        return physicsWorld->getVoxelWorld()->createVoxelBody(pos, glm::vec3(0.5f), mass);
    }

    // U1 (docs/DestructionSystemV2.md §15.3): a floor + low containing walls so dropped
    // bodies PILE and generate real terrain + body-body contacts — the contact-dominated
    // cost that actually bounds the coherent-fragment budget (a fell's collision proxy is
    // ~1 box per wood cell, so N single-box bodies ≈ a fell of N cells / N active boxes).
    std::vector<std::unique_ptr<Physics::VoxelOccupancyGrid>> m_arenaGrids;
    void buildArena() {
        auto g = std::make_unique<Physics::VoxelOccupancyGrid>();
        g->setChunkOrigin(glm::ivec3(0, 0, 0));
        for (int x = 0; x < 32; ++x)
            for (int z = 0; z < 32; ++z) {
                g->setCube(glm::ivec3(x, 0, z), true);           // floor at y=0 (top y=1)
                for (int y = 1; y < 16; ++y)                     // perimeter walls, contain the pile
                    if (x == 0 || x == 31 || z == 0 || z == 31)
                        g->setCube(glm::ivec3(x, y, z), true);
            }
        physicsWorld->getVoxelWorld()->registerGrid(g.get());
        m_arenaGrids.push_back(std::move(g));
    }

    // Drop N single-box bodies clustered over the arena centre, step until the pile
    // settles, and return {peakStepMs (the impact frame), settledStepMs}.
    struct PileResult { double peakMs; double settledMs; };
    PileResult measurePile(int n, int steps = 220) {
        auto* vw = physicsWorld->getVoxelWorld();
        // Cluster in x,z ∈ [6,26) so bodies stay on the 32×32 floor; stack upward.
        const int side = 20;
        for (int i = 0; i < n; ++i) {
            int gx = 6 + (i % side);
            int gz = 6 + ((i / side) % side);
            int gy = i / (side * side);
            makeBody(glm::vec3(gx + 0.5f, 20.0f + gy * 1.2f, gz + 0.5f), 2.0f);
        }
        double peak = 0.0, last = 0.0;
        for (int s = 0; s < steps; ++s) {
            auto t0 = Clock::now();
            physicsWorld->stepSimulation(1.0f / 60.0f);
            double ms = Duration(Clock::now() - t0).count();
            peak = std::max(peak, ms);
            last = ms;
        }
        return {peak, last};
    }
};

TEST_F(PhysicsBenchmarks, CreateDynamicBodies) {
    auto result = runBenchmark("Create 100 dynamic voxel bodies", 10, 100, [this]() {
        for (int i = 0; i < 100; ++i) {
            makeBody(glm::vec3(i * 2.0f, 10.0f, 0.0f));
        }
    });

    EXPECT_LT(result.averageMs, 100.0) << "Creating dynamic bodies is too slow";
    EXPECT_GT(result.opsPerSecond(), 500.0) << "Should create at least 500 bodies/sec";
}

TEST_F(PhysicsBenchmarks, SimulationStepEmpty) {
    auto result = runBenchmark("Physics step (empty world)", 1000, 1, [this]() {
        physicsWorld->stepSimulation(1.0f / 60.0f);
    });

    EXPECT_LT(result.averageMs, 1.0) << "Empty physics step is too slow";
}

TEST_F(PhysicsBenchmarks, SimulationStep10Bodies) {
    for (int i = 0; i < 10; ++i) {
        makeBody(glm::vec3(i * 2.0f, 10.0f + i, 0.0f));
    }

    auto result = runBenchmark("Physics step (10 bodies)", 1000, 1, [this]() {
        physicsWorld->stepSimulation(1.0f / 60.0f);
    });

    EXPECT_LT(result.averageMs, 5.0) << "Physics step with 10 bodies is too slow";
}

TEST_F(PhysicsBenchmarks, SimulationStep100Bodies) {
    for (int i = 0; i < 100; ++i) {
        makeBody(glm::vec3((i % 10) * 2.0f, 10.0f + (i / 10) * 2.0f, (i / 50) * 2.0f));
    }

    auto result = runBenchmark("Physics step (100 bodies)", 100, 1, [this]() {
        physicsWorld->stepSimulation(1.0f / 60.0f);
    });

    EXPECT_LT(result.averageMs, 16.67) << "Physics step should maintain 60 FPS with 100 bodies";
    std::cout << "  Target FPS:  60 (16.67 ms)\n";
    std::cout << "  Actual FPS:  " << (1000.0 / result.averageMs) << "\n";
}

// U1: the CONTACT-dominated CPU ceiling — the number that should ground
// COHERENT_MAX_VOXELS and any active-body cap. Sweeps a piling drop and prints the
// peak (impact-frame) step time per active-box count. Run in RELEASE for real numbers;
// Debug is ~10-20x slower and must NOT be used to set the constants.
TEST_F(PhysicsBenchmarks, CoherentBudgetCeiling_PilingContacts) {
    buildArena();
    std::cout << "\n[U1] Contact-dominated pile — peak (impact) step time vs active boxes\n";
    std::cout << "     (single-box bodies; ~1 box per fell wood-cell; 60fps budget = 16.67ms)\n";
    std::cout << "     boxes |  peak ms  | settled ms\n";

    struct Row { int n; double peak; double settled; };
    std::vector<Row> rows;
    for (int n : {100, 200, 400, 800, 1200}) {
        // Fresh world per point so piles don't accumulate across the sweep.
        physicsWorld->getVoxelWorld()->removeAllBodies();
        m_arenaGrids.clear();
        physicsWorld = std::make_unique<Physics::PhysicsWorld>();
        physicsWorld->initialize();
        buildArena();
        auto r = measurePile(n);
        rows.push_back({n, r.peakMs, r.settledMs});
        std::cout << "     " << std::setw(5) << n << " | "
                  << std::setw(8) << std::fixed << std::setprecision(3) << r.peakMs << "  | "
                  << std::setw(8) << r.settledMs << "\n";
    }

    // GROUNDED CEILING (Release, this machine, 2026-07-20): peak impact-frame step time
    // is ~7.7ms @100 boxes, ~14ms @200 (the 60fps knee), ~32ms @400, ~62ms @800. So the
    // worst-case simultaneous-impact budget for 60fps is ~200 active collision boxes; a
    // fell's proxy is ~1 box/wood-cell, so that's ~one 200-cell fell OR several smaller
    // ones landing together. Assertion is a REGRESSION guard at the comfortable operating
    // point (100 boxes), with a ~1.8x margin so CI variance / machine load doesn't flake
    // it; a real solver regression (or the U1a broadphase fix silently reverting) trips it.
    double peak100 = 0.0, peak400 = 0.0;
    for (const auto& r : rows) { if (r.n == 100) peak100 = r.peak; if (r.n == 400) peak400 = r.peak; }
    ASSERT_GT(peak100, 0.0) << "100-box row missing";
    EXPECT_LT(peak100, 14.0)
        << "100 active colliding boxes should peak well under a 60fps frame in Release "
           "(grounded ~7.7ms). A big jump means the physics broadphase/solver regressed.";
    // Monotonicity sanity: more boxes cost more (guards a broken measurement).
    EXPECT_GT(peak400, peak100) << "cost did not grow with box count — measurement is broken";
}

TEST_F(PhysicsBenchmarks, BodyCountTracked) {
    std::cout << "\n[MEMORY] VoxelDynamicsWorld capacity test\n";

    size_t bodyCount = 1000;
    for (size_t i = 0; i < bodyCount; ++i) {
        glm::vec3 pos((i % 10) * 3.0f, 50.0f + (i / 100) * 3.0f, (i / 10 % 10) * 3.0f);
        makeBody(pos);
    }

    size_t actual = physicsWorld->getVoxelWorld()->getBodyCount();
    std::cout << "  Created:     " << bodyCount << " bodies\n";
    std::cout << "  In world:    " << actual << " bodies\n";

    EXPECT_EQ(actual, bodyCount) << "Not all bodies were added to physics world";
}

} // namespace Testing
} // namespace Phyxel
