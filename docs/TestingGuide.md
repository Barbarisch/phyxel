# Testing Setup for Phyxel

## Overview

This guide explains how to set up and write tests for the Phyxel voxel engine using Google Test (gtest).

## Quick Start

### 1. Install Google Test

**Windows (vcpkg)**:
```powershell
# Install vcpkg if not already installed
git clone https://github.com/Microsoft/vcpkg.git external/vcpkg
.\external\vcpkg\bootstrap-vcpkg.bat

# Install gtest
.\external\vcpkg\vcpkg install gtest:x64-windows
```

**Linux**:
```bash
sudo apt-get install libgtest-dev
cd /usr/src/gtest
sudo cmake CMakeLists.txt
sudo make
sudo cp lib/*.a /usr/lib
```

### 2. Update CMakeLists.txt

Add to root `CMakeLists.txt`:

```cmake
# Enable testing
option(BUILD_TESTS "Build tests" ON)

if(BUILD_TESTS)
    enable_testing()
    
    # Find Google Test
    find_package(GTest REQUIRED)
    include(GoogleTest)
    
    # Add test subdirectory
    add_subdirectory(tests)
endif()
```

### 3. Create Test Directory Structure

```
tests/
├── CMakeLists.txt
├── core/
│   ├── ChunkManager_test.cpp
│   ├── ChunkVoxelQuerySystem_test.cpp
│   └── ChunkVoxelModificationSystem_test.cpp
├── scene/
│   ├── VoxelRaycaster_test.cpp
│   └── VoxelManipulationSystem_test.cpp
├── utils/
│   └── CoordinateUtils_test.cpp
└── mocks/
    ├── MockChunk.h
    ├── MockChunkManager.h
    └── MockPhysicsWorld.h
```

### 4. Create tests/CMakeLists.txt

```cmake
# Collect all test files
file(GLOB_RECURSE TEST_SOURCES "*.cpp")

# Create test executable
add_executable(phyxel_tests ${TEST_SOURCES})

# Link against main library and gtest
target_link_libraries(phyxel_tests
    PRIVATE
        GTest::GTest
        GTest::Main
        # Add your main library here when you create it
        # phyxel_lib
)

# Include directories
target_include_directories(phyxel_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
)

# Discover tests
gtest_discover_tests(phyxel_tests)
```

## Running Tests

### Fast Testing Workflow (Recommended)

For rapid iteration, use the `test_fast.ps1` script. This script builds only the test executable and allows you to run specific tests using a filter.

**Usage:**
```powershell
# Run all tests
.\test_fast.ps1

# Run tests matching a pattern (e.g., all VoxelRaycaster tests)
.\test_fast.ps1 VoxelRaycasterTest.*

# Run a specific test case
.\test_fast.ps1 VoxelRaycasterTest.RayHit_SimpleCube
```

### Standard Workflow (Full Build)

To build and run all tests (including integration tests):

```powershell
.\build_and_test.ps1
```

## Writing Tests

### Mocking Dependencies

To test components in isolation (like `VoxelRaycaster`) without spinning up the entire engine, use **Interface Extraction** and **Mocking**.

**Pattern:**
1.  **Extract Interface**: Create an interface (e.g., `IChunkManager`) for the dependency.
2.  **Implement Interface**: Make the real class (e.g., `ChunkManager`) implement this interface.
3.  **Create Mock**: Create a mock class (e.g., `MockChunkManager`) that implements the interface but uses simplified logic (e.g., a `std::unordered_map` instead of full chunk storage).
4.  **Inject Dependency**: Update the component under test to accept the interface (or a factory/callback returning the interface) rather than the concrete class.

**Example (VoxelRaycaster):**

```cpp
// In VoxelRaycaster.h
// Accepts a callback to get the IChunkManager
VoxelLocation pickVoxel(
    const glm::vec3& rayOrigin, 
    const glm::vec3& rayDirection,
    std::function<IChunkManager*()> chunkManagerProvider
);

// In Test
MockChunkManager mock;
mock.addCube(glm::ivec3(5, 0, 0)); // Setup mock state

raycaster.pickVoxel(origin, dir, [&]() { return &mock; });
```

### Test Structure


### Unit Test Template

