#include "MinimalGame.h"
#include "core/ChunkManager.h"
#include "graphics/Camera.h"
#include "graphics/CameraManager.h"
#include "graphics/RenderCoordinator.h"
#include "input/InputManager.h"
#include "vulkan/VulkanDevice.h"
#include "vulkan/RenderPipeline.h"
#include "ui/WindowManager.h"
#include "ui/ImGuiRenderer.h"
#include "ui/GameScreen.h"
#include "ui/GameMenus.h"
#include "utils/PerformanceProfiler.h"
#include "utils/PerformanceMonitor.h"
#include "utils/Logger.h"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

namespace Examples {

// ============================================================================
// Game-owned subsystems
// ============================================================================

// The game owns a RenderCoordinator (it has game-specific wiring)
static std::unique_ptr<Phyxel::Graphics::RenderCoordinator> renderCoordinator;

// ============================================================================
// GameCallbacks implementation
// ============================================================================

bool MinimalGame::onInitialize(Phyxel::Core::EngineRuntime& engine) {
    LOG_INFO("MinimalGame", "Initializing minimal example game...");

    auto* chunkMgr = engine.getChunkManager();

    // Build a 32x1x32 stone platform at Y=15
    for (int x = 0; x < 32; ++x) {
        for (int z = 0; z < 32; ++z) {
            chunkMgr->addCube(glm::ivec3(x, 15, z));
        }
    }

    // Build a few pillars
    for (int y = 16; y < 22; ++y) {
        chunkMgr->addCube(glm::ivec3(0,  y, 0));
        chunkMgr->addCube(glm::ivec3(31, y, 0));
        chunkMgr->addCube(glm::ivec3(0,  y, 31));
        chunkMgr->addCube(glm::ivec3(31, y, 31));
    }

    chunkMgr->rebuildAllChunkFaces();
    LOG_INFO("MinimalGame", "Built 32x32 platform with 4 pillars");

    // Position camera above the platform looking down
    auto* cam = engine.getCamera();
    cam->setPosition(glm::vec3(16.0f, 30.0f, 16.0f));
    cam->setYaw(-90.0f);
    cam->setPitch(-35.0f);

    // Sync InputManager so camera movement works
    auto* input = engine.getInputManager();
    input->setCameraPosition(cam->getPosition());
    input->setYawPitch(cam->getYaw(), cam->getPitch());

    // Create a RenderCoordinator for frame rendering
    renderCoordinator = std::make_unique<Phyxel::Graphics::RenderCoordinator>(
        engine.getVulkanDevice(),
        engine.getRenderPipeline(),
        engine.getDynamicRenderPipeline(),
        engine.getImGuiRenderer(),
        engine.getWindowManager(),
        engine.getInputManager(),
        engine.getCamera(),
        engine.getChunkManager(),
        engine.getPerformanceMonitor(),
        engine.getPerformanceProfiler(),
        nullptr,  // No raycast visualizer
        nullptr   // No scripting system
    );

    // Populate inventory with some starter items
    inventory_.addItem("Stone", 32);
    inventory_.addItem("Wood", 16);
    inventory_.addItem("Metal", 8);
    inventory_.addItem("Glass", 4);

    // Start on main menu
    screen_.setState(Phyxel::UI::ScreenState::MainMenu);

    initialized_ = true;
    LOG_INFO("MinimalGame", "Minimal game initialized");
    return true;
}

void MinimalGame::onHandleInput(Phyxel::Core::EngineRuntime& engine) {
    auto* input = engine.getInputManager();
    if (!input) return;

    auto state = screen_.getState();

    // ESC: pause toggle (in gameplay) or resume (in pause)
    if (input->isKeyPressed(GLFW_KEY_ESCAPE)) {
        if (state == Phyxel::UI::ScreenState::Playing) {
            screen_.togglePause();
        } else if (state == Phyxel::UI::ScreenState::Paused) {
            screen_.resume();
        } else if (state == Phyxel::UI::ScreenState::Inventory) {
            screen_.resume();
        }
    }

    // Tab: toggle inventory (in gameplay or inventory screen)
    if (input->isKeyPressed(GLFW_KEY_TAB)) {
        if (state == Phyxel::UI::ScreenState::Playing ||
            state == Phyxel::UI::ScreenState::Inventory) {
            screen_.toggleInventory();
        }
    }

    // Only process camera/movement input when actually playing
    if (Phyxel::UI::isGameRunning(screen_.getState())) {
        input->processInput(0.016f);
    }
}

void MinimalGame::onUpdate(Phyxel::Core::EngineRuntime& engine, float dt) {
    if (!Phyxel::UI::isGameRunning(screen_.getState())) return;

    elapsed_ += dt;

    // Update physics at fixed step
    auto* physics = engine.getPhysicsWorld();
    if (physics) {
        physics->stepSimulation(dt);
    }
}

void MinimalGame::onRender(Phyxel::Core::EngineRuntime& engine) {
    auto* window = engine.getWindowManager();
    if (window && window->isMinimized()) return;

    auto* imgui = engine.getImGuiRenderer();
    if (imgui) {
        imgui->newFrame();

        auto state = screen_.getState();

        switch (state) {
        case Phyxel::UI::ScreenState::MainMenu:
            Phyxel::UI::renderMainMenu("Phyxel", {
                [this]() { screen_.startGame(); },
                [&engine]() {
                    auto* w = engine.getWindowManager();
                    if (w) glfwSetWindowShouldClose(w->getHandle(), GLFW_TRUE);
                }
            });
            break;

        case Phyxel::UI::ScreenState::Playing:
            Phyxel::UI::renderGameHUD(&health_, &inventory_);
            break;

        case Phyxel::UI::ScreenState::Paused:
            Phyxel::UI::renderGameHUD(&health_, &inventory_);
            Phyxel::UI::renderPauseMenu({
                [this]() { screen_.resume(); },
                [this]() { screen_.returnToMainMenu(); },
                [&engine]() {
                    auto* w = engine.getWindowManager();
                    if (w) glfwSetWindowShouldClose(w->getHandle(), GLFW_TRUE);
                }
            });
            break;

        case Phyxel::UI::ScreenState::Inventory:
            Phyxel::UI::renderGameHUD(&health_, &inventory_);
            Phyxel::UI::renderInventoryScreen(&inventory_,
                [this]() { screen_.resume(); });
            break;
        }

        imgui->endFrame();
    }

    // Render the world
    if (renderCoordinator) {
        renderCoordinator->render();
    }
}

void MinimalGame::onShutdown() {
    LOG_INFO("MinimalGame", "Shutting down minimal game...");
    renderCoordinator.reset();
    initialized_ = false;
}

} // namespace Examples
