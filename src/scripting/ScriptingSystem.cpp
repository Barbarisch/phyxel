#include "scripting/ScriptingSystem.h"
#include "Application.h"
#include "utils/Logger.h"
#include <pybind11/pybind11.h>

namespace VulkanCube {

extern void setScriptingAppInstance(Application* app);

ScriptingSystem::ScriptingSystem(Application* app) : m_app(app) {
}

ScriptingSystem::~ScriptingSystem() {
    shutdown();
}

void ScriptingSystem::init() {
    setScriptingAppInstance(m_app);
    try {
        // Initialize the interpreter
        // This also initializes the GIL
        m_interpreter = std::make_unique<py::scoped_interpreter>();
        
        Utils::Logger::getInstance().log(Utils::LogLevel::Info, "ScriptingSystem", "Python interpreter initialized");
        
        // Import the embedded module
        try {
            py::module::import("phyxel");
        } catch (const std::exception& e) {
             Utils::Logger::getInstance().log(Utils::LogLevel::Error, "ScriptingSystem", std::string("Failed to import phyxel module: ") + e.what());
        }
        
        runCommand("import phyxel");
        runCommand("print('Hello from Python! Phyxel module loaded.')");
        runCommand("phyxel.Logger.info('ScriptingSystem', 'Logging from Python works!')");
        
        // Add scripts directory to path
        try {
            py::module::import("sys").attr("path").attr("append")("scripts");
            
            // Try to load startup script
            py::eval_file("scripts/startup.py");
            Utils::Logger::getInstance().log(Utils::LogLevel::Info, "ScriptingSystem", "Loaded scripts/startup.py");
        } catch (const std::exception& e) {
            Utils::Logger::getInstance().log(Utils::LogLevel::Warn, "ScriptingSystem", std::string("Startup script error: ") + e.what());
        }
        
    } catch (const std::exception& e) {
        Utils::Logger::getInstance().log(Utils::LogLevel::Error, "ScriptingSystem", std::string("Failed to initialize Python: ") + e.what());
    }
}

void ScriptingSystem::update(float deltaTime) {
    // Future: Call python update hooks
}

void ScriptingSystem::shutdown() {
    if (m_interpreter) {
        m_interpreter.reset();
        Utils::Logger::getInstance().log(Utils::LogLevel::Info, "ScriptingSystem", "Python interpreter shut down");
    }
}

void ScriptingSystem::runCommand(const std::string& cmd) {
    try {
        py::exec(cmd);
    } catch (const std::exception& e) {
        Utils::Logger::getInstance().log(Utils::LogLevel::Error, "ScriptingSystem", std::string("Python Error: ") + e.what());
    }
}

}
