# Integration Testing Guide

## Overview

Phyxel now has comprehensive integration testing alongside its unit tests. Integration tests verify that components work correctly together with real Vulkan and Physics subsystems.

**Test Count Summary:**
- **Unit Tests**: 251 fast tests (<120ms total) - pure functions, no dependencies
- **Integration Tests**: 47 tests (~5-10 seconds total) - real Vulkan/Physics initialization

## Philosophy

### Unit Tests vs Integration Tests

**Unit Tests** (`tests/core/`, `tests/utils/`, etc.):
- Test pure functions in isolation
- No external dependencies (no Vulkan, Physics, ChunkManager)
- Fast execution (<1ms per test)
- Run on every build
- Focus: Mathematical correctness, edge cases, algorithms

**Integration Tests** (`tests/integration/`):
- Test component interactions
- Initialize real Vulkan devices and Physics worlds
- Slower execution (varies by GPU/system)
- Verify end-to-end workflows
- Focus: System integration, component orchestration

## Directory Structure

```
tests/
├── core/               # Unit tests for core systems
├── utils/              # Unit tests for utilities
├── scene/              # Unit tests for scene components
├── mocks/              # Mock objects for testing
├── integration/        # Integration tests (NEW)
│   ├── CMakeLists.txt
│   ├── IntegrationTestFixture.h
│   ├── IntegrationTestFixture.cpp
│   ├── ChunkManagerIntegrationTest.cpp
│   ├── PhysicsIntegrationTest.cpp
│   └── VulkanIntegrationTest.cpp
└── CMakeLists.txt
```

## Test Fixtures

Integration tests use Google Test fixtures to set up complex environments:

### 1. VulkanTestFixture
Provides minimal Vulkan environment (instance, device, queue).

```cpp
class MyVulkanTest : public VulkanTestFixture {
  // Automatic setup/teardown of Vulkan
  // Access: device, physicalDevice, instance, queue
};
```

### 2. PhysicsTestFixture
Provides initialized Bullet Physics world.

```cpp
class MyPhysicsTest : public PhysicsTestFixture {
  // Automatic setup/teardown of PhysicsWorld
  // Access: physicsWorld
};
```

### 3. VulkanPhysicsTestFixture
Combines both Vulkan and Physics.

```cpp
class MyComplexTest : public VulkanPhysicsTestFixture {
  // Both subsystems initialized
  // Access: device, physicalDevice, instance, queue, physicsWorld
};
```

### 4. ChunkManagerTestFixture
Fully initialized ChunkManager with Vulkan and Physics.

```cpp
class MyChunkTest : public ChunkManagerTestFixture {
  // Complete environment for chunk operations
  // Access: chunkManager, device, physicsWorld
};
```

## Writing Integration Tests

### Example: ChunkManager Test

```cpp
#include "IntegrationTestFixture.h"
#include "core/ChunkManager.h"

class MyIntegrationTest : public ChunkManagerTestFixture {};

TEST_F(MyIntegrationTest, CreateAndModifyChunk) {
    // ChunkManager is already initialized
    ASSERT_NE(chunkManager, nullptr);
    
    // Create chunk
    chunkManager->createChunk({0, 0, 0});
    EXPECT_EQ(chunkManager->chunks.size(), 1);
    
    // Modify voxels
    chunkManager->initializeAllChunkVoxelMaps();
    chunkManager->removeCube({5, 5, 5});
    
    // Verify
    EXPECT_EQ(chunkManager->getCubeAt({5, 5, 5}), nullptr);
}
```

### Example: Physics Test

```cpp
class MyPhysicsTest : public PhysicsTestFixture {};

TEST_F(MyPhysicsTest, RigidBodyFalls) {
    btRigidBody* body = physicsWorld->createBoxRigidBody(
        {0, 10, 0}, {1, 1, 1}, 1.0f
    );
    
    float startY = /* get Y position */;
    
    for (int i = 0; i < 60; i++) {
        physicsWorld->stepSimulation(1.0f / 60.0f);
    }
    
    float endY = /* get Y position */;
    EXPECT_LT(endY, startY); // Fell due to gravity
}
```

## Running Tests

### Run All Tests (Unit + Integration)
```bash
./build_and_test.bat
# or
./build_and_test.ps1
```

### Run Only Unit Tests (Fast)
```bash
./build_and_test.bat -UnitOnly
# or
./build_and_test.ps1 -UnitOnly
```

### Run Only Integration Tests
```bash
./build_and_test.bat -IntegrationOnly
# or
./build_and_test.ps1 -IntegrationOnly
```

### Manual Execution
```bash
# Unit tests
build/tests/Debug/phyxel_tests.exe

# Integration tests
build/tests/integration/Debug/phyxel_integration_tests.exe
```

## Test Coverage

### ChunkManager Integration Tests (23 tests)
- ✅ Chunk creation (single and multiple)
- ✅ Chunk spatial hash map population
- ✅ Chunk initialization with cubes
- ✅ Voxel queries (chunk lookup, cube lookup)
- ✅ Voxel modifications (place/remove cubes)
- ✅ Cube subdivision into subcubes
- ✅ Dirty chunk tracking
- ✅ Cross-chunk face culling
- ✅ Voxel map initialization
- ✅ Multi-chunk queries
- ✅ Physics integration
- ✅ Vulkan buffer creation/updates

