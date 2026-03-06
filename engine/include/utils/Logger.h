#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <memory>
#include <sstream>
#include <chrono>
#include <iomanip>

namespace Phyxel {
namespace Utils {

/**
 * @brief Logging system with configurable levels and per-module control
 * 
 * Features:
 * - Multiple log levels (TRACE, DEBUG, INFO, WARN, ERROR, FATAL)
 * - Per-module logging control (e.g., enable DEBUG for "Physics" but not "Rendering")
 * - Console and file output
 * - Thread-safe operations
 * - Configurable formatting (timestamps, colors, module names)
 * - Runtime configuration via config file
 * 
 * Usage:
 *   Logger::setGlobalLevel(LogLevel::Debug);
 *   Logger::enableModule("Physics", LogLevel::Trace);
 *   
 *   LOG_INFO("Application", "Starting engine...");
 *   LOG_DEBUG("Physics", "Creating rigid body at position: {}", position);
 *   LOG_ERROR("Vulkan", "Failed to create swapchain!");
 */

enum class LogLevel {
    Trace = 0,   // Most detailed - function calls, data flow
    Debug = 1,   // Debug information - useful during development
    Info = 2,    // General information - important events
    Warn = 3,    // Warnings - potential issues
    Error = 4,   // Errors - recoverable problems
    Fatal = 5,   // Fatal errors - unrecoverable problems
    Off = 6      // Disable all logging
};

class Logger {
public:
    // Singleton access
    static Logger& getInstance();
    
    // Configuration
    static void setGlobalLevel(LogLevel level);
    static void setModuleLevel(const std::string& module, LogLevel level);
    static void enableModule(const std::string& module, LogLevel level = LogLevel::Debug);
    static void disableModule(const std::string& module);
    static void enableAllModules(LogLevel level = LogLevel::Info);
    static void disableAllModules();
    
    // Output configuration
    static void enableConsoleOutput(bool enable = true);
    static void enableFileOutput(bool enable = true, const std::string& filename = "phyxel.log");
    static void setOutputFile(const std::string& filename);
    
    // Formatting options
    static void enableTimestamps(bool enable = true);
    static void enableColors(bool enable = true);
    static void enableModuleNames(bool enable = true);
    static void enableThreadIds(bool enable = true);
    
    // Configuration file
    static bool loadConfig(const std::string& configFile = "logging.ini");
    static void saveConfig(const std::string& configFile = "logging.ini");
    
    // Logging methods
    static void log(LogLevel level, const std::string& module, const std::string& message);
    static void trace(const std::string& module, const std::string& message);
    static void debug(const std::string& module, const std::string& message);
    static void info(const std::string& module, const std::string& message);
    static void warn(const std::string& module, const std::string& message);
    static void error(const std::string& module, const std::string& message);
    static void fatal(const std::string& module, const std::string& message);
    
    // Query methods
    static bool isLevelEnabled(LogLevel level, const std::string& module);
    static LogLevel getGlobalLevel();
    static LogLevel getModuleLevel(const std::string& module);
    
    // Utility
    static std::string levelToString(LogLevel level);
    static std::string levelToColorCode(LogLevel level);
    static void flush(); // Force flush all pending logs
    
private:
    Logger();
    ~Logger();
    
    // Prevent copying
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    
    // Internal implementation
    void logImpl(LogLevel level, const std::string& module, const std::string& message);
    bool shouldLog(LogLevel level, const std::string& module) const;
    std::string formatMessage(LogLevel level, const std::string& module, const std::string& message) const;
    std::string getCurrentTimestamp() const;
    
    // Member variables
    LogLevel globalLevel_;
    std::unordered_map<std::string, LogLevel> moduleLevels_;
    
    bool consoleOutputEnabled_;
    bool fileOutputEnabled_;
    std::ofstream logFile_;
    std::string logFileName_;
    
    bool timestampsEnabled_;
    bool colorsEnabled_;
    bool moduleNamesEnabled_;
    bool threadIdsEnabled_;
    
    mutable std::mutex mutex_; // For thread safety
};

// Convenience macros for logging with automatic line/file information
#define LOG_TRACE(module, ...) \
    LOG_TRACE_FMT(module, __VA_ARGS__)

#define LOG_DEBUG(module, ...) \
    LOG_DEBUG_FMT(module, __VA_ARGS__)

#define LOG_INFO(module, ...) \
    LOG_INFO_FMT(module, __VA_ARGS__)

#define LOG_WARN(module, ...) \
    LOG_WARN_FMT(module, __VA_ARGS__)

#define LOG_ERROR(module, ...) \
    LOG_ERROR_FMT(module, __VA_ARGS__)

#define LOG_FATAL(module, ...) \
    LOG_FATAL_FMT(module, __VA_ARGS__)

// Formatted logging macros (for C++11 compatible string formatting)
#define LOG_TRACE_FMT(module, ...) \
    do { \
        std::ostringstream oss; \
        oss << __VA_ARGS__; \
        Phyxel::Utils::Logger::trace(module, oss.str()); \
    } while(0)

#define LOG_DEBUG_FMT(module, ...) \
    do { \
        std::ostringstream oss; \
        oss << __VA_ARGS__; \
        Phyxel::Utils::Logger::debug(module, oss.str()); \
    } while(0)

#define LOG_INFO_FMT(module, ...) \
    do { \
        std::ostringstream oss; \
        oss << __VA_ARGS__; \
        Phyxel::Utils::Logger::info(module, oss.str()); \
    } while(0)

#define LOG_WARN_FMT(module, ...) \
    do { \
        std::ostringstream oss; \
        oss << __VA_ARGS__; \
        Phyxel::Utils::Logger::warn(module, oss.str()); \
    } while(0)

#define LOG_ERROR_FMT(module, ...) \
    do { \
        std::ostringstream oss; \
        oss << __VA_ARGS__; \
        Phyxel::Utils::Logger::error(module, oss.str()); \
    } while(0)

#define LOG_FATAL_FMT(module, ...) \
    do { \
        std::ostringstream oss; \
        oss << __VA_ARGS__; \
        Phyxel::Utils::Logger::fatal(module, oss.str()); \
    } while(0)

} // namespace Utils
} // namespace Phyxel
