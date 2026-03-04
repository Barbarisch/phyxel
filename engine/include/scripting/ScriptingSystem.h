#pragma once
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

#include <pybind11/embed.h>
#include <memory>
#include <string>

namespace py = pybind11;

namespace VulkanCube {

class Application;

class ScriptingSystem {
public:
    ScriptingSystem(Application* app);
    ~ScriptingSystem();

    void init();
    void update(float deltaTime);
    void shutdown();

    // Execute a simple string command
    void runCommand(const std::string& cmd);

    // Reload/Run a script file
    void reloadScript(const std::string& filename);

    // Get tab completions for a given prefix
    std::vector<std::string> getCompletions(const std::string& prefix);

    // Log access
    const std::vector<std::string>& getLogBuffer() const { return m_logBuffer; }
    void appendLog(const std::string& msg);
    void clearLog() { m_logBuffer.clear(); }

private:
    Application* m_app;
    std::unique_ptr<py::scoped_interpreter> m_interpreter;
    py::object m_completer;
    py::object m_globals; // Persistent global namespace
    std::vector<std::string> m_logBuffer;
};

}
