#include "core/SceneDefinition.h"
#include "utils/Logger.h"

namespace Phyxel {
namespace Core {

// ============================================================================
// SceneDefinition
// ============================================================================

SceneDefinition SceneDefinition::fromJson(const json& j) {
    SceneDefinition scene;
    scene.id = j.value("id", "");
    scene.name = j.value("name", scene.id);
    scene.description = j.value("description", "");
    scene.worldDatabase = j.value("worldDatabase", "");
    scene.onEnterScript = j.value("onEnterScript", "");
    scene.onExitScript = j.value("onExitScript", "");

    if (j.contains("transitionStyle")) {
        scene.transitionStyle = transitionStyleFromString(j["transitionStyle"].get<std::string>());
    }

    // The scene's game definition is the scene object itself, minus the scene-management keys.
    // Copy the entire object, then use it as the definition.
    scene.definition = j;
    // Remove scene-management-only keys from the definition so GameDefinitionLoader
    // doesn't encounter unknown fields.
    scene.definition.erase("id");
    scene.definition.erase("name");
    scene.definition.erase("description");
    scene.definition.erase("worldDatabase");
    scene.definition.erase("onEnterScript");
    scene.definition.erase("onExitScript");
    scene.definition.erase("transitionStyle");

    return scene;
}

json SceneDefinition::toJson() const {
    json j = definition; // Start with the game definition content
    j["id"] = id;
    j["name"] = name;
    if (!description.empty()) j["description"] = description;
    if (!worldDatabase.empty()) j["worldDatabase"] = worldDatabase;
    if (!onEnterScript.empty()) j["onEnterScript"] = onEnterScript;
    if (!onExitScript.empty()) j["onExitScript"] = onExitScript;
    if (transitionStyle != SceneTransitionStyle::LoadingScreen) {
        j["transitionStyle"] = transitionStyleToString(transitionStyle);
    }
    return j;
}

// ============================================================================
// SceneManifest
// ============================================================================

bool SceneManifest::isMultiScene(const json& gameDef) {
    return gameDef.contains("scenes") && gameDef["scenes"].is_array();
}

const SceneDefinition* SceneManifest::findScene(const std::string& sceneId) const {
    for (const auto& scene : scenes) {
        if (scene.id == sceneId) return &scene;
    }
    return nullptr;
}

SceneManifest SceneManifest::fromJson(const json& j) {
    SceneManifest manifest;

    manifest.startScene = j.value("startScene", "");

    if (j.contains("playerDefaults")) {
        manifest.playerDefaults = j["playerDefaults"];
    }

    if (j.contains("globalStory")) {
        manifest.globalStory = j["globalStory"];
    }

    if (j.contains("scenes") && j["scenes"].is_array()) {
        for (const auto& sceneDef : j["scenes"]) {
            manifest.scenes.push_back(SceneDefinition::fromJson(sceneDef));
        }
    }

    // Default startScene to the first scene if not specified
    if (manifest.startScene.empty() && !manifest.scenes.empty()) {
        manifest.startScene = manifest.scenes.front().id;
    }

    return manifest;
}

json SceneManifest::toJson() const {
    json j;
    j["startScene"] = startScene;

    if (!playerDefaults.is_null()) {
        j["playerDefaults"] = playerDefaults;
    }
    if (!globalStory.is_null()) {
        j["globalStory"] = globalStory;
    }

    json scenesArr = json::array();
    for (const auto& scene : scenes) {
        scenesArr.push_back(scene.toJson());
    }
    j["scenes"] = scenesArr;

    return j;
}

} // namespace Core
} // namespace Phyxel
