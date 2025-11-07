#include "utils/Logger.h"
#include <iostream>
#include <ctime>
#include <thread>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace VulkanCube {
namespace Utils {

// Static instance
Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

// Constructor
Logger::Logger()
    : globalLevel_(LogLevel::Info)
    , consoleOutputEnabled_(true)
    , fileOutputEnabled_(false)
    , logFileName_("phyxel.log")
    , timestampsEnabled_(true)
    , colorsEnabled_(true)
    , moduleNamesEnabled_(true)
    , threadIdsEnabled_(false) {
    
    // Initialize default module levels
    moduleLevels_["Application"] = LogLevel::Info;
    moduleLevels_["Vulkan"] = LogLevel::Info;
    moduleLevels_["Physics"] = LogLevel::Info;
    moduleLevels_["Rendering"] = LogLevel::Info;
    moduleLevels_["Chunk"] = LogLevel::Info;
    moduleLevels_["Performance"] = LogLevel::Info;
    
#ifdef _WIN32
    // Enable ANSI color codes in Windows console
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
#endif
}

// Destructor
Logger::~Logger() {
    if (logFile_.is_open()) {
        logFile_.close();
    }
}

// Configuration methods
void Logger::setGlobalLevel(LogLevel level) {
    getInstance().globalLevel_ = level;
}

void Logger::setModuleLevel(const std::string& module, LogLevel level) {
    std::lock_guard<std::mutex> lock(getInstance().mutex_);
    getInstance().moduleLevels_[module] = level;
}

void Logger::enableModule(const std::string& module, LogLevel level) {
    setModuleLevel(module, level);
}

void Logger::disableModule(const std::string& module) {
    setModuleLevel(module, LogLevel::Off);
}

void Logger::enableAllModules(LogLevel level) {
    std::lock_guard<std::mutex> lock(getInstance().mutex_);
    for (auto& pair : getInstance().moduleLevels_) {
        pair.second = level;
    }
}

void Logger::disableAllModules() {
    std::lock_guard<std::mutex> lock(getInstance().mutex_);
    for (auto& pair : getInstance().moduleLevels_) {
        pair.second = LogLevel::Off;
    }
}

// Output configuration
void Logger::enableConsoleOutput(bool enable) {
    getInstance().consoleOutputEnabled_ = enable;
}

void Logger::enableFileOutput(bool enable, const std::string& filename) {
    auto& instance = getInstance();
    std::lock_guard<std::mutex> lock(instance.mutex_);
    
    instance.fileOutputEnabled_ = enable;
    
    if (enable && !filename.empty()) {
        instance.logFileName_ = filename;
        
        // Close existing file if open
        if (instance.logFile_.is_open()) {
            instance.logFile_.close();
        }
        
        // Open new file
        instance.logFile_.open(filename, std::ios::out | std::ios::app);
        if (!instance.logFile_.is_open()) {
            std::cerr << "Failed to open log file: " << filename << std::endl;
            instance.fileOutputEnabled_ = false;
        }
    }
}

void Logger::setOutputFile(const std::string& filename) {
    enableFileOutput(getInstance().fileOutputEnabled_, filename);
}

// Formatting options
void Logger::enableTimestamps(bool enable) {
    getInstance().timestampsEnabled_ = enable;
}

void Logger::enableColors(bool enable) {
    getInstance().colorsEnabled_ = enable;
}

void Logger::enableModuleNames(bool enable) {
    getInstance().moduleNamesEnabled_ = enable;
}

void Logger::enableThreadIds(bool enable) {
    getInstance().threadIdsEnabled_ = enable;
}

// Configuration file
bool Logger::loadConfig(const std::string& configFile) {
    std::ifstream file(configFile);
    if (!file.is_open()) {
        return false;
    }
    
    auto& instance = getInstance();
    std::lock_guard<std::mutex> lock(instance.mutex_);
    
    std::string line;
    std::string currentSection;
    
    while (std::getline(file, line)) {
        // Remove whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }
        
        // Parse sections [Section]
        if (line[0] == '[' && line[line.length()-1] == ']') {
            currentSection = line.substr(1, line.length()-2);
            continue;
        }
        
        // Parse key=value pairs
        size_t equalPos = line.find('=');
        if (equalPos != std::string::npos) {
            std::string key = line.substr(0, equalPos);
            std::string value = line.substr(equalPos + 1);
            
            // Remove whitespace from key and value
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            
            if (currentSection == "General") {
                if (key == "global_level") {
                    if (value == "TRACE") instance.globalLevel_ = LogLevel::Trace;
                    else if (value == "DEBUG") instance.globalLevel_ = LogLevel::Debug;
                    else if (value == "INFO") instance.globalLevel_ = LogLevel::Info;
                    else if (value == "WARN") instance.globalLevel_ = LogLevel::Warn;
                    else if (value == "ERROR") instance.globalLevel_ = LogLevel::Error;
                    else if (value == "FATAL") instance.globalLevel_ = LogLevel::Fatal;
                    else if (value == "OFF") instance.globalLevel_ = LogLevel::Off;
                } else if (key == "console_output") {
                    instance.consoleOutputEnabled_ = (value == "true" || value == "1");
                } else if (key == "file_output") {
                    bool enableFile = (value == "true" || value == "1");
                    instance.fileOutputEnabled_ = enableFile;
                } else if (key == "log_file") {
                    instance.logFileName_ = value;
                } else if (key == "timestamps") {
                    instance.timestampsEnabled_ = (value == "true" || value == "1");
                } else if (key == "colors") {
                    instance.colorsEnabled_ = (value == "true" || value == "1");
                } else if (key == "module_names") {
                    instance.moduleNamesEnabled_ = (value == "true" || value == "1");
                } else if (key == "thread_ids") {
                    instance.threadIdsEnabled_ = (value == "true" || value == "1");
                }
            } else if (currentSection == "Modules") {
                LogLevel level = LogLevel::Info;
                if (value == "TRACE") level = LogLevel::Trace;
                else if (value == "DEBUG") level = LogLevel::Debug;
                else if (value == "INFO") level = LogLevel::Info;
                else if (value == "WARN") level = LogLevel::Warn;
                else if (value == "ERROR") level = LogLevel::Error;
                else if (value == "FATAL") level = LogLevel::Fatal;
                else if (value == "OFF") level = LogLevel::Off;
                
                instance.moduleLevels_[key] = level;
            }
        }
    }
    
    // Open log file if file output is enabled
    if (instance.fileOutputEnabled_ && !instance.logFileName_.empty()) {
        if (instance.logFile_.is_open()) {
            instance.logFile_.close();
        }
        instance.logFile_.open(instance.logFileName_, std::ios::out | std::ios::app);
        if (!instance.logFile_.is_open()) {
            std::cerr << "Failed to open log file: " << instance.logFileName_ << std::endl;
            instance.fileOutputEnabled_ = false;
        }
    }
    
    return true;
}

void Logger::saveConfig(const std::string& configFile) {
    auto& instance = getInstance();
    std::lock_guard<std::mutex> lock(instance.mutex_);
    
    std::ofstream file(configFile);
    if (!file.is_open()) {
        std::cerr << "Failed to save config file: " << configFile << std::endl;
        return;
    }
    
    file << "# Phyxel Engine Logging Configuration\n";
    file << "# Log Levels: TRACE, DEBUG, INFO, WARN, ERROR, FATAL, OFF\n\n";
    
    file << "[General]\n";
    file << "global_level=" << levelToString(instance.globalLevel_) << "\n";
    file << "console_output=" << (instance.consoleOutputEnabled_ ? "true" : "false") << "\n";
    file << "file_output=" << (instance.fileOutputEnabled_ ? "true" : "false") << "\n";
    file << "log_file=" << instance.logFileName_ << "\n";
    file << "timestamps=" << (instance.timestampsEnabled_ ? "true" : "false") << "\n";
    file << "colors=" << (instance.colorsEnabled_ ? "true" : "false") << "\n";
    file << "module_names=" << (instance.moduleNamesEnabled_ ? "true" : "false") << "\n";
    file << "thread_ids=" << (instance.threadIdsEnabled_ ? "true" : "false") << "\n\n";
    
    file << "[Modules]\n";
    for (const auto& pair : instance.moduleLevels_) {
        file << pair.first << "=" << levelToString(pair.second) << "\n";
    }
    
    file.close();
}

// Logging methods
void Logger::log(LogLevel level, const std::string& module, const std::string& message) {
    getInstance().logImpl(level, module, message);
}

void Logger::trace(const std::string& module, const std::string& message) {
    log(LogLevel::Trace, module, message);
}

void Logger::debug(const std::string& module, const std::string& message) {
    log(LogLevel::Debug, module, message);
}

void Logger::info(const std::string& module, const std::string& message) {
    log(LogLevel::Info, module, message);
}

void Logger::warn(const std::string& module, const std::string& message) {
    log(LogLevel::Warn, module, message);
}

void Logger::error(const std::string& module, const std::string& message) {
    log(LogLevel::Error, module, message);
}

void Logger::fatal(const std::string& module, const std::string& message) {
    log(LogLevel::Fatal, module, message);
}

// Query methods
bool Logger::isLevelEnabled(LogLevel level, const std::string& module) {
    return getInstance().shouldLog(level, module);
}

LogLevel Logger::getGlobalLevel() {
    return getInstance().globalLevel_;
}

LogLevel Logger::getModuleLevel(const std::string& module) {
    std::lock_guard<std::mutex> lock(getInstance().mutex_);
    auto it = getInstance().moduleLevels_.find(module);
    if (it != getInstance().moduleLevels_.end()) {
        return it->second;
    }
    return getInstance().globalLevel_;
}

// Utility
std::string Logger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
        case LogLevel::Off:   return "OFF";
        default: return "UNKNOWN";
    }
}

