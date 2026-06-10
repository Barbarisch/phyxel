#include "core/GameShell.h"
#include "core/EngineRuntime.h"
#include "core/SceneManager.h"
#include "graphics/Camera.h"
#include "input/InputManager.h"
#include "scene/AnimatedVoxelCharacter.h"

#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

void GameShell::updateGameplayCamera(EngineRuntime& engine, float dt,
                                     Scene::AnimatedVoxelCharacter* character) {
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
        if (cameraController_.rig()) onCameraRigResolved(*cameraController_.rig());
        cameraResolved_ = true;
        cameraResolvedScene_ = sceneId;
    }

    cameraController_.update(dt, *input, character, *cam);
}

} // namespace Core
} // namespace Phyxel
