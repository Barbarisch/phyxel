# Phyxel Unit Tests

This directory contains all unit tests for the Phyxel engine using Google Test framework.

## Running Tests

### Windows (Visual Studio)
```cmd
# Build tests
cmake --build build --config Debug --target phyxel_tests

# Run all tests
.\build\tests\Debug\phyxel_tests.exe

# Or use CTest
cd build
ctest -C Debug --output-on-failure
```

### Linux
```bash
# Build tests
cmake --build build --target phyxel_tests

# Run all tests
./build/tests/phyxel_tests

# Or use CTest
cd build
ctest --output-on-failure
```

## Test Organization

```
tests/
├── utils/               # Utility class tests
│   └── CoordinateUtilsTest.cpp
├── core/                # Core system tests (future)
├── scene/               # Scene system tests (future)
└── mocks/               # Mock objects for testing (future)
```

## Writing New Tests

### Example Test File

```cpp
#include <gtest/gtest.h>
#include "path/to/YourClass.h"

TEST(YourClassTest, TestName) {
    // Arrange
    YourClass obj;
    
    // Act
    int result = obj.someFunction();
    
    // Assert
    EXPECT_EQ(result, expected_value);
}
```

### Test Naming Convention

- **Test Suite**: `ClassNameTest` (e.g., `CoordinateUtilsTest`)
- **Test Name**: `MethodName_Scenario` (e.g., `WorldToChunkCoord_NegativeValues`)

### Common Assertions

```cpp
// Equality
EXPECT_EQ(actual, expected);
ASSERT_EQ(actual, expected);  // Stops test on failure

// Boolean
EXPECT_TRUE(condition);
EXPECT_FALSE(condition);

// Floating point (with tolerance)
EXPECT_NEAR(actual, expected, tolerance);

// Exceptions
EXPECT_THROW(statement, exception_type);
EXPECT_NO_THROW(statement);

// Strings
EXPECT_STREQ(str1, str2);
```

## Current Test Coverage

### ✅ Tested Components

- **CoordinateUtils** (19 tests)
  - World → Chunk coordinate conversion
  - World → Local coordinate conversion
  - Chunk → World origin conversion
  - Local + Chunk → World conversion
  - Validation functions
  - Round-trip conversions

### 🔲 Not Yet Tested

- ChunkVoxelQuerySystem
- VoxelRaycaster (DDA algorithm)
- ChunkVoxelManager (hash map operations)
- Subsystem callback patterns
- Physics integration

## Adding Tests for Your Code

1. **Create test file** in appropriate directory (`utils/`, `core/`, `scene/`)
2. **Include gtest header** and your class header
3. **Write TEST() macros** following naming conventions
4. **Build and run** to verify tests pass
5. **Check coverage** - aim for critical paths and edge cases

## Best Practices

### DO:
- ✅ Test one thing per TEST() function
- ✅ Use descriptive test names
- ✅ Test edge cases (boundaries, negatives, empty, null)
- ✅ Keep tests fast (< 1ms each)
- ✅ Make tests independent (no shared state)

### DON'T:
- ❌ Test private methods (test public interface)
- ❌ Test third-party libraries (trust they work)
- ❌ Write brittle tests (tight coupling to implementation)
- ❌ Use sleep/timing in tests (use mocks instead)

## Continuous Integration

Tests run automatically on:
- Every commit (local pre-commit hook - future)
- Every pull request (GitHub Actions - future)
- Before deployment (CI/CD pipeline - future)

## Troubleshooting

### "Could NOT find GTest"
Google Test is downloaded automatically via FetchContent. If this fails:
1. Check internet connection
2. Verify CMake version >= 3.14
3. Try clearing build directory: `rm -rf build`

### Tests link but don't run
Make sure you built the `phyxel_tests` target:
```bash
cmake --build build --target phyxel_tests
```

### Linker errors (LNK4098)
This is a common warning with MSVC and different runtime libraries.
It's safe to ignore for tests. To fix permanently, ensure consistent
runtime library settings across all dependencies.

## Resources

- [Google Test Primer](https://google.github.io/googletest/primer.html)
- [Google Test Documentation](https://google.github.io/googletest/)
- [Google Mock (for mocking)](https://google.github.io/googletest/gmock_for_dummies.html)