std::string Logger::levelToColorCode(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "\033[37m";      // White
        case LogLevel::Debug: return "\033[36m";      // Cyan
        case LogLevel::Info:  return "\033[32m";      // Green
        case LogLevel::Warn:  return "\033[33m";      // Yellow
        case LogLevel::Error: return "\033[31m";      // Red
        case LogLevel::Fatal: return "\033[1;31m";    // Bold Red
        default: return "\033[0m";                     // Reset
    }
}

void Logger::flush() {
    std::lock_guard<std::mutex> lock(getInstance().mutex_);
    if (getInstance().consoleOutputEnabled_) {
        std::cout.flush();
    }
    if (getInstance().fileOutputEnabled_ && getInstance().logFile_.is_open()) {
        getInstance().logFile_.flush();
    }
}

// Internal implementation
void Logger::logImpl(LogLevel level, const std::string& module, const std::string& message) {
    if (!shouldLog(level, module)) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::string formattedMessage = formatMessage(level, module, message);
    
    // Console output
    if (consoleOutputEnabled_) {
        if (colorsEnabled_) {
            std::cout << levelToColorCode(level) << formattedMessage << "\033[0m" << std::endl;
        } else {
            std::cout << formattedMessage << std::endl;
        }
    }
    
    // File output (no colors)
    if (fileOutputEnabled_ && logFile_.is_open()) {
        logFile_ << formattedMessage << std::endl;
    }
}

