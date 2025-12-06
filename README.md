# VulkanCube

A high-performance 3D cube rendering engine built with Vulkan API, featuring real-time rendering of 32,000+ cubes with optimized face culling and interactive mouse controls.

## Features

### 🚀 **High Performance**
- **40+ FPS** rendering of 32,768 cubes (32x32x32 grid)
- **Optimized face culling** - only renders visible cube faces
- **Instance-based rendering** for efficient GPU utilization
- **Minimal CPU overhead** with pre-calculated face masks

### 🎮 **Interactive Controls**
- **WASD movement** - Navigate through the 3D scene
- **Mouse look** - Right-click and drag to rotate camera
- **Vertical movement** - Space (up) and Shift (down)
- **Mouse hover highlighting** - Cubes light up when cursor hovers over them

### 🔧 **Technical Excellence**
- **Modern Vulkan API** - Low-level graphics programming
- **Modular architecture** - Clean separation of concerns
- **Ray-AABB intersection** - Precise mouse picking
- **Bullet Physics integration** - Ready for dynamic objects
- **Comprehensive profiling** - Real-time performance metrics

## Architecture

```
src/
├── core/
│   ├── Application.cpp        # Main application loop and coordination
│   ├── ChunkManager.cpp       # Multi-chunk world management
│   └── Timer.cpp             # Performance timing utilities
├── graphics/
│   ├── RenderCoordinator.cpp  # Frame rendering coordination
│   ├── VulkanDevice.cpp       # Vulkan device and resource management
│   └── RenderPipeline.cpp     # Graphics and compute pipelines
├── scene/
│   └── VoxelInteractionSystem.cpp  # Mouse picking and voxel manipulation
├── physics/
│   └── PhysicsWorld.cpp       # Bullet physics integration
└── utils/
    ├── FileUtils.cpp          # File I/O utilities
    └── Math.cpp               # Mathematical operations
```

## Building

### Quick Start (Windows)

**Default (Fast):** Just build the code without running tests
```powershell
.\build_and_test.ps1
```

**With Tests:** Build and run fast unit tests
```powershell
.\build_and_test.ps1 -RunTests
```

**Skip Build:** Just run tests on existing binaries
```powershell
.\build_and_test.ps1 -SkipBuild -RunTests
```

**Specific Test Suites:**
```powershell
.\build_and_test.ps1 -UnitOnly          # All unit tests (including benchmarks)
.\build_and_test.ps1 -IntegrationOnly   # 36 integration tests
.\build_and_test.ps1 -BenchmarkOnly     # 11 benchmark tests
.\build_and_test.ps1 -StressOnly        # 24 stress tests
.\build_and_test.ps1 -E2EOnly           # 25 end-to-end tests
```

**Combined Options:**
```powershell
.\build_and_test.ps1 -SkipBuild -E2EOnly   # Quick E2E test run
.\build_and_test.ps1 -RunTests -E2EOnly    # Build + unit tests + E2E tests
```

### Prerequisites

