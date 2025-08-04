#pragma once

#include "core/Types.h"
#include <chrono>
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>

namespace VulkanCube {

class PerformanceProfiler {
public:
    PerformanceProfiler();
    ~PerformanceProfiler() = default;

    // Frame lifecycle
    void startFrame();
    void endFrame();

    // Timing utilities
    void startTimer(const std::string& name);
    void endTimer(const std::string& name);
    double getTimerDuration(const std::string& name) const;

    // Culling metrics
    void recordFrustumCulling(int totalObjects, int culled, double timeMs);
    void recordOcclusionCulling(int totalObjects, int culled, double timeMs);
    void recordFaceCulling(int totalFaces, int culled, double timeMs);

    // Memory bandwidth tracking
    void recordMemoryTransfer(size_t bytes, const std::string& bufferType = "generic");
    void recordBufferUpdate(const std::string& bufferType, size_t oldSize, size_t newSize);

    // GPU timing (placeholder for future Vulkan timestamp queries)
    void recordGPUTiming(const std::string& operation, double timeMs);

    // Statistics and reporting
    FrameTiming getCurrentFrameStats() const;
    DetailedFrameTiming getCurrentDetailedStats() const;
    
    void printFrameReport() const;
    void printDetailedReport() const;
    void printMemoryReport() const;
    void printCullingReport() const;

    // Performance analysis
    double getAverageFPS(int frames = 60) const;
    double getAverageFrameTime(int frames = 60) const;
    double getCullingEfficiency() const;
    
    // Reset statistics
    void resetStats();

private:
    struct Timer {
        std::chrono::high_resolution_clock::time_point startTime;
        double accumulatedTime = 0.0;
        bool isRunning = false;
    };

    struct CullingStats {
        int totalObjectsProcessed = 0;
        int totalCulled = 0;
        double totalTime = 0.0;
        int frameCount = 0;

        double getEfficiency() const {
            return totalObjectsProcessed > 0 ? (double)totalCulled / totalObjectsProcessed * 100.0 : 0.0;
        }

        double getAverageTime() const {
            return frameCount > 0 ? totalTime / frameCount : 0.0;
        }
    };

    struct MemoryStats {
        size_t totalBytesTransferred = 0;
        size_t frameCount = 0;
        std::unordered_map<std::string, size_t> bufferSizes;
        std::unordered_map<std::string, size_t> transfersByType;

        double getAverageTransferPerFrame() const {
            return frameCount > 0 ? (double)totalBytesTransferred / frameCount : 0.0;
        }
    };

    // Frame timing
    std::chrono::high_resolution_clock::time_point frameStartTime;
    std::vector<double> frameTimings;
    static constexpr size_t MAX_FRAME_SAMPLES = 120; // 2 seconds at 60fps

    // Named timers
    std::unordered_map<std::string, Timer> timers;

    // Culling statistics
    CullingStats frustumCullingStats;
    CullingStats occlusionCullingStats;
    CullingStats faceCullingStats;

    // Memory tracking
    MemoryBandwidthTracker bandwidthTracker;
    MemoryStats memoryStats;

    // Current frame data
    FrameTiming currentFrameTiming;
    DetailedFrameTiming currentDetailedTiming;

    // GPU timing data (for future implementation)
    std::unordered_map<std::string, double> gpuTimings;

    // Helper methods
    void updateFrameTimings();
    void calculateMemoryBandwidth();
};

// RAII timer helper class
class ScopedTimer {
public:
    ScopedTimer(PerformanceProfiler& profiler, const std::string& name)
        : profiler_(profiler), name_(name) {
        profiler_.startTimer(name_);
    }

    ~ScopedTimer() {
        profiler_.endTimer(name_);
    }

private:
    PerformanceProfiler& profiler_;
    std::string name_;
};

// Convenience macro for scoped timing
#define PROFILE_SCOPE(profiler, name) ScopedTimer _timer(profiler, name)

} // namespace VulkanCube
