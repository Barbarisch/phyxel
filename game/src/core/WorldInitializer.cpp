#include "core/WorldInitializer.h"
#include "ui/WindowManager.h"
#include "input/InputManager.h"
#include "vulkan/VulkanDevice.h"
#include "vulkan/RenderPipeline.h"
#include "physics/PhysicsWorld.h"
#include "core/ChunkManager.h"
#include "core/ForceSystem.h"
#include "utils/PerformanceProfiler.h"
#include "utils/PerformanceMonitor.h"
#include "utils/Timer.h"
#include "ui/ImGuiRenderer.h"
#include "utils/Logger.h"
#include "core/EngineConfig.h"
#include "core/AssetManager.h"
#include "examples/MultiChunkDemo.h"
#include <GLFW/glfw3.h>

namespace Phyxel {
namespace Core {

WorldInitializer::WorldInitializer(
    UI::WindowManager* windowManager,
    Input::InputManager* inputManager,
    Vulkan::VulkanDevice* vulkanDevice,
    Vulkan::RenderPipeline* renderPipeline,
    Vulkan::RenderPipeline* dynamicRenderPipeline,
    Physics::PhysicsWorld* physicsWorld,
    Timer* timer,
    ChunkManager* chunkManager,
    ForceSystem* forceSystem,
    MouseVelocityTracker* mouseVelocityTracker,
    PerformanceProfiler* performanceProfiler,
    Utils::PerformanceMonitor* performanceMonitor,
    UI::ImGuiRenderer* imguiRenderer,
    const EngineConfig* config
)
    : windowManager(windowManager)
    , inputManager(inputManager)
    , vulkanDevice(vulkanDevice)
    , renderPipeline(renderPipeline)
    , dynamicRenderPipeline(dynamicRenderPipeline)
    , physicsWorld(physicsWorld)
    , timer(timer)
    , chunkManager(chunkManager)
    , forceSystem(forceSystem)
    , mouseVelocityTracker(mouseVelocityTracker)
    , performanceProfiler(performanceProfiler)
    , performanceMonitor(performanceMonitor)
    , imguiRenderer(imguiRenderer)
    , engineConfig(config)
{
}

bool WorldInitializer::initialize() {
    // Initialize logging system first
    auto& assets = AssetManager::instance();
    Utils::Logger::loadConfig(assets.loggingConfigPath()); // Load config if exists
    LOG_INFO("WorldInitializer", "Initializing Phyxel Application...");
    LOG_INFO("WorldInitializer", "Logging system initialized (check logging.ini for configuration)");

    // Initialize window first
    if (!initializeWindow()) {
        LOG_ERROR("WorldInitializer", "Failed to initialize window!");
        return false;
    }

    // Initialize subsystems
    if (!initializeVulkan()) {
        LOG_ERROR("WorldInitializer", "Failed to initialize Vulkan!");
        return false;
    }

    // Initialize texture atlas system
    if (!initializeTextureAtlas()) {
        LOG_ERROR("WorldInitializer", "Failed to initialize texture atlas!");
        return false;
    }

    // Initialize chunk manager after Vulkan is ready
    chunkManager->initialize(vulkanDevice->getDevice(), vulkanDevice->getPhysicalDevice());
    
    // Set physics world for proper cleanup of dynamic objects
    chunkManager->setPhysicsWorld(physicsWorld);
    
    // Initialize world storage for persistent chunks
    if (!chunkManager->initializeWorldStorage(assets.worldDatabasePath())) {
        LOG_WARN("WorldInitializer", "Failed to initialize world storage. Using temporary chunks.");
    }
    
    // Try to load all chunks from database first
    std::vector<glm::ivec3> loadedChunks = chunkManager->loadAllChunksFromDatabase();
    
    if (loadedChunks.empty()) {
        // No chunks in database - create initial world
        // LOG_INFO("WorldInitializer", "No chunks found in database - creating initial world");
        // auto origins = MultiChunkDemo::create3DGridChunks(2, 2, 2);
        
        // for (const auto& origin : origins) {
        //     glm::ivec3 chunkCoord = chunkManager->worldToChunkCoord(origin);
        //     chunkManager->generateOrLoadChunk(chunkCoord);
        // }
        LOG_INFO("WorldInitializer", "No chunks found in database - starting with EMPTY world for Python scripting");
    } else {
        LOG_INFO_FMT("WorldInitializer", "Loaded " << loadedChunks.size() << " chunks from database");
    }
    
    // Rebuild faces for all chunks AFTER all are loaded (critical for cross-chunk culling)
    chunkManager->rebuildAllChunkFaces();
    
    // Initialize hash maps for optimized O(1) hover detection
    chunkManager->initializeAllChunkVoxelMaps();
    
    // Calculate face culling optimization after chunk creation (DISABLED for now)
    // chunkManager->calculateChunkFaceCulling();
    
    // Cross-chunk occlusion culling is now handled in rebuildAllChunkFaces()
    // chunkManager->performOcclusionCulling();
    
    LOG_INFO_FMT("WorldInitializer", "Created " << chunkManager->chunks.size() << " chunks for testing");

    // if (!initializeScene()) {
    //     std::cerr << "Failed to initialize scene!" << std::endl;
    //     return false;
    // }

    // Initialize input manager and register actions
    if (!inputManager->initialize(windowManager->getHandle())) {
        LOG_ERROR("WorldInitializer", "Failed to initialize input manager!");
        return false;
    }
    
    // Register InputManager callbacks with WindowManager (delegation)
    windowManager->setCursorPosCallback([this](double x, double y) {
        inputManager->handleMouseMove(x, y);
    });
    
    windowManager->setMouseButtonCallback([this](int button, int action, int mods) {
        inputManager->handleMouseButton(button, action, mods);
    });
    
    // Set up initial camera position and orientation
    inputManager->setCameraPosition(glm::vec3(50.0f, 50.0f, 50.0f));
    glm::vec3 lookAt = glm::normalize(glm::vec3(16.0f, 16.0f, 16.0f) - glm::vec3(50.0f, 50.0f, 50.0f));
    inputManager->setCameraFront(lookAt);
    inputManager->setYawPitch(-135.0f, -30.0f);
    
    // Register mouse position callback for mouse velocity tracker
    inputManager->setMousePositionCallback([this](double x, double y) {
        mouseVelocityTracker->updatePosition(x, y);
    });

    if (!initializePhysics()) {
        LOG_ERROR("WorldInitializer", "Failed to initialize physics!");
        return false;
    }

    // Create physics bodies for all chunks AFTER physics world is initialized
    // Each chunk will be a compound shape made from individual cube collision boxes
    LOG_INFO("WorldInitializer", "Creating chunk physics bodies...");
    for (auto& chunk : chunkManager->chunks) {
        chunk->setPhysicsWorld(physicsWorld);
        chunk->createChunkPhysicsBody();
    }

    if (!loadAssets()) {
        LOG_ERROR("WorldInitializer", "Failed to load assets!");
        return false;
    }

    // Initialize ImGui after Vulkan is fully set up
    if (!imguiRenderer->initialize(windowManager->getHandle(), vulkanDevice, renderPipeline->getRenderPass())) {
        LOG_ERROR("WorldInitializer", "Failed to initialize ImGui!");
        return false;
    }

    timer->start();
    LOG_INFO("WorldInitializer", "Application initialized successfully!");
    return true;
}

bool WorldInitializer::initializeWindow() {
    LOG_INFO("WorldInitializer", "Initializing window");
    
    int w = 1600, h = 900;
    std::string title = "Phyxel - Voxel Physics Engine";
    if (engineConfig) {
        w = engineConfig->windowWidth;
        h = engineConfig->windowHeight;
        title = engineConfig->windowTitle;
    }
    if (!windowManager->initialize(w, h, title.c_str())) {
        LOG_ERROR("WorldInitializer", "Failed to initialize window manager");
        return false;
    }
    
    LOG_INFO("WorldInitializer", "Window initialization complete");
    return true;
}

bool WorldInitializer::initializeVulkan() {
    auto& assets = AssetManager::instance();
    // Create Vulkan instance first
    if (!vulkanDevice->createInstance()) {
        LOG_ERROR("WorldInitializer", "Failed to create Vulkan instance!");
        return false;
    }

    // Setup debug messenger
    if (!vulkanDevice->setupDebugMessenger()) {
        LOG_ERROR("WorldInitializer", "Failed to setup debug messenger!");
        return false;
    }

    // Create Vulkan surface with the window (must be done before device selection)
    if (!vulkanDevice->createSurface(windowManager->getHandle())) {
        LOG_ERROR("WorldInitializer", "Failed to create Vulkan surface!");
        return false;
    }

    // Now pick physical device and create logical device
    if (!vulkanDevice->pickPhysicalDevice()) {
        LOG_ERROR("WorldInitializer", "Failed to pick physical device!");
        return false;
    }

    if (!vulkanDevice->createLogicalDevice()) {
        LOG_ERROR("WorldInitializer", "Failed to create logical device!");
        return false;
    }

    // Create swapchain
    if (!vulkanDevice->createSwapChain(windowManager->getWidth(), windowManager->getHeight())) {
        LOG_ERROR("WorldInitializer", "Failed to create swapchain!");
        return false;
    }

    // Create render pass
    if (!renderPipeline->createRenderPass()) {
        LOG_ERROR("WorldInitializer", "Failed to create render pass!");
        return false;
    }

    // Create depth resources
    if (!vulkanDevice->createDepthResources()) {
        LOG_ERROR("WorldInitializer", "Failed to create depth resources!");
        return false;
    }

    // Create framebuffers
    if (!vulkanDevice->createFramebuffers(renderPipeline->getRenderPass())) {
        LOG_ERROR("WorldInitializer", "Failed to create framebuffers!");
        return false;
    }

    // Load shaders for static pipeline
    if (!renderPipeline->loadShaders(assets.resolveShader("static_voxel.vert.spv"), assets.resolveShader("voxel.frag.spv"))) {
        LOG_ERROR("WorldInitializer", "Failed to load static graphics shaders!");
        return false;
    }

    // Load shaders for dynamic subcube pipeline
    if (!dynamicRenderPipeline->loadShaders(assets.resolveShader("dynamic_voxel.vert.spv"), assets.resolveShader("voxel.frag.spv"))) {
        LOG_ERROR("WorldInitializer", "Failed to load dynamic subcube graphics shaders!");
        return false;
    }
    
    // Load debug shaders for debug visualization pipeline
    if (!renderPipeline->loadDebugShaders(assets.resolveShader("debug_voxel.vert.spv"), assets.resolveShader("debug_voxel.frag.spv"))) {
        LOG_ERROR("WorldInitializer", "Failed to load debug pipeline shaders!");
        return false;
    }
    
    // Load debug line shaders for raycast visualization
    if (!renderPipeline->loadDebugLineShaders(assets.resolveShader("debug_line.vert.spv"), assets.resolveShader("debug_line.frag.spv"))) {
        LOG_ERROR("WorldInitializer", "Failed to load debug line shaders!");
        return false;
    }

    // Load character shaders
    if (!renderPipeline->loadCharacterShaders(assets.resolveShader("character.vert.spv"), assets.resolveShader("character.frag.spv"))) {
        LOG_ERROR("WorldInitializer", "Failed to load character pipeline shaders!");
        return false;
    }

    // Load instanced character shaders
    if (!renderPipeline->loadInstancedCharacterShaders(assets.resolveShader("character_instanced.vert.spv"), assets.resolveShader("character.frag.spv"))) {
        LOG_ERROR("WorldInitializer", "Failed to load instanced character pipeline shaders!");
        return false;
    }

    // TODO: Compute shader functionality for frustum culling (experimental/incomplete)
    /*
    if (!renderPipeline->loadComputeShader("shaders/frustum_cull.comp.spv")) {
        LOG_ERROR("WorldInitializer", "Failed to load compute shader!");
        return false;
    }
    */

    // Create descriptor set layout before graphics pipeline
    if (!vulkanDevice->createDescriptorSetLayout()) {
        LOG_ERROR("WorldInitializer", "Failed to create descriptor set layout!");
        return false;
    }

    if (!renderPipeline->createGraphicsPipeline()) {
        LOG_ERROR("WorldInitializer", "Failed to create static graphics pipeline!");
        return false;
    }

    if (!dynamicRenderPipeline->createGraphicsPipelineForDynamicSubcubes()) {
        LOG_ERROR("WorldInitializer", "Failed to create dynamic graphics pipeline!");
        return false;
    }
    
    if (!renderPipeline->createDebugGraphicsPipeline()) {
        LOG_ERROR("WorldInitializer", "Failed to create debug graphics pipeline!");
        return false;
    }
    
    if (!renderPipeline->createDebugLinePipeline()) {
        LOG_ERROR("WorldInitializer", "Failed to create debug line pipeline!");
        return false;
    }

    if (!renderPipeline->createCharacterPipeline()) {
        LOG_ERROR("WorldInitializer", "Failed to create character graphics pipeline!");
        return false;
    }

    if (!renderPipeline->createInstancedCharacterPipeline()) {
        LOG_ERROR("WorldInitializer", "Failed to create instanced character graphics pipeline!");
        return false;
    }

    // TODO: Compute pipeline functionality (experimental/incomplete)
    /*
    if (!renderPipeline->createComputePipeline()) {
        return false;
    }
    */

    // Note: Compute descriptor sets will be created after scene initialization
    // when we have actual AABB data to bind

    // Create command buffers
    if (!vulkanDevice->createCommandBuffers()) {
        LOG_ERROR("WorldInitializer", "Failed to create command buffers!");
        return false;
    }

    // Create rendering buffers
    if (!vulkanDevice->createVertexBuffer()) {
        LOG_ERROR("WorldInitializer", "Failed to create vertex buffer!");
        return false;
    }

    if (!vulkanDevice->createIndexBuffer()) {
        LOG_ERROR("WorldInitializer", "Failed to create index buffer!");
        return false;
    }

    if (!vulkanDevice->createInstanceBuffer()) {
        LOG_ERROR("WorldInitializer", "Failed to create instance buffer!");
        return false;
    }

    // Create dynamic subcube buffer (support up to 1000 dynamic subcubes)
    if (!vulkanDevice->createDynamicSubcubeBuffer(1000)) {
        LOG_ERROR("WorldInitializer", "Failed to create dynamic subcube buffer!");
        return false;
    }

    // TODO: Frustum culling buffers functionality (experimental/incomplete)
    /*
    // Create frustum culling buffers (support up to 35,000 instances)
    if (!vulkanDevice->createFrustumCullingBuffers(35000)) {
        LOG_ERROR("WorldInitializer", "Failed to create frustum culling buffers!");
        return false;
    }
    */

    // Create dynamic subcube buffer (support up to 1000 dynamic subcubes)
    if (!vulkanDevice->createDynamicSubcubeBuffer(1000)) {
        LOG_ERROR("WorldInitializer", "Failed to create dynamic subcube buffer!");
        return false;
    }

    if (!vulkanDevice->createUniformBuffers()) {
        LOG_ERROR("WorldInitializer", "Failed to create uniform buffers!");
        return false;
    }

    if (!vulkanDevice->createLightBuffers()) {
        LOG_ERROR("WorldInitializer", "Failed to create light SSBO buffers!");
        return false;
    }

    if (!vulkanDevice->createDescriptorPool()) {
        LOG_ERROR("WorldInitializer", "Failed to create descriptor pool!");
        return false;
    }

    // TODO: Compute descriptor pool functionality (experimental/incomplete)
    /*
    if (!vulkanDevice->createComputeDescriptorPool()) {
        LOG_ERROR("WorldInitializer", "Failed to create compute descriptor pool!");
        return false;
    }
    */

    if (!vulkanDevice->createDescriptorSets()) {
        LOG_ERROR("WorldInitializer", "Failed to create descriptor sets!");
        return false;
    }

    // Create synchronization objects
    if (!vulkanDevice->createSyncObjects()) {
        LOG_ERROR("WorldInitializer", "Failed to create sync objects!");
        return false;
    }

    LOG_INFO("WorldInitializer", "Vulkan subsystem initialized successfully");
    return true;
}

bool WorldInitializer::initializeTextureAtlas() {
    auto& assets = AssetManager::instance();
    LOG_INFO("WorldInitializer", "Initializing texture atlas system...");
    
    // Load the texture atlas
    if (!vulkanDevice->loadTextureAtlas(assets.textureAtlasPath())) {
        LOG_ERROR("WorldInitializer", "Failed to load texture atlas!");
        return false;
    }
    
    // Create the texture sampler
    if (!vulkanDevice->createTextureAtlasSampler()) {
        LOG_ERROR("WorldInitializer", "Failed to create texture atlas sampler!");
        return false;
    }
    
    // Update descriptor sets with texture binding
    vulkanDevice->updateDescriptorSetsWithTexture();
    
    LOG_INFO("WorldInitializer", "Texture atlas system initialized successfully");
    return true;
}

bool WorldInitializer::initializeScene() {
    // Scene initialization now handled by ChunkManager
    // No need for separate scene manager initialization

    LOG_INFO("WorldInitializer", "Scene subsystem initialized successfully");
    return true;
}

bool WorldInitializer::initializePhysics() {
    if (!physicsWorld->initialize()) {
        return false;
    }

    LOG_INFO("WorldInitializer", "Physics subsystem initialized successfully (static cubes excluded)");
    return true;
}

bool WorldInitializer::loadAssets() {
    // Load textures, models, etc.
    // For now, just return true as we have basic cube rendering
    
    LOG_INFO("WorldInitializer", "Assets loaded successfully");
    return true;
}

} // namespace Core
} // namespace Phyxel