```cpp
#include <gtest/gtest.h>
#include "core/ChunkVoxelQuerySystem.h"
#include "mocks/MockChunkManager.h"

namespace VulkanCube {
namespace {

// Test fixture for shared setup
class ChunkVoxelQuerySystemTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize test objects
        querySystem = std::make_unique<ChunkVoxelQuerySystem>();
        
        // Configure with mock callbacks
        querySystem->setCallbacks(
            [this]() -> auto& { return mockChunkMap; },
            [this]() -> auto& { return mockChunks; }
        );
    }
    
    void TearDown() override {
        // Cleanup
        querySystem.reset();
    }
    
    // Test objects
    std::unique_ptr<ChunkVoxelQuerySystem> querySystem;
    std::unordered_map<glm::ivec3, size_t> mockChunkMap;
    std::vector<std::unique_ptr<Chunk>> mockChunks;
};

// Basic functionality test
TEST_F(ChunkVoxelQuerySystemTest, GetChunkAtValidPosition) {
    // Arrange
    auto chunk = std::make_unique<Chunk>(glm::ivec3(0, 0, 0));
    mockChunks.push_back(std::move(chunk));
    mockChunkMap[glm::ivec3(0, 0, 0)] = 0;
    
    // Act
    Chunk* result = querySystem->getChunkAtFast(glm::ivec3(5, 5, 5));
    
    // Assert
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->getWorldOrigin(), glm::ivec3(0, 0, 0));
}

TEST_F(ChunkVoxelQuerySystemTest, GetChunkAtInvalidPosition) {
    // Act
    Chunk* result = querySystem->getChunkAtFast(glm::ivec3(100, 100, 100));
    
    // Assert
    EXPECT_EQ(result, nullptr);
}

// Edge case test
TEST_F(ChunkVoxelQuerySystemTest, GetChunkAtBoundary) {
    auto chunk = std::make_unique<Chunk>(glm::ivec3(0, 0, 0));
    mockChunks.push_back(std::move(chunk));
    mockChunkMap[glm::ivec3(0, 0, 0)] = 0;
    
    // Test at chunk boundary (position 31,31,31 is last valid position in chunk)
    Chunk* result = querySystem->getChunkAtFast(glm::ivec3(31, 31, 31));
    
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->getWorldOrigin(), glm::ivec3(0, 0, 0));
}

} // namespace
} // namespace VulkanCube
```

### Integration Test Example

```cpp
#include <gtest/gtest.h>
#include "core/ChunkManager.h"
#include "physics/PhysicsWorld.h"

namespace VulkanCube {
namespace {

class ChunkManagerIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create real objects for integration testing
        manager = std::make_unique<ChunkManager>();
        // Note: Vulkan device setup omitted for clarity
        // In real tests, you'd use a mock Vulkan device or test without GPU
    }
    
    std::unique_ptr<ChunkManager> manager;
};

TEST_F(ChunkManagerIntegrationTest, CreateAndQueryChunk) {
    // Arrange
    glm::ivec3 origin(0, 0, 0);
    
    // Act
    manager->createChunk(origin);
    Chunk* chunk = manager->getChunkAtCoord(glm::ivec3(0, 0, 0));
    
    // Assert
    ASSERT_NE(chunk, nullptr);
    EXPECT_EQ(chunk->getWorldOrigin(), origin);
}

TEST_F(ChunkManagerIntegrationTest, VoxelModificationPersists) {
    // Arrange
    manager->createChunk(glm::ivec3(0, 0, 0));
    glm::ivec3 worldPos(5, 5, 5);
    glm::vec3 testColor(1.0f, 0.0f, 0.0f);
    
    // Act
    bool added = manager->addCubeFast(worldPos, testColor);
    Cube* cube = manager->getCubeAtFast(worldPos);
    
    // Assert
    EXPECT_TRUE(added);
    ASSERT_NE(cube, nullptr);
    EXPECT_EQ(cube->getColor(), testColor);
}

} // namespace
} // namespace VulkanCube
```

## Mock Objects

### Mock Chunk

```cpp
// tests/mocks/MockChunk.h
#pragma once
#include "core/Chunk.h"

namespace VulkanCube {

class MockChunk : public Chunk {
public:
    MockChunk(const glm::ivec3& origin = glm::ivec3(0))
        : Chunk(origin) {}
    
    // Override methods that need special behavior in tests
    void initialize(VkDevice dev, VkPhysicalDevice physDev) override {
        // Skip Vulkan initialization in tests
        device = dev;
        physicalDevice = physDev;
    }
};

} // namespace VulkanCube
```

### Mock ChunkManager

```cpp
// tests/mocks/MockChunkManager.h
#pragma once
#include "core/ChunkManager.h"

namespace VulkanCube {

class MockChunkManager {
public:
    MOCK_METHOD(Chunk*, getChunkAtFast, (const glm::ivec3&), ());
    MOCK_METHOD(Cube*, getCubeAtFast, (const glm::ivec3&), ());
    MOCK_METHOD(bool, addCubeFast, (const glm::ivec3&, const glm::vec3&), ());
    MOCK_METHOD(bool, removeCubeFast, (const glm::ivec3&), ());
};

} // namespace VulkanCube
```

## Test Categories

### 1. Unit Tests (Subsystems)

Test individual subsystems in isolation using mocks:

**ChunkVoxelQuerySystem Tests**:
- ✅ O(1) chunk lookup
- ✅ Boundary conditions (chunk edges)
- ✅ Invalid position handling
- ✅ Coordinate conversions