bool Logger::shouldLog(LogLevel level, const std::string& module) const {
    // Check if logging is completely disabled
    if (globalLevel_ == LogLevel::Off) {
        return false;
    }
    
    // Check module-specific level
    auto it = moduleLevels_.find(module);
    if (it != moduleLevels_.end()) {
        if (it->second == LogLevel::Off) {
            return false;
        }
        return level >= it->second;
    }
    
    // Fall back to global level
    return level >= globalLevel_;
}

std::string Logger::formatMessage(LogLevel level, const std::string& module, const std::string& message) const {
    std::ostringstream oss;
    
    // Timestamp
    if (timestampsEnabled_) {
        oss << "[" << getCurrentTimestamp() << "] ";
    }
    
    // Level
    oss << "[" << levelToString(level) << "] ";
    
    // Module name
    if (moduleNamesEnabled_) {
        oss << "[" << module << "] ";
    }
    
    // Thread ID
    if (threadIdsEnabled_) {
        oss << "[T:" << std::this_thread::get_id() << "] ";
    }
    
    // Message
    oss << message;
    
    return oss.str();
}

std::string Logger::getCurrentTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    
    std::ostringstream oss;
    oss << std::put_time(&tm, "%H:%M:%S") << "." << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

} // namespace Utils
} // namespace VulkanCube
