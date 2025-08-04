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

#### Windows (Visual Studio)
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
cmake .. -G "Visual Studio 17 2022" -A x64

# Build (Bullet3 will be built automatically with proper runtime compatibility)
cmake --build . --config Debug

# Run the application (will be copied to project root)
cd ..
VulkanCube.exe
```

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
