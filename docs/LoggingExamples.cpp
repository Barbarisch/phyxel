// Logging System Examples - Before and After
// This file demonstrates various conversion patterns

// =============================================================================
// EXAMPLE 1: Basic Debug Output
// =============================================================================

// BEFORE:
void oldFunction() {
    std::cout << "[DEBUG] Function called" << std::endl;
    // ... work ...
    std::cout << "[DEBUG] Function complete" << std::endl;
}

// AFTER:
void newFunction() {
    LOG_DEBUG("ModuleName", "Function called");
    // ... work ...
    LOG_DEBUG("ModuleName", "Function complete");
}

// =============================================================================
// EXAMPLE 2: Error Messages
// =============================================================================

// BEFORE:
bool oldLoadTexture(const std::string& path) {
    if (!fileExists(path)) {
        std::cerr << "Failed to load texture: " << path << std::endl;
        return false;
    }
    std::cout << "[DEBUG] Successfully loaded texture: " << path << std::endl;
    return true;
}

// AFTER:
bool newLoadTexture(const std::string& path) {
    if (!fileExists(path)) {
        LOG_ERROR_FMT("Vulkan", "Failed to load texture: " << path);
        return false;
    }
    LOG_DEBUG_FMT("Vulkan", "Successfully loaded texture: " << path);
    return true;
}

// =============================================================================
// EXAMPLE 3: Formatted Output with Multiple Variables
// =============================================================================

// BEFORE:
void oldUpdatePosition(int x, int y, int z) {
    std::cout << "[DEBUG] Updating position to (" << x << "," << y << "," << z << ")" << std::endl;
    // ... update logic ...
    std::cout << "[DEBUG] Position updated successfully" << std::endl;
}

// AFTER:
void newUpdatePosition(int x, int y, int z) {
    LOG_DEBUG_FMT("Chunk", "Updating position to (" << x << "," << y << "," << z << ")");
    // ... update logic ...
    LOG_DEBUG("Chunk", "Position updated successfully");
}

// =============================================================================
// EXAMPLE 4: Performance Metrics
// =============================================================================

// BEFORE:
void oldProfileFrame() {
    auto start = std::chrono::high_resolution_clock::now();
    // ... work ...
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "[DEBUG] Frame time: " << duration.count() << "ms" << std::endl;
}

// AFTER:
void newProfileFrame() {
    auto start = std::chrono::high_resolution_clock::now();
    // ... work ...
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    LOG_DEBUG_FMT("Performance", "Frame time: " << duration.count() << "ms");
}

// =============================================================================
// EXAMPLE 5: Commented Debug Code (Now Controllable)
// =============================================================================

// BEFORE:
void oldProcessChunk() {
    // std::cout << "[DEBUG] Processing chunk..." << std::endl;
    // std::cout << "[DEBUG] Chunk contains " << cubes.size() << " cubes" << std::endl;
    
    for (auto& cube : cubes) {
        // std::cout << "[DEBUG] Processing cube at (" << cube.x << "," << cube.y << "," << cube.z << ")" << std::endl;
        processCube(cube);
    }
    
    // std::cout << "[DEBUG] Chunk processing complete" << std::endl;
}

// AFTER (Now you can enable/disable via config!):
void newProcessChunk() {
    LOG_TRACE("ChunkProcessing", "Processing chunk...");
    LOG_TRACE_FMT("ChunkProcessing", "Chunk contains " << cubes.size() << " cubes");
    
    for (auto& cube : cubes) {
        LOG_TRACE_FMT("ChunkProcessing", "Processing cube at (" << cube.x << "," << cube.y << "," << cube.z << ")");
        processCube(cube);
    }
    
    LOG_TRACE("ChunkProcessing", "Chunk processing complete");
}

// Control in logging.ini:
// [Modules]
// ChunkProcessing=OFF  # Disable when not debugging
// ChunkProcessing=TRACE  # Enable for detailed debugging

// =============================================================================
// EXAMPLE 6: Complex Multi-line Output
// =============================================================================

// BEFORE:
void oldPrintStats() {
    std::cout << "=== PERFORMANCE STATS ===" << std::endl;
    std::cout << "FPS: " << fps << std::endl;
    std::cout << "Frame Time: " << frameTime << "ms" << std::endl;
    std::cout << "Draw Calls: " << drawCalls << std::endl;
    std::cout << "=======================" << std::endl;
}

// AFTER:
void newPrintStats() {
    LOG_INFO("Performance", "=== PERFORMANCE STATS ===");
    LOG_INFO_FMT("Performance", "FPS: " << fps);
    LOG_INFO_FMT("Performance", "Frame Time: " << frameTime << "ms");
    LOG_INFO_FMT("Performance", "Draw Calls: " << drawCalls);
    LOG_INFO("Performance", "========================");
}

// =============================================================================
// EXAMPLE 7: Conditional Expensive Logging
// =============================================================================

// BEFORE (always computes expensive stats):
void oldDetailedLog() {
    std::string detailedStats = generateExpensiveStats(); // Always runs!
    // std::cout << "[DEBUG] Stats: " << detailedStats << std::endl;
}