#### All Platforms
- **C++17** compiler
- **Vulkan SDK** (1.3+) - Download from [LunarG](https://vulkan.lunarg.com/)
- **CMake** (3.10+)
- **Git** (for submodules)

**Important**: After installing Vulkan SDK, restart your command prompt/IDE to ensure environment variables are loaded.

#### Linux (Ubuntu/Debian)
- **GCC 9+** or **Clang 10+**
- **GLFW3** development headers
- **GLM** (OpenGL Mathematics)

```bash
sudo apt update
sudo apt install build-essential cmake git
sudo apt install libvulkan-dev vulkan-utils
sudo apt install libglfw3-dev libglm-dev
```

#### Windows
- **Visual Studio 2019+** or **MinGW-w64**
- **Vulkan SDK** from [LunarG](https://vulkan.lunarg.com/) - **Required!**
- **GLFW** precompiled binaries (download from [GLFW website](https://www.glfw.org/download.html))
- **GLM** headers (automatically included as submodule)
- **Bullet3** physics engine (automatically included as submodule)

**Note**: GLFW uses prebuilt binaries for faster build times and stability. Download the Windows pre-compiled binaries and extract to `external/glfw/`.

### Build Steps

#### Windows (Quick - PowerShell Script)
```powershell
# Clone the repository
git clone <repository-url>
cd phyxel

# Initialize submodules
git submodule update --init --recursive

# Download GLFW:
# 1. Download from https://www.glfw.org/download.html
# 2. Extract to external/glfw/

# Build (default: build only, no tests - fast!)
.\build_and_test.ps1

# Build and run tests
.\build_and_test.ps1 -RunTests

# Run the application
.\VulkanCube.exe
```

#### Linux
```bash
# Clone the repository
git clone <repository-url>
cd phyxel

# Initialize submodules (this downloads Bullet3 and GLM automatically)
git submodule update --init --recursive

# Create build directory
mkdir build && cd build

# Configure and build (Bullet3 and GLM will be built automatically)
cmake ..
make -j$(nproc)

# Run the application
./VulkanCube
```

#### Windows (Visual Studio - Manual)
```cmd
# Clone the repository
git clone <repository-url>
cd phyxel

# Initialize submodules (this downloads Bullet3 and GLM automatically)
git submodule update --init --recursive

# Download and setup GLFW manually:
# 1. Download GLFW pre-compiled binaries from https://www.glfw.org/download.html
# 2. Extract to external/glfw/ (should contain include/ and lib-vc2022/ folders)
# 3. Verify external/glfw/lib-vc2022/glfw3.lib exists

# Ensure Vulkan SDK is installed and restart terminal if just installed

# Create build directory and configure
mkdir build && cd build
cmake -B build -G "Visual Studio 17 2022" -A x64

# Build (Bullet3 will be built automatically with proper runtime compatibility)
cmake --build . --config Debug

# Run the application (will be copied to project root)
cd ..
VulkanCube.exe
```

#### Windows (MinGW)
```bash
# Clone the repository
git clone <repository-url>
cd phyxel

# Initialize submodules (this downloads Bullet3 and GLM automatically)
git submodule update --init --recursive

# Create build directory
mkdir build && cd build

# Configure and build (Bullet3 and GLM will be built automatically)
cmake .. -G "MinGW Makefiles"
cmake --build .

# Run the application
VulkanCube.exe
```

### Test Suite (383 Tests Total)

The project includes comprehensive testing:
- **287 Unit Tests** - Fast component tests (benchmarks excluded by default)
- **36 Integration Tests** - System interaction tests
- **11 Benchmark Tests** - Performance measurements
- **24 Stress Tests** - Load and stability tests
- **25 E2E Tests** - Full application lifecycle tests

**Default behavior:** `.\build_and_test.ps1` builds without running tests for fast iteration.

Use `-RunTests` flag to run unit tests (excluding slow benchmarks).

Use specific flags (`-UnitOnly`, `-IntegrationOnly`, `-BenchmarkOnly`, `-StressOnly`, `-E2EOnly`) to run particular test suites.

## Troubleshooting

### "Could NOT find Vulkan" Error
```bash
# Install Vulkan SDK from https://vulkan.lunarg.com/
# Restart your command prompt/IDE after installation
# Verify installation:
echo %VULKAN_SDK%  # Should show path like C:\VulkanSDK\1.x.x.x
```

### "glfw3.lib not found" Error
```bash
# Download GLFW from https://www.glfw.org/download.html
# Extract to external/glfw/
# Verify: external/glfw/lib-vc2022/glfw3.lib exists
```

### Linux Build Issues
```bash
# If missing dependencies:
sudo apt update
sudo apt install libvulkan-dev vulkan-utils libglfw3-dev

# If Vulkan validation layers missing:
sudo apt install vulkan-validationlayers-dev
```

### Build Script Options (Windows)

The `build_and_test.ps1` script provides flexible build and test options:

**Parameters:**
- `-SkipBuild` - Skip compilation, use existing binaries
- `-RunTests` - Run unit tests after build (excludes slow benchmarks)
- `-UnitOnly` - Run all unit tests including benchmarks
- `-IntegrationOnly` - Run only integration tests
- `-BenchmarkOnly` - Run only benchmark tests
- `-StressOnly` - Run only stress tests
- `-E2EOnly` - Run only end-to-end tests

**Common Usage:**
```powershell
# Fast development iteration (default)
.\build_and_test.ps1

# Full build with fast tests
.\build_and_test.ps1 -RunTests

# Quick test run without rebuild
.\build_and_test.ps1 -SkipBuild -RunTests

# Run specific test suite
.\build_and_test.ps1 -E2EOnly

# Multiple test suites
.\build_and_test.ps1 -RunTests -E2EOnly  # Unit tests + E2E tests
```

**Test Execution Times:**
- **Unit Tests** (default filter): ~10 seconds
- **Unit Tests** (all, with benchmarks): ~30 seconds
- **Integration Tests**: ~5 seconds
- **Benchmark Tests**: ~20 seconds
- **Stress Tests**: ~45 seconds
- **E2E Tests**: ~105 seconds

#### Windows (MinGW)
```bash
# Clone the repository
git clone <repository-url>
cd phyxel

# Initialize submodules (this downloads Bullet3 and GLM automatically)
git submodule update --init --recursive

# Create build directory
mkdir build && cd build

# Configure and build (Bullet3 and GLM will be built automatically)
cmake .. -G "MinGW Makefiles"
cmake --build .

# Run the application
VulkanCube.exe
```

## Usage

### Controls
- **W/A/S/D** - Move forward/left/backward/right
- **Space** - Move up
- **Left Shift** - Move down
- **Right Mouse + Drag** - Look around (camera rotation)
- **Mouse Hover** - Highlight cubes under cursor
- **Left Click** - Break/remove hovered voxel (cube/subcube/microcube)
- **C** - Place cube adjacent to hovered face
- **Shift + C** - Place subcube adjacent to hovered face
- **Ctrl + C** - Place microcube adjacent to hovered face
- **Ctrl + Left Click** - Subdivide cube into 27 subcubes
- **Alt + Left Click** - Subdivide subcube into 27 microcubes
- **Middle Click** - Subdivide cube (alternative)
- **F1** - Toggle performance overlay
- **F4** - Toggle debug rendering on/off
- **Ctrl + F4** - Cycle debug visualization modes (Wireframe → Normals → Hierarchy → UV Coords)
- **F5** - Toggle raycast visualization (show ray path, hit point, traversed voxels)
- **ESC** - Exit application

### Performance Monitoring
The application displays real-time performance metrics including:
- **FPS** (Frames Per Second)
- **Frame timing breakdown**
- **Vertex and draw call statistics**
- **Memory usage**

## Technical Details

### Rendering Pipeline
1. **Vertex Processing** - Basic cube geometry with instancing
2. **Face Culling** - Pre-calculated masks eliminate hidden faces
3. **Frustum Culling** - GPU compute shader for view frustum optimization
4. **Instance Rendering** - Single draw call for all visible cubes

### Performance Optimizations
- **Face Mask Pre-calculation** - Eliminates 192K face calculations per frame
- **Static Physics Exclusion** - 32K static cubes don't participate in physics simulation
- **Instance Data Batching** - Efficient GPU memory usage
- **Color Preservation** - Smart instance data updates preserve hover states

### Mouse Picking Algorithm
```cpp
1. Screen coordinates → Normalized Device Coordinates (NDC)
2. NDC → World space ray via inverse projection/view matrices
3. Ray-AABB intersection testing against all cube bounding boxes
4. Closest intersection determines hovered cube
5. Color modification with preservation during instance updates
```

## Documentation

Detailed technical documentation is available in the `docs/` directory:

### Core Systems
- **[DynamicSubcubeRenderPipeline.md](docs/DynamicSubcubeRenderPipeline.md)** - Dual-pipeline architecture for static and physics-enabled subcubes
- **[MultiChunkSystem.md](docs/MultiChunkSystem.md)** - Scalable chunk-based world management
- **[CoordinateSystem.md](docs/CoordinateSystem.md)** - World, chunk, and local coordinate systems
- **[ChunkUpdateOptimization.md](docs/ChunkUpdateOptimization.md)** - Performance optimization strategies

### Quick References
- **[CoordinateQuickRef.md](docs/CoordinateQuickRef.md)** - Coordinate conversion formulas
- **[IndexingReference.md](docs/IndexingReference.md)** - Array indexing patterns and algorithms

### Key Features Documentation
- **Dynamic Subcube Rendering**: Dual-pipeline system supporting both grid-aligned static subcubes (16-byte instances) and physics-enabled dynamic subcubes (32-byte instances) with seamless Bullet Physics integration
- **Multi-Chunk Architecture**: Scalable O(1) chunk lookup system supporting unlimited world size with efficient cross-chunk culling
- **Performance Optimizations**: Face culling, instance batching, and efficient GPU buffer management for 40+ FPS with 32K+ cubes

## Project History

This project began as a monolithic 2,800+ line `main.cpp` file and was successfully refactored into a clean, modular architecture. Key milestones:

1. **Performance Crisis Resolution** - Fixed 1-2 FPS to 40+ FPS
2. **Modular Refactoring** - Extracted separate subsystems
3. **Mouse Interaction** - Added ray-casting and hover detection
4. **VM Compatibility** - Right-click camera controls for virtual environments
5. **Cross-Platform Support** - Linux and Windows build compatibility

## Cross-Platform Support

VulkanCube is designed to work on both **Linux** and **Windows** platforms:

### Platform Differences
- **Linux**: Uses system packages for GLFW and GLM, builds Bullet Physics from source
- **Windows**: Uses precompiled libraries for GLFW, header-only GLM, builds Bullet Physics from source

### Dependencies Structure
```
external/
├── bullet3/           # Cross-platform (submodule, built automatically)
├── glfw/             # Windows only (prebuilt binaries for fast builds)
│   ├── include/      # Download from glfw.org
│   └── lib-vc2022/   # Multiple compiler versions supported
└── glm/              # Cross-platform (submodule, header-only)
```

### Build Strategy Rationale
- **GLFW**: Prebuilt binaries for faster builds and stability
- **Bullet3**: Submodule for latest physics features and customization
- **GLM**: Submodule header-only library (no build time impact)

### Build Configuration
The CMakeLists.txt automatically detects the platform and configures dependencies accordingly:
- **Linux**: Uses `find_package()` for system libraries, builds Bullet3 from submodule
- **Windows**: Uses local copies for GLFW/GLM, builds Bullet3 from submodule
- **Cross-platform**: Bullet3 is automatically built from the submodule on all platforms

## Development

### Adding New Features
The modular architecture makes it easy to extend:

- **New rendering effects** → Modify `RenderPipeline`
- **Physics objects** → Extend `PhysicsWorld`
- **Input handling** → Update `InputManager`
- **World objects** → Add to `ChunkManager`
- **Voxel interactions** → Extend `VoxelInteractionSystem`

### Cross-Platform Development
When adding new features:
1. Test on both Linux and Windows
2. Use cross-platform libraries when possible
3. Add platform-specific code with `#ifdef` guards if necessary
4. Update CMakeLists.txt for new dependencies

### Debugging
Enable Vulkan validation layers by building in Debug mode:

**Linux:**
```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
```

**Windows:**
```cmd
cmake --build . --config Debug
```

## License

[Add your license information here]

## Contributing

[Add contribution guidelines here]

## Performance Benchmarks

| Configuration | Cube Count | FPS | Frame Time |
|--------------|------------|-----|------------|
| 32x32x32 Grid | 32,768 | 42-46 | ~22ms |
| Optimized Culling | 32,768 | 40+ | ~24ms |
| Original (Unoptimized) | 32,768 | 1-2 | >500ms |

*Tested on modern GPU with Vulkan 1.3 support*
