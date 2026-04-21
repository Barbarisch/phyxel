// Include standard headers BEFORE the hack to avoid CRT conflicts
#include <vector>
#include <string>
#include <memory>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <iostream>
#include <map>
#include <unordered_map>
#include <set>
#include <unordered_set>
#include <functional>

// Hack to fix broken Python 3.13 environment where pyconfig.h defaults to free-threaded
// but only standard library is available.
#define Py_BUILD_CORE_BUILTIN
// Temporarily undefine _DEBUG to prevent pyconfig.h from defining Py_DEBUG
// and linking against debug libraries (which are missing)
#ifdef _DEBUG
    #undef _DEBUG
    #include <pyconfig.h>
    #define _DEBUG
#else
    #include <pyconfig.h>
#endif
#undef Py_BUILD_CORE_BUILTIN
#ifdef Py_GIL_DISABLED
#undef Py_GIL_DISABLED
#endif

#include <pybind11/pybind11.h>

#include "utils/Logger.h"
#include "Application.h"
#include "scripting/ScriptingSystem.h"
#include "scene/VoxelInteractionSystem.h"
#include "core/ObjectTemplateManager.h"
#include "core/AudioSystem.h"
#include "core/EntityRegistry.h"
#include "core/EngineAPIServer.h"
#include "input/InputManager.h"

namespace py = pybind11;

namespace Phyxel {

// Global pointer to application instance
static Application* g_appInstance = nullptr;

// Function to set the app instance (called from ScriptingSystem via registered callback)
static void setScriptingAppInstance(Application* app) {
    g_appInstance = app;
}

// Auto-register at static init time so ScriptingSystem can call back
static struct BindingsRegistration {
    BindingsRegistration() {
        ScriptingSystem::registerAppInstanceSetter(&setScriptingAppInstance);
    }
} s_bindingsReg;

} // namespace Phyxel

using namespace Phyxel;