**VoxelRaycaster Tests**:
- ✅ Ray-AABB intersection
- ✅ Screen to world ray conversion
- ✅ Voxel picking accuracy
- ✅ Subcube and microcube detection

**VoxelManipulationSystem Tests**:
- ✅ Voxel removal logic
- ✅ Subdivision operations
- ✅ Breaking with physics
- ✅ Material selection

### 2. Integration Tests

Test subsystem interactions:

**ChunkManager + Subsystems**:
- ✅ Chunk creation and query
- ✅ Voxel modification persistence
- ✅ Dirty chunk tracking
- ✅ Face update coordination

**VoxelInteractionSystem + Subsystems**:
- ✅ Mouse hover detection
- ✅ Voxel manipulation from UI
- ✅ Force application
- ✅ Raycasting accuracy

### 3. Performance Tests

Measure critical path performance:

```cpp
TEST(PerformanceTest, ChunkVoxelQueryBenchmark) {
    ChunkManager manager;
    manager.createChunk(glm::ivec3(0, 0, 0));
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Perform 10,000 queries
    for (int i = 0; i < 10000; ++i) {
        glm::ivec3 pos(rand() % 32, rand() % 32, rand() % 32);
        Cube* cube = manager.getCubeAtFast(pos);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // Assert O(1) performance: 10k queries should take < 10ms
    EXPECT_LT(duration.count(), 10000);
}
```

## Running Tests

### Build and Run All Tests

```bash
# Configure with tests enabled
cmake -B build -DBUILD_TESTS=ON

# Build tests
cmake --build build --target phyxel_tests

# Run all tests
cd build
ctest --output-on-failure

# Or run test executable directly
./tests/phyxel_tests
```

### Run Specific Tests

```bash
# Run tests matching a pattern
./phyxel_tests --gtest_filter=ChunkVoxelQuerySystem*

# Run with verbose output
./phyxel_tests --gtest_filter=ChunkVoxelQuerySystem* --gtest_verbose

# List all tests without running
./phyxel_tests --gtest_list_tests
```

## Test-Driven Development Workflow

1. **Write failing test** for new feature
2. **Implement minimum code** to pass test
3. **Refactor** while keeping tests green
4. **Add edge case tests**
5. **Repeat**

Example:

```cpp
// Step 1: Write failing test
TEST(ChunkVoxelManager, SubdivideAt) {
    ChunkVoxelManager manager;
    // ... setup ...
    
    bool result = manager.subdivideAt(glm::ivec3(5, 5, 5));
    EXPECT_TRUE(result);
    // Test will fail - subdivideAt doesn't exist yet
}

// Step 2: Implement subdivideAt
// Step 3: Refactor implementation
// Step 4: Add edge cases

TEST(ChunkVoxelManager, SubdivideAtInvalidPosition) {
    ChunkVoxelManager manager;
    bool result = manager.subdivideAt(glm::ivec3(-1, -1, -1));
    EXPECT_FALSE(result);  // Should handle invalid gracefully
}
```

## Continuous Integration

### GitHub Actions Example

```yaml
# .github/workflows/tests.yml
name: Tests

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    
    steps:
    - uses: actions/checkout@v2
      with:
        submodules: recursive
    
    - name: Install dependencies
      run: |
        sudo apt-get update
        sudo apt-get install -y libgtest-dev cmake
    
    - name: Configure
      run: cmake -B build -DBUILD_TESTS=ON
    
    - name: Build
      run: cmake --build build
    
    - name: Test
      run: cd build && ctest --output-on-failure
```

## Best Practices

### ✅ DO
- Test subsystems in isolation with mocks
- Test one behavior per test function
- Use descriptive test names: `TEST(Class, BehaviorUnderTest)`
- Clean up in TearDown()
- Test edge cases and error conditions
- Keep tests fast (< 1ms per test)

### ❌ DON'T
- Test implementation details (test behavior, not internals)
- Make tests depend on each other
- Use sleeps or timeouts
- Test multiple behaviors in one test
- Ignore failing tests
- Skip error case testing

## Next Steps

1. **Start small**: Add tests for utility functions (CoordinateUtils)
2. **Mock Vulkan**: Create stub Vulkan objects for rendering tests
3. **Mock Bullet**: Create stub physics objects for physics tests
4. **Expand coverage**: Gradually add tests for subsystems
5. **CI integration**: Set up automated testing on commits

## Resources

- [Google Test Primer](https://google.github.io/googletest/primer.html)
- [Google Mock Documentation](https://google.github.io/googletest/gmock_for_dummies.html)
- [Testing Best Practices](https://google.github.io/googletest/faq.html)

## Summary

Testing the subsystem architecture is straightforward:
1. Mock callbacks for unit tests
2. Use real objects for integration tests
3. Focus on behavior, not implementation
4. Keep tests fast and focused

The callback pattern makes testing easier because subsystems can be tested in complete isolation from their parent systems.
