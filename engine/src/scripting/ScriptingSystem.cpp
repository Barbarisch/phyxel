#include "scripting/ScriptingSystem.h"
#include "core/AssetManager.h"
#include "utils/Logger.h"
#include <pybind11/pybind11.h>
#include <algorithm>

namespace Phyxel {

// Default null — game-side Bindings.cpp registers the real one at static init
void (*ScriptingSystem::s_setAppInstance)(Application*) = nullptr;

void ScriptingSystem::registerAppInstanceSetter(void(*setter)(Application*)) {
    s_setAppInstance = setter;
}

ScriptingSystem::ScriptingSystem(Application* app) : m_app(app) {
}

ScriptingSystem::~ScriptingSystem() {
    shutdown();
}

void ScriptingSystem::init() {
    if (s_setAppInstance) {
        s_setAppInstance(m_app);
    }
    try {
        // Initialize the interpreter
        // This also initializes the GIL
        m_interpreter = std::make_unique<py::scoped_interpreter>();
        
        Utils::Logger::getInstance().log(Utils::LogLevel::Info, "ScriptingSystem", "Python interpreter initialized");
        
        // Initialize persistent globals
        m_globals = py::module::import("__main__").attr("__dict__");

        // Import the embedded module
        try {
            py::module::import("phyxel");
        } catch (const std::exception& e) {
             Utils::Logger::getInstance().log(Utils::LogLevel::Error, "ScriptingSystem", std::string("Failed to import phyxel module: ") + e.what());
        }
        
        // Setup output redirection
        try {
            // Define a Python class to catch stdout/stderr
            py::exec(R"(
import sys
import phyxel

class LogCatcher:
    def write(self, message):
        if message.strip():  # Ignore empty newlines
            phyxel.log_to_console(message)
    def flush(self):
        pass

sys.stdout = LogCatcher()
sys.stderr = LogCatcher()
            )", m_globals);
        } catch (const std::exception& e) {
            Utils::Logger::getInstance().log(Utils::LogLevel::Error, "ScriptingSystem", std::string("Failed to setup output redirection: ") + e.what());
        }

        runCommand("import phyxel");
        runCommand("print('Hello from Python! Phyxel module loaded.')");
        runCommand("phyxel.Logger.info('ScriptingSystem', 'Logging from Python works!')");
        
        // Initialize completer with persistent globals
        try {
            py::module rlcompleter = py::module::import("rlcompleter");
            m_completer = rlcompleter.attr("Completer")(m_globals);
        } catch (const std::exception& e) {
            Utils::Logger::getInstance().log(Utils::LogLevel::Warn, "ScriptingSystem", std::string("Failed to init completer: ") + e.what());
        }

        // Add scripts directory to path
        try {
            py::module::import("sys").attr("path").attr("append")(Core::AssetManager::instance().scriptsDir());
            
            // Try to load startup script
            py::eval_file(Core::AssetManager::instance().resolveScript("startup.py"), m_globals);
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
    // Release python objects before interpreter shutdown
    m_globals = py::object();
    m_completer = py::object();

    if (m_interpreter) {
        m_interpreter.reset();
        Utils::Logger::getInstance().log(Utils::LogLevel::Info, "ScriptingSystem", "Python interpreter shut down");
    }
}

void ScriptingSystem::runCommand(const std::string& cmd) {
    try {
        // Use persistent globals
        py::exec(cmd.c_str(), m_globals);
        appendLog(">>> " + cmd);
    } catch (const std::exception& e) {
        std::string err = std::string("Python Error: ") + e.what();
        Utils::Logger::getInstance().log(Utils::LogLevel::Error, "ScriptingSystem", err);
        appendLog(err);
    }
}

void ScriptingSystem::reloadScript(const std::string& filename) {
    try {
        // Check if file exists in scripts folder
        std::string path = Core::AssetManager::instance().resolveScript(filename);
        
        // If filename already contains path separators, use it as is
        if (filename.find("/") != std::string::npos || filename.find("\\") != std::string::npos) {
            path = filename;
        }
        
        Utils::Logger::getInstance().log(Utils::LogLevel::Info, "ScriptingSystem", "Reloading script: " + path);
        py::eval_file(path, m_globals);
        Utils::Logger::getInstance().log(Utils::LogLevel::Info, "ScriptingSystem", "Script executed successfully");
        appendLog("Reloaded script: " + path);
    } catch (const std::exception& e) {
        std::string err = std::string("Script Error: ") + e.what();
        Utils::Logger::getInstance().log(Utils::LogLevel::Error, "ScriptingSystem", err);
        appendLog(err);
    }
}

void ScriptingSystem::appendLog(const std::string& msg) {
    m_logBuffer.push_back(msg);
    // Keep buffer size reasonable
    if (m_logBuffer.size() > 1000) {
        m_logBuffer.erase(m_logBuffer.begin(), m_logBuffer.begin() + 100);
    }
}

std::vector<std::string> ScriptingSystem::getCompletions(const std::string& prefix) {
    std::vector<std::string> results;
    Utils::Logger::getInstance().log(Utils::LogLevel::Debug, "ScriptingSystem", "Completing prefix: " + prefix);

    try {
        std::string expr;
        std::string attr_prefix;
        size_t last_dot = prefix.find_last_of('.');
        
        if (last_dot != std::string::npos) {
            expr = prefix.substr(0, last_dot);
            attr_prefix = prefix.substr(last_dot + 1);
        } else {
            attr_prefix = prefix;
        }

        if (expr.empty()) {
            // Global scope completion
            if (py::isinstance<py::dict>(m_globals)) {
                for (auto item : m_globals.cast<py::dict>()) {
                    std::string key = py::str(item.first);
                    if (key.find(attr_prefix) == 0) {
                        std::string completion = key;
                        // Check if callable
                        try {
                            py::object val = py::reinterpret_borrow<py::object>(item.second);
                            if (py::isinstance<py::function>(val) || py::hasattr(val, "__call__")) {
                                completion += "(";
                            }
                        } catch (...) {}
                        results.push_back(completion);
                    }
                }
            }
            
            // Also check builtins
            try {
                py::object builtins = py::module::import("builtins");
                py::object dir_func = builtins.attr("dir");
                py::list attrs = dir_func(); // dir() without args returns locals, but we want builtins? No.
                // dir(builtins) gives builtin names
                attrs = dir_func(builtins);
                for (auto item : attrs) {
                    std::string key = py::str(item);
                    if (key.find(attr_prefix) == 0) {
                         // We skip callable check for builtins for performance/simplicity or add it
                         results.push_back(key);
                    }
                }
            } catch (...) {}

        } else {
            // Attribute completion
            // Evaluate the expression before the dot
            py::object obj = py::eval(expr.c_str(), m_globals);
            
            // Get attributes using dir()
            py::object dir_func = py::module::import("builtins").attr("dir");
            py::list attrs = dir_func(obj);
            
            for (auto item : attrs) {
                std::string attr = py::str(item);
                if (attr.find(attr_prefix) == 0) {
                    std::string completion = expr + "." + attr;
                    // Check if callable
                    try {
                        py::object val = obj.attr(attr.c_str());
                        if (py::isinstance<py::function>(val) || py::hasattr(val, "__call__")) {
                            completion += "(";
                        }
                    } catch (...) {}
                    results.push_back(completion);
                }
            }
        }
        
        // Sort results
        std::sort(results.begin(), results.end());
        
        // Log results count
        Utils::Logger::getInstance().log(Utils::LogLevel::Debug, "ScriptingSystem", "Found " + std::to_string(results.size()) + " completions");

    } catch (const std::exception& e) {
        // Eval failed or other error
        Utils::Logger::getInstance().log(Utils::LogLevel::Debug, "ScriptingSystem", std::string("Completion error: ") + e.what());
    }
    
    return results;
}

}
