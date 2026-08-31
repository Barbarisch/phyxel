#include "core/GameShell.h"
#include "core/EngineRuntime.h"
#include "core/SceneManager.h"
#include "graphics/Camera.h"
#include "input/InputManager.h"
#include "scene/AnimatedVoxelCharacter.h"
#include "utils/Logger.h"

#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

void GameShell::startTestApi(EngineRuntime& engine, int port, const std::string& name) {
    gameApi_.runtime          = &engine;
    gameApi_.renderCoordinator = apiRenderCoordinator();
    gameApi_.npcManager       = apiNPCManager();
    gameApi_.triggers         = apiTriggerSystem();
    gameApi_.screen           = apiScreen();
    gameApi_.entityRegistry   = apiEntityRegistry();
    gameApi_.playerProvider   = [this]() { return apiPlayer(); };
    gameApi_.combatDirector   = apiCombatDirector();
    gameApi_.combatAI         = apiCombatAI();
    gameApi_.combatSystem     = apiCombatSystem();
    gameApi_.playerTurn       = apiPlayerTurn();
    gameApi_.playerSheet      = apiPlayerSheet();
    gameApi_.inventory        = apiInventory();
    gameApi_.projectName      = name;
    if (gameApi_.start(port))
        LOG_WARN("GameShell", "*** TEST API ENABLED on 127.0.0.1:{} — dev/test build, do NOT ship ***", port);
}

void GameShell::pumpTestApi() { gameApi_.pump(); }
void GameShell::stopTestApi() { gameApi_.stop(); }

void GameShell::updateGameplayCamera(EngineRuntime& engine, float dt,
                                     Scene::AnimatedVoxelCharacter* character,
                                     bool driveCharacter) {
    auto* input = engine.getInputManager();
    auto* cam   = engine.getCamera();
    if (!input || !cam) return;

    // Resolve the rig + scheme from the active scene's camera block — once, and
    // again whenever the active scene changes (each scene may author its own
    // camera.mode / camera.controlScheme).
    auto* sm = engine.getSceneManager();
    const auto* active = sm ? sm->getActiveScene() : nullptr;
    const std::string sceneId = active ? active->id : std::string();
    if (!cameraResolved_ || sceneId != cameraResolvedScene_) {
        std::string rigName = defaultRigName();
        std::string schemeName = defaultSchemeName();
        if (active && active->definition.contains("camera")) {
            const auto& camDef = active->definition["camera"];
            rigName = camDef.value("mode", rigName);
            schemeName = camDef.value("controlScheme", schemeName);
        }
        // Unknown names (incl. mode "free") fall back to the shell defaults.
        if (!cameraController_.setRigByName(rigName))
            cameraController_.setRigByName(defaultRigName());
        if (!cameraController_.setSchemeByName(schemeName))
            cameraController_.setSchemeByName(defaultSchemeName());
        if (auto* rig = cameraController_.rig()) {
            // Eye height: the rig ships a generic 0.5 (≈ knee height on a humanoid),
            // which left first-person looking out of the character's shins. Derive it
            // from THIS character's controller height so the eye sits near the top of
            // the body (feet + ~1.7 for the default ~1.9-tall humanoid) and scales with
            // the model. worldPosition (the rig target) is the capsule BOTTOM/feet, and
            // the controller half-height is half the full height, so full height =
            // 2×halfHeight; 1.8×halfHeight ≈ 90% of full height. An explicit game.json
            // camera.eyeHeight still wins.
            if (character)
                rig->eyeHeight = character->getControllerHalfHeight() * 1.8f;
            if (active && active->definition.contains("camera") &&
                active->definition["camera"].contains("eyeHeight"))
                rig->eyeHeight = active->definition["camera"].value("eyeHeight", rig->eyeHeight);
            onCameraRigResolved(*rig);
        }
        cameraResolved_ = true;
        cameraResolvedScene_ = sceneId;
    }

    cameraController_.update(dt, *input, character, *cam,
                             /*advanceCharacter=*/true, driveCharacter);

    // Update-LOD: publish the viewer position so AnimatedVoxelCharacter can tick
    // distant characters at a reduced rate. This is the standalone-game analog of
    // the editor's per-frame setViewerPosition call, and is essential for crowds
    // (100s of characters) where most are far from the camera at any moment.
    Scene::AnimatedVoxelCharacter::setViewerPosition(cam->getPosition());
}

} // namespace Core
} // namespace Phyxel