// AFTER (only computes if logging is enabled):
void newDetailedLog() {
    if (Utils::Logger::isLevelEnabled(LogLevel::Debug, "Performance")) {
        std::string detailedStats = generateExpensiveStats(); // Only runs if needed
        LOG_DEBUG("Performance", detailedStats);
    }
}

// =============================================================================
// EXAMPLE 8: Module-Specific Logging Levels
// =============================================================================

// Instead of commenting out code, use different log levels:

void exampleFunction() {
    // Always visible (even in release builds)
    LOG_ERROR("Module", "Critical error occurred");
    LOG_WARN("Module", "Potential problem detected");
    LOG_INFO("Module", "Important milestone reached");
    
    // Only visible in debug builds (default config)
    LOG_DEBUG("Module", "Debug information");
    LOG_TRACE("Module", "Very detailed trace information");
}

// Configure per module in logging.ini:
// [Modules]
// ImportantModule=INFO    # Show INFO and above
// NoisyModule=WARN        # Only warnings and errors
// DetailedModule=TRACE    # Show everything
// UnusedModule=OFF        # Disable completely

// =============================================================================
// EXAMPLE 9: Different Modules for Different Subsystems
// =============================================================================

void exampleSubsystems() {
    // Vulkan operations
    LOG_DEBUG("Vulkan", "Creating swapchain");
    LOG_DEBUG("VulkanDevice", "Allocating GPU memory");
    
    // Physics operations
    LOG_DEBUG("Physics", "Stepping simulation");
    LOG_TRACE("Collision", "Testing AABB intersection");
    
    // Rendering operations
    LOG_DEBUG("Rendering", "Drawing frame");
    LOG_TRACE("FaceCulling", "Culling hidden faces");
    
    // World management
    LOG_DEBUG("Chunk", "Loading chunk");
    LOG_DEBUG("ChunkManager", "Managing chunk lifecycle");
}

// =============================================================================
// EXAMPLE 10: Application Initialization
// =============================================================================

// BEFORE:
int oldMain() {
    std::cout << "Starting application..." << std::endl;
    
    if (!initWindow()) {
        std::cerr << "Failed to initialize window!" << std::endl;
        return -1;
    }
    std::cout << "Window initialized" << std::endl;
    
    if (!initVulkan()) {
        std::cerr << "Failed to initialize Vulkan!" << std::endl;
        return -1;
    }
    std::cout << "Vulkan initialized" << std::endl;
    
    std::cout << "Application started successfully" << std::endl;
    return 0;
}

// AFTER:
int newMain() {
    // Initialize logging first
    Utils::Logger::loadConfig("logging.ini");
    
    LOG_INFO("Application", "Starting application...");
    
    if (!initWindow()) {
        LOG_ERROR("Application", "Failed to initialize window!");
        return -1;
    }
    LOG_INFO("Application", "Window initialized");
    
    if (!initVulkan()) {
        LOG_ERROR("Application", "Failed to initialize Vulkan!");
        return -1;
    }
    LOG_INFO("Application", "Vulkan initialized");
    
    LOG_INFO("Application", "Application started successfully");
    return 0;
}

// =============================================================================
// EXAMPLE 11: Float Precision Control
// =============================================================================

// BEFORE:
void oldPrintPosition(const glm::vec3& pos) {
    std::cout << "[DEBUG] Position: (" 
              << std::fixed << std::setprecision(2) 
              << pos.x << ", " << pos.y << ", " << pos.z << ")" << std::endl;
}

// AFTER:
void newPrintPosition(const glm::vec3& pos) {
    LOG_DEBUG_FMT("Position", "Position: (" 
                  << std::fixed << std::setprecision(2) 
                  << pos.x << ", " << pos.y << ", " << pos.z << ")");
}

// =============================================================================
// EXAMPLE 12: Different Log Levels in Same Function
// =============================================================================

void complexOperation() {
    LOG_INFO("Operation", "Starting complex operation");
    
    try {
        LOG_DEBUG("Operation", "Validating input parameters");
        validateInput();
        
        LOG_DEBUG("Operation", "Performing calculation");
        auto result = performCalculation();
        
        if (result < 0) {
            LOG_WARN_FMT("Operation", "Negative result detected: " << result);
        }
        
        LOG_DEBUG_FMT("Operation", "Calculation result: " << result);
        LOG_INFO("Operation", "Complex operation completed successfully");
        
    } catch (const std::exception& e) {
        LOG_ERROR_FMT("Operation", "Operation failed: " << e.what());
        throw;
    }
}

// =============================================================================
// SUMMARY OF BENEFITS
// =============================================================================

/*
BEFORE LOGGING SYSTEM:
- Had to comment out debug code
- No way to control verbosity
- Debug output mixed with errors
- Hard to troubleshoot production
- Cluttered codebase

AFTER LOGGING SYSTEM:
- Keep all debug code, control via config
- Fine-grained control per module
- Clear separation of log levels
- Can debug production builds
- Clean, professional codebase

TO ENABLE DEBUG FOR SPECIFIC MODULE:
Edit logging.ini:
[Modules]
MyModule=DEBUG  # or TRACE for more detail

TO DISABLE NOISY MODULE:
[Modules]
NoisyModule=OFF
*/
