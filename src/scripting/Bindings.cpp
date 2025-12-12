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
#include "scene/VoxelInteractionSystem.h"

namespace py = pybind11;

namespace VulkanCube {

// Global pointer to application instance
static Application* g_appInstance = nullptr;

// Function to set the app instance (called from C++)
void setScriptingAppInstance(Application* app) {
    g_appInstance = app;
}

} // namespace VulkanCube

using namespace VulkanCube;

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
        .def("remove_cube", [](ChunkManager& cm, int x, int y, int z) {
            return cm.removeCube(glm::ivec3(x, y, z));
        }, "Remove a cube at the specified world coordinates");

    // Expose Application
    py::class_<Application>(m, "Application")
        .def("get_voxel_interaction_system", &Application::getVoxelInteractionSystem, py::return_value_policy::reference)
        .def("get_chunk_manager", &Application::getChunkManager, py::return_value_policy::reference);

    // Global function to get app
    m.def("get_app", []() { return g_appInstance; }, py::return_value_policy::reference);
}
