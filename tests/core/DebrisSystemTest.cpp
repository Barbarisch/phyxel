#include <gtest/gtest.h>
#include "core/DebrisSystem.h"
#include "core/ChunkVoxelQuerySystem.h"
#include <glm/glm.hpp>
#include <cmath>

using namespace Phyxel;

// Minimal mock: no voxels in the world
class MockVoxelQuery : public ChunkVoxelQuerySystem {
public:
    MockVoxelQuery() = default;
    // ChunkVoxelQuerySystem with no callbacks = hasVoxelAt always returns false
};

// ============================================================================
// Construction / Basics
// ============================================================================

TEST(DebrisSystemTest, ConstructionNoVoxelQuery) {
    DebrisSystem ds(nullptr);
    EXPECT_EQ(ds.getActiveParticleCount(), 0u);
    EXPECT_FALSE(ds.getParticles().empty());  // Pre-allocated pool
}

TEST(DebrisSystemTest, ConstructionWithMock) {
    MockVoxelQuery mock;
    DebrisSystem ds(&mock);
    EXPECT_EQ(ds.getActiveParticleCount(), 0u);
}

TEST(DebrisSystemTest, InitialParticlesInactive) {
    DebrisSystem ds(nullptr);
    for (const auto& p : ds.getParticles()) {
        EXPECT_FALSE(p.active);
    }
}

// ============================================================================
// Spawning
// ============================================================================

TEST(DebrisSystemTest, SpawnSingleParticle) {
    DebrisSystem ds(nullptr);
    ds.spawnDebris({1, 2, 3}, {0, 0, 0}, {0.1f, 0.1f, 0.1f}, {1, 0, 0, 1}, 2.0f);
    EXPECT_EQ(ds.getActiveParticleCount(), 1u);
}

