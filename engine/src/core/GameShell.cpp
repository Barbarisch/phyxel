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

    cameraController_.update(dt, *input, character, *cam);

    // Update-LOD: publish the viewer position so AnimatedVoxelCharacter can tick
    // distant characters at a reduced rate. This is the standalone-game analog of
    // the editor's per-frame setViewerPosition call, and is essential for crowds
    // (100s of characters) where most are far from the camera at any moment.
    Scene::AnimatedVoxelCharacter::setViewerPosition(cam->getPosition());
}

} // namespace Core
} // namespace Phyxel
