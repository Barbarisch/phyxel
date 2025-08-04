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
│   └── Timer.cpp             # Performance timing utilities
├── graphics/
│   ├── VulkanDevice.cpp      # Vulkan device and resource management
│   └── RenderPipeline.cpp    # Graphics and compute pipelines
├── scene/
│   └── SceneManager.cpp      # Scene graph and cube management
├── physics/
│   └── PhysicsWorld.cpp      # Bullet physics integration
└── utils/
    ├── FileUtils.cpp         # File I/O utilities
    └── Math.cpp              # Mathematical operations
```

## Building

### Prerequisites

#### All Platforms
- **C++17** compiler
- **Vulkan SDK** (1.3+)
- **CMake** (3.10+)
- **Git** (for submodules)

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
- **Vulkan SDK** from LunarG
- **GLFW** precompiled binaries (place in `external/glfw/`)
- **GLM** headers (place in `external/glm/`)

### Build Steps

#### Linux
```bash
# Clone the repository
git clone <repository-url>
cd vulkan_game

# Initialize submodules
git submodule update --init --recursive

# Create build directory
mkdir build && cd build

# Configure and build
cmake ..
make -j$(nproc)

# Run the application
./VulkanCube
```

#### Windows (Visual Studio)
```cmd
# Clone the repository
git clone <repository-url>
cd vulkan_game

# Initialize submodules
git submodule update --init --recursive

# Ensure dependencies are in place:
# - external/glfw/include/ (GLFW headers)
# - external/glfw/lib-vc2022/ (GLFW libraries)
# - external/glm/ (GLM headers)
# - external/bullet3/ (Bullet Physics source - already included as submodule)

# Build Bullet Physics first
cd external/bullet3
mkdir build && cd build
# cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake -G "Visual Studio 17 2022" -A x64 -DUSE_MSVC_RUNTIME_LIBRARY_DLL=ON -DCMAKE_BUILD_TYPE=Debug ..
cmake --build . --config Debug
cd ../../..

# Create build directory
mkdir build && cd build

# Configure with Visual Studio
cmake .. -G "Visual Studio 16 2019" -A x64

# Build
cmake --build . --config Debug

# Run the application (will be copied to project root)
cd ..
VulkanCube.exe
```

#### Windows (MinGW)
```bash
# Clone the repository
git clone <repository-url>
cd vulkan_game

# Initialize submodules
git submodule update --init --recursive

# Build Bullet Physics
cd external/bullet3
mkdir build && cd build
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build .
cd ../../..

# Create build directory
mkdir build && cd build

# Configure and build
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
├── bullet3/           # Cross-platform (submodule, built from source)
├── glfw/             # Windows only (precompiled binaries)
│   ├── include/
│   └── lib-vc2022/
└── glm/              # Windows only (header-only library)
```

### Build Configuration
The CMakeLists.txt automatically detects the platform and configures dependencies accordingly:
- **Linux**: Uses `find_package()` for system libraries
- **Windows**: Uses local copies and manual path configuration

## Development

### Adding New Features
The modular architecture makes it easy to extend:

- **New rendering effects** → Modify `RenderPipeline`
- **Physics objects** → Extend `PhysicsWorld`
- **Input handling** → Update `Application` input methods
- **Scene objects** → Add to `SceneManager`

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