PYBIND11_EMBEDDED_MODULE(phyxel, m) {
    m.doc() = "Phyxel Game Engine API";

    // Expose Logger as a submodule (since it's a singleton with private destructor)
    auto logger = m.def_submodule("Logger", "Logging utilities");
    
    logger.def("info", [](const std::string& category, const std::string& msg) {
            Utils::Logger::getInstance().log(Utils::LogLevel::Info, category, msg);
        }, "Log an info message");
        
    logger.def("error", [](const std::string& category, const std::string& msg) {
            Utils::Logger::getInstance().log(Utils::LogLevel::Error, category, msg);
        }, "Log an error message");
        
    logger.def("warn", [](const std::string& category, const std::string& msg) {
            Utils::Logger::getInstance().log(Utils::LogLevel::Warn, category, msg);
        }, "Log a warning message");
        
    logger.def("debug", [](const std::string& category, const std::string& msg) {
            Utils::Logger::getInstance().log(Utils::LogLevel::Debug, category, msg);
        }, "Log a debug message");

    // Expose VoxelInteractionSystem
    py::class_<VoxelInteractionSystem>(m, "VoxelInteractionSystem")
        .def("place_voxel_at_hover", &VoxelInteractionSystem::placeVoxelAtHover)
        .def("place_subcube_at_hover", &VoxelInteractionSystem::placeSubcubeAtHover)
        .def("place_microcube_at_hover", &VoxelInteractionSystem::placeMicrocubeAtHover)
        .def("break_hovered_subcube", &VoxelInteractionSystem::breakHoveredSubcube)
        .def("break_hovered_microcube", &VoxelInteractionSystem::breakHoveredMicrocube)
        .def("has_hovered_cube", &VoxelInteractionSystem::hasHoveredCube);

    // Expose ChunkManager
    py::class_<ChunkManager>(m, "ChunkManager")
        .def("add_cube", [](ChunkManager& cm, int x, int y, int z) {
            return cm.addCube(glm::ivec3(x, y, z));
        }, "Add a cube at the specified world coordinates")
        .def("add_cube_with_material", [](ChunkManager& cm, int x, int y, int z, const std::string& material) {
            return cm.m_voxelModificationSystem.addCubeWithMaterial(glm::ivec3(x, y, z), material);
        }, "Add a cube with specific material at the specified world coordinates")
        .def("remove_cube", [](ChunkManager& cm, int x, int y, int z) {
            return cm.removeCube(glm::ivec3(x, y, z));
        }, "Remove a cube at the specified world coordinates");

    // Expose ObjectTemplateManager
    py::class_<ObjectTemplateManager>(m, "ObjectTemplateManager")
        .def("spawn_template", [](ObjectTemplateManager& tm, const std::string& name, float x, float y, float z, bool isStatic) {
            return tm.spawnTemplate(name, glm::vec3(x, y, z), isStatic);
        }, "Spawn a template at a specific world position", 
           py::arg("name"), py::arg("x"), py::arg("y"), py::arg("z"), py::arg("is_static") = true)
        .def("spawn_template_sequentially", [](ObjectTemplateManager& tm, const std::string& name, float x, float y, float z, bool isStatic) {
            tm.spawnTemplateSequentially(name, glm::vec3(x, y, z), isStatic);
        }, "Spawn a template sequentially over multiple frames",
           py::arg("name"), py::arg("x"), py::arg("y"), py::arg("z"), py::arg("is_static") = true);

    // Expose InputManager
    py::class_<Input::InputManager>(m, "InputManager")
        .def("set_camera_position", [](Input::InputManager& im, float x, float y, float z) {
            im.setCameraPosition(glm::vec3(x, y, z));
        }, "Set camera position")
        .def("get_camera_position", [](Input::InputManager& im) {
            auto pos = im.getCameraPosition();
            return py::make_tuple(pos.x, pos.y, pos.z);
        }, "Get camera position as (x, y, z) tuple")
        .def("set_yaw_pitch", &Input::InputManager::setYawPitch, "Set camera orientation (yaw, pitch)");

    // Expose AudioChannel
    py::enum_<Core::AudioChannel>(m, "AudioChannel")
        .value("Master", Core::AudioChannel::Master)
        .value("SFX", Core::AudioChannel::SFX)
        .value("Music", Core::AudioChannel::Music)
        .value("Voice", Core::AudioChannel::Voice)
        .export_values();

    // Expose AudioSystem
    py::class_<Core::AudioSystem>(m, "AudioSystem")
        .def("play_sound", [](Core::AudioSystem& as, const std::string& path, Core::AudioChannel channel, float volume) {
            as.playSound(path, channel, volume);
        }, "Play a 2D sound", py::arg("path"), py::arg("channel") = Core::AudioChannel::SFX, py::arg("volume") = 1.0f)
        .def("play_sound_3d", [](Core::AudioSystem& as, const std::string& path, float x, float y, float z, Core::AudioChannel channel, float volume, float vx, float vy, float vz) {
            as.playSound3D(path, glm::vec3(x, y, z), channel, volume, glm::vec3(vx, vy, vz));
        }, "Play a 3D sound", py::arg("path"), py::arg("x"), py::arg("y"), py::arg("z"), py::arg("channel") = Core::AudioChannel::SFX, py::arg("volume") = 1.0f, py::arg("vx") = 0.0f, py::arg("vy") = 0.0f, py::arg("vz") = 0.0f)
        .def("set_channel_volume", &Core::AudioSystem::setChannelVolume, "Set volume for a channel", py::arg("channel"), py::arg("volume"))
        .def("preload_sound", &Core::AudioSystem::preloadSound, "Preload a sound into memory", py::arg("path"));

    // Expose Application
    py::class_<Application>(m, "Application")
        .def("get_voxel_interaction_system", &Application::getVoxelInteractionSystem, py::return_value_policy::reference)
        .def("get_chunk_manager", &Application::getChunkManager, py::return_value_policy::reference)
        .def("get_object_template_manager", &Application::getObjectTemplateManager, py::return_value_policy::reference)
        .def("get_input_manager", &Application::getInputManager, py::return_value_policy::reference)
        .def("get_audio_system", &Application::getAudioSystem, py::return_value_policy::reference)
        // Character Management
        .def("create_animated_character", [](Application& app, float x, float y, float z, const std::string& animFile) {
            return app.createAnimatedCharacter(glm::vec3(x, y, z), animFile);
        }, py::return_value_policy::reference, "Create an animated character at (x,y,z) with animation file")
        .def("set_control_target", &Application::setControlTarget, "Set the character to control ('spider', 'animated', 'physics')")
        .def("toggle_character_control", &Application::toggleCharacterControl, "Cycle through controllable characters")
        .def("toggle_camera_mode", &Application::toggleCameraMode, "Toggle camera mode (First/Third/Free)")
        .def("derez_character", &Application::derezCharacter,
             "Derez the current character (staggered voxel falling-apart effect)",
             py::arg("duration") = 2.0f)
        // Entity Registry access
        .def("get_entity_registry", &Application::getEntityRegistry, py::return_value_policy::reference)
        // API Server access
        .def("get_api_server", &Application::getAPIServer, py::return_value_policy::reference);

    // Expose EntityRegistry
    py::class_<Core::EntityRegistry>(m, "EntityRegistry")
        .def("register_entity", [](Core::EntityRegistry& reg, Scene::Entity* entity, const std::string& id, const std::string& typeTag) {
            return reg.registerEntity(entity, id, typeTag);
        }, "Register an entity with ID and type", py::arg("entity"), py::arg("id"), py::arg("type_tag") = "")
        .def("unregister_entity", [](Core::EntityRegistry& reg, const std::string& id) {
            return reg.unregisterEntity(id);
        }, "Unregister an entity by ID")
        .def("get_entity", &Core::EntityRegistry::getEntity, py::return_value_policy::reference, "Get entity by ID")
        .def("get_entity_id", &Core::EntityRegistry::getEntityId, "Get the ID of an entity")
        .def("has_entity", &Core::EntityRegistry::hasEntity, "Check if an entity ID exists")
        .def("get_all_ids", &Core::EntityRegistry::getAllIds, "Get all registered entity IDs")
        .def("size", &Core::EntityRegistry::size, "Get number of registered entities");

    // Expose Character Classes (Opaque handles for now)
    py::class_<Scene::Entity>(m, "Entity")
        .def("get_position", [](Scene::Entity& e) {
            auto pos = e.getPosition();
            return py::make_tuple(pos.x, pos.y, pos.z);
        }, "Get entity position as (x, y, z)")
        .def("set_position", [](Scene::Entity& e, float x, float y, float z) {
            e.setPosition(glm::vec3(x, y, z));
        }, "Set entity position");
    py::class_<Scene::AnimatedVoxelCharacter, Scene::Entity>(m, "AnimatedVoxelCharacter")
        .def("play_animation", &Scene::AnimatedVoxelCharacter::playAnimation, "Play an animation by name")
        .def("get_animation_names", &Scene::AnimatedVoxelCharacter::getAnimationNames, "Get list of all available animation names")
        .def("set_animation_mapping", &Scene::AnimatedVoxelCharacter::setAnimationMapping, "Map a state (e.g. 'Walk') to an animation name")
        .def("get_animation_mapping", &Scene::AnimatedVoxelCharacter::getAnimationMapping, "Get the animation name mapped to a state")
        .def("set_animation_rotation_offset", &Scene::AnimatedVoxelCharacter::setAnimationRotationOffset, "Set rotation offset (degrees) for an animation")
        .def("set_animation_position_offset", [](Scene::AnimatedVoxelCharacter& c, const std::string& name, float x, float y, float z) {
            c.setAnimationPositionOffset(name, glm::vec3(x, y, z));
        }, "Set position offset for an animation");

    // Global function to get app
    m.def("get_app", []() { return g_appInstance; }, py::return_value_policy::reference);

    // Expose app instance as 'app' attribute for easier access
    if (g_appInstance) {
        m.attr("app") = py::cast(g_appInstance, py::return_value_policy::reference);
    }

    // Log redirection helper
    m.def("log_to_console", [](const std::string& msg) {
        if (g_appInstance) {
            auto scripting = g_appInstance->getScriptingSystem();
            if (scripting) {
                scripting->appendLog(msg);
            }
        }
    }, "Internal use: log to console window");
}