### Physics Integration Tests (17 tests)
- ✅ PhysicsWorld initialization
- ✅ Gravity simulation
- ✅ Rigid body creation (dynamic and static)
- ✅ Falling bodies
- ✅ Static body stability
- ✅ Collision detection
- ✅ Rigid body removal
- ✅ Material properties
- ✅ Friction and restitution
- ✅ Bouncing simulation
- ✅ Force application
- ✅ Impulse application
- ✅ Multi-body tower stability

### Vulkan Integration Tests (7 tests)
- ✅ Vulkan device creation
- ✅ Queue availability
- ✅ Chunk initialization
- ✅ Vulkan buffer creation
- ✅ Buffer updates
- ✅ Empty chunk buffers
- ✅ Instance data generation
- ✅ Multiple chunk buffers

## When to Use Integration Tests

### Good Candidates for Integration Tests:
- Workflows spanning multiple components (chunk creation → physics → rendering)
- Systems requiring GPU/device initialization
- Cross-chunk operations (face culling between chunks)
- Physics simulations (gravity, collisions, forces)
- Voxel manipulation through ChunkManager
- Buffer creation and updates

### Keep as Unit Tests:
- Pure mathematical functions
- Coordinate conversions
- Data structure operations (without side effects)
- Utility functions without dependencies
- Anything that runs in <1ms

## Performance Considerations

Integration tests are slower than unit tests:
- **Vulkan initialization**: ~100-500ms per fixture
- **Physics setup**: ~10-50ms per fixture
- **ChunkManager setup**: ~100-300ms (includes Vulkan + Physics)
- **Test execution**: Varies (physics simulation can take seconds)

**Total integration test time**: ~5-10 seconds (47 tests)
**Total unit test time**: <120ms (251 tests)

## Continuous Integration

Integration tests are automatically run in the build pipeline:
1. Build unit tests → Run unit tests
2. Build integration tests → Run integration tests
3. If either fails, build fails

For fast iteration during development, use `-UnitOnly` flag.

## Debugging Integration Tests

### Vulkan Issues
If tests skip due to "Vulkan not available":
- Ensure Vulkan SDK is installed
- Check `VK_SDK_PATH` environment variable
- Verify GPU drivers are up to date

### Physics Issues
Physics tests rarely fail unless:
- Simulation timestep is too large
- Collision detection threshold is off
- Test expectations are too strict (use tolerances)

### Test Timeouts
Integration tests have 300-second timeout (set in CMakeLists.txt).
If tests hang, check for:
- Infinite loops in simulation
- Deadlocks in Vulkan command submission
- Resource leaks preventing cleanup

## Adding New Integration Tests

1. **Choose appropriate fixture** (Vulkan, Physics, or Combined)
2. **Create test file** in `tests/integration/`
3. **Add to CMakeLists.txt** in `INTEGRATION_TEST_SOURCES`
4. **Write test cases** using Google Test macros
5. **Verify test runs** with `./build_and_test.ps1 -IntegrationOnly`
6. **Commit** with descriptive message

### Template for New Test File

```cpp
#include "IntegrationTestFixture.h"
// Include your headers

namespace VulkanCube {
namespace Testing {

class MyNewIntegrationTest : public ChunkManagerTestFixture {};

TEST_F(MyNewIntegrationTest, DescriptiveTestName) {
    // Setup
    // Action
    // Verify
    EXPECT_TRUE(condition);
}

} // namespace Testing
} // namespace VulkanCube
```

## Best Practices

1. **One concept per test** - Don't test everything in one test
2. **Clear test names** - `TEST_F(ChunkTest, RemovingCubeMarksChunkDirty)`
3. **Use fixtures** - Don't manually initialize Vulkan/Physics
4. **Clean assertions** - Use `EXPECT_*` macros with clear messages
5. **Test realistic workflows** - Not edge cases (unit tests handle those)
6. **Performance aware** - Don't simulate 10,000 frames if 60 suffices
7. **Skip gracefully** - Use `GTEST_SKIP()` if environment unavailable

## Troubleshooting

### "Vulkan not available" - Tests skip
- **Cause**: No Vulkan SDK or GPU drivers
- **Fix**: Install Vulkan SDK from https://vulkan.lunarg.com/

### Integration tests build but don't run
- **Cause**: Executable path mismatch
- **Fix**: Check `build/tests/integration/Debug/phyxel_integration_tests.exe` exists

### Tests crash during Vulkan initialization
- **Cause**: Invalid device/driver state
- **Fix**: Update GPU drivers, restart system

### Physics tests fail intermittently
- **Cause**: Non-deterministic simulation or timing issues
- **Fix**: Increase simulation steps, add tolerance to expectations

## Future Enhancements

Potential integration test additions:
- World persistence (save/load chunks from SQLite)
- Texture atlas integration
- Frustum culling with real camera
- Input system integration
- Render pipeline full-cycle tests (with swapchain)
- Multi-threaded chunk generation

## Summary

Integration tests complement unit tests by verifying component interactions:
- **Unit tests**: Fast, isolated, comprehensive coverage of algorithms
- **Integration tests**: Realistic, end-to-end validation of subsystems

Together, they provide confidence that:
1. Individual components work correctly (unit tests)
2. Components integrate properly (integration tests)
3. The system functions as a whole

**Test Strategy**: Write unit tests first (fast feedback), add integration tests for critical workflows.