TEST(DebrisSystemTest, SpawnedParticleProperties) {
    DebrisSystem ds(nullptr);
    ds.spawnDebris({5, 10, 15}, {1, 0, 0}, {0.2f, 0.2f, 0.2f}, {0, 1, 0, 1}, 3.0f);

    const auto& particles = ds.getParticles();
    // Find the active one
    bool found = false;
    for (const auto& p : particles) {
        if (p.active) {
            EXPECT_FLOAT_EQ(p.position.x, 5.0f);
            EXPECT_FLOAT_EQ(p.position.y, 10.0f);
            EXPECT_FLOAT_EQ(p.position.z, 15.0f);
            EXPECT_FLOAT_EQ(p.lifetime, 3.0f);
            EXPECT_FLOAT_EQ(p.maxLifetime, 3.0f);
            EXPECT_FLOAT_EQ(p.color.r, 0.0f);
            EXPECT_FLOAT_EQ(p.color.g, 1.0f);
            EXPECT_FLOAT_EQ(p.scale.x, 0.2f);
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST(DebrisSystemTest, SpawnMultipleParticles) {
    DebrisSystem ds(nullptr);
    for (int i = 0; i < 50; ++i) {
        ds.spawnDebris({float(i), 5, 0}, {0, 0, 0}, {0.1f, 0.1f, 0.1f}, {1, 1, 1, 1}, 5.0f);
    }
    EXPECT_EQ(ds.getActiveParticleCount(), 50u);
}

TEST(DebrisSystemTest, SpawnVelocitySetsPrevsPosition) {
    DebrisSystem ds(nullptr);
    glm::vec3 pos(10, 20, 30);
    glm::vec3 vel(100, 0, 0);
    ds.spawnDebris(pos, vel, {0.1f, 0.1f, 0.1f}, {1, 1, 1, 1}, 5.0f);

    for (const auto& p : ds.getParticles()) {
        if (p.active) {
            // prevPosition = position - velocity * 0.016
            EXPECT_NEAR(p.prevPosition.x, pos.x - vel.x * 0.016f, 1e-4f);
            EXPECT_NEAR(p.prevPosition.y, pos.y - vel.y * 0.016f, 1e-4f);
            break;
        }
    }
}

// ============================================================================
// Update / Lifetime
// ============================================================================

TEST(DebrisSystemTest, UpdateDecreasesLifetime) {
    DebrisSystem ds(nullptr);
    ds.spawnDebris({10, 50, 10}, {0, 0, 0}, {0.1f, 0.1f, 0.1f}, {1, 1, 1, 1}, 2.0f);
    ds.update(0.03f);  // Within 0.05s clamp

    for (const auto& p : ds.getParticles()) {
        if (p.active) {
            EXPECT_NEAR(p.lifetime, 1.97f, 1e-4f);
            break;
        }
    }
}

TEST(DebrisSystemTest, ParticleDeactivatesWhenLifetimeExpires) {
    DebrisSystem ds(nullptr);
    ds.spawnDebris({10, 50, 10}, {0, 0, 0}, {0.1f, 0.1f, 0.1f}, {1, 1, 1, 1}, 0.02f);
    EXPECT_EQ(ds.getActiveParticleCount(), 1u);

    ds.update(0.03f);  // 0.03 > 0.02 lifetime, particle dies
    EXPECT_EQ(ds.getActiveParticleCount(), 0u);
}

TEST(DebrisSystemTest, ZeroLifetimeDeactivatesImmediately) {
    DebrisSystem ds(nullptr);
    ds.spawnDebris({10, 50, 10}, {0, 0, 0}, {0.1f, 0.1f, 0.1f}, {1, 1, 1, 1}, 0.0f);
    ds.update(0.016f);
    EXPECT_EQ(ds.getActiveParticleCount(), 0u);
}

TEST(DebrisSystemTest, DtClampedToMax) {
    DebrisSystem ds(nullptr);
    ds.spawnDebris({0, 100, 0}, {0, 0, 0}, {0.1f, 0.1f, 0.1f}, {1, 1, 1, 1}, 10.0f);

    // Even with huge dt, lifetime should only decrease by max 0.05
    ds.update(999.0f);
    for (const auto& p : ds.getParticles()) {
        if (p.active) {
            EXPECT_NEAR(p.lifetime, 10.0f - 0.05f, 1e-4f);
            break;
        }
    }
}

// ============================================================================
// Physics: Gravity
// ============================================================================

TEST(DebrisSystemTest, GravityPullsDown) {
    DebrisSystem ds(nullptr);
    // Spawn high up with zero velocity
    ds.spawnDebris({0, 100, 0}, {0, 0, 0}, {0.1f, 0.1f, 0.1f}, {1, 1, 1, 1}, 5.0f);

    float initialY = 100.0f;
    ds.update(0.016f);

    for (const auto& p : ds.getParticles()) {
        if (p.active) {
            EXPECT_LT(p.position.y, initialY);
            break;
        }
    }
}

TEST(DebrisSystemTest, HorizontalVelocityMaintained) {
    DebrisSystem ds(nullptr);
    // Spawn with horizontal velocity, high enough to avoid floor
    ds.spawnDebris({0, 100, 0}, {50, 0, 0}, {0.1f, 0.1f, 0.1f}, {1, 1, 1, 1}, 5.0f);
    ds.update(0.016f);

    for (const auto& p : ds.getParticles()) {
        if (p.active) {
            EXPECT_GT(p.position.x, 0.0f);  // Moved in +X
            break;
        }
    }
}

// ============================================================================
// Physics: Floor Collision
// ============================================================================

TEST(DebrisSystemTest, FloorCollisionPreventsNegativeY) {
    DebrisSystem ds(nullptr);
    // Spawn just above floor with downward velocity
    ds.spawnDebris({0, 0.1f, 0}, {0, -100, 0}, {0.1f, 0.1f, 0.1f}, {1, 1, 1, 1}, 5.0f);
    ds.update(0.016f);

    for (const auto& p : ds.getParticles()) {
        if (p.active) {
            EXPECT_GE(p.position.y, 0.0f);
            break;
        }
    }
}

// ============================================================================
// Update with zero dt
// ============================================================================

TEST(DebrisSystemTest, ZeroDtNoChange) {
    DebrisSystem ds(nullptr);
    ds.spawnDebris({5, 50, 5}, {0, 0, 0}, {0.1f, 0.1f, 0.1f}, {1, 1, 1, 1}, 2.0f);

    ds.update(0.0f);
    EXPECT_EQ(ds.getActiveParticleCount(), 1u);
    for (const auto& p : ds.getParticles()) {
        if (p.active) {
            EXPECT_FLOAT_EQ(p.lifetime, 2.0f);
            break;
        }
    }
}

// ============================================================================
// Ring Buffer Behavior
// ============================================================================

TEST(DebrisSystemTest, RingBufferOverwritesOldest) {
    DebrisSystem ds(nullptr);

    // Spawn MAX_PARTICLES + 1 to trigger wraparound
    for (size_t i = 0; i <= 10000; ++i) {
        ds.spawnDebris({float(i), 50, 0}, {0, 0, 0}, {0.1f, 0.1f, 0.1f}, {1, 1, 1, 1}, 99.0f);
    }
    // Active count should cap at MAX_PARTICLES
    EXPECT_EQ(ds.getActiveParticleCount(), 10000u);
}

// ============================================================================
// Multiple particles deactivate independently
// ============================================================================

TEST(DebrisSystemTest, IndependentLifetimes) {
    DebrisSystem ds(nullptr);
    ds.spawnDebris({0, 50, 0}, {0, 0, 0}, {0.1f, 0.1f, 0.1f}, {1, 0, 0, 1}, 0.03f); // Very short
    ds.spawnDebris({5, 50, 0}, {0, 0, 0}, {0.1f, 0.1f, 0.1f}, {0, 1, 0, 1}, 5.0f);  // Long
    EXPECT_EQ(ds.getActiveParticleCount(), 2u);

    ds.update(0.04f);  // Short one dies (0.03 - 0.04 < 0)
    EXPECT_EQ(ds.getActiveParticleCount(), 1u);
}

TEST(DebrisSystemTest, UpdateWithNoActiveParticles) {
    DebrisSystem ds(nullptr);
    // Should be safe to call with nothing active
    ds.update(0.016f);
    EXPECT_EQ(ds.getActiveParticleCount(), 0u);
}
