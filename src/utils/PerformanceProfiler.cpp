#include "utils/PerformanceProfiler.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <numeric>

namespace VulkanCube {

PerformanceProfiler::PerformanceProfiler() {
    frameTimings.reserve(MAX_FRAME_SAMPLES);
}

void PerformanceProfiler::startFrame() {
    frameStartTime = std::chrono::high_resolution_clock::now();
    bandwidthTracker.startFrame();
    
    // Reset current frame timing
    currentFrameTiming = FrameTiming{};
    currentDetailedTiming = DetailedFrameTiming{};
}

void PerformanceProfiler::endFrame() {
    auto frameEndTime = std::chrono::high_resolution_clock::now();
    double frameTimeMs = std::chrono::duration<double, std::milli>(frameEndTime - frameStartTime).count();
    
    // Update frame timings
    frameTimings.push_back(frameTimeMs);
    if (frameTimings.size() > MAX_FRAME_SAMPLES) {
        frameTimings.erase(frameTimings.begin());
    }
    
    // Update current frame stats
    currentFrameTiming.cpuFrameTime = frameTimeMs;
    currentDetailedTiming.totalFrameTime = frameTimeMs;
    
    // Calculate memory bandwidth
    bandwidthTracker.endFrame();
    currentFrameTiming.memoryBandwidthMBps = bandwidthTracker.averageBandwidthMBps;
    
    updateFrameTimings();
}

void PerformanceProfiler::startTimer(const std::string& name) {
    auto& timer = timers[name];
    if (!timer.isRunning) {
        timer.startTime = std::chrono::high_resolution_clock::now();
        timer.isRunning = true;
    }
}

void PerformanceProfiler::endTimer(const std::string& name) {
    auto it = timers.find(name);
    if (it != timers.end() && it->second.isRunning) {
        auto endTime = std::chrono::high_resolution_clock::now();
        double duration = std::chrono::duration<double, std::milli>(endTime - it->second.startTime).count();
        it->second.accumulatedTime += duration;
        it->second.isRunning = false;
        
        // Update specific timing categories
        if (name == "frustum_culling") {
            currentDetailedTiming.frustumCullingTime += duration;
        } else if (name == "occlusion_culling") {
            currentDetailedTiming.occlusionCullingTime += duration;
        } else if (name == "face_culling") {
            currentDetailedTiming.faceCullingTime += duration;
        } else if (name == "memory_transfer") {
            currentDetailedTiming.memoryTransferTime += duration;
        } else if (name == "buffer_update") {
            currentDetailedTiming.bufferUpdateTime += duration;
        }
    }
}

double PerformanceProfiler::getTimerDuration(const std::string& name) const {
    auto it = timers.find(name);
    return it != timers.end() ? it->second.accumulatedTime : 0.0;
}

void PerformanceProfiler::recordFrustumCulling(int totalObjects, int culled, double timeMs) {
    frustumCullingStats.totalObjectsProcessed += totalObjects;
    frustumCullingStats.totalCulled += culled;
    frustumCullingStats.totalTime += timeMs;
    frustumCullingStats.frameCount++;
    
    currentFrameTiming.frustumCulledInstances = culled;
    currentFrameTiming.culledInstances = culled; // Update total culled for UI display
    currentDetailedTiming.frustumCullingTime += timeMs;
}

void PerformanceProfiler::recordOcclusionCulling(int totalObjects, int culled, double timeMs) {
    occlusionCullingStats.totalObjectsProcessed += totalObjects;
    occlusionCullingStats.totalCulled += culled;
    occlusionCullingStats.totalTime += timeMs;
    occlusionCullingStats.frameCount++;
    
    currentFrameTiming.occlusionCulledInstances = culled;
    currentDetailedTiming.occlusionCullingTime += timeMs;
}

void PerformanceProfiler::recordFaceCulling(int totalFaces, int culled, double timeMs) {
    faceCullingStats.totalObjectsProcessed += totalFaces;
    faceCullingStats.totalCulled += culled;
    faceCullingStats.totalTime += timeMs;
    faceCullingStats.frameCount++;
    
    currentFrameTiming.faceCulledFaces = culled;
    currentDetailedTiming.faceCullingTime += timeMs;
}

void PerformanceProfiler::recordMemoryTransfer(size_t bytes, const std::string& bufferType) {
    bandwidthTracker.recordTransfer(bytes);
    memoryStats.totalBytesTransferred += bytes;
    memoryStats.transfersByType[bufferType] += bytes;
    
    // Update current frame buffer sizes
    if (bufferType == "vertex") {
        currentFrameTiming.vertexBufferBytes += bytes;
    } else if (bufferType == "index") {
        currentFrameTiming.indexBufferBytes += bytes;
    } else if (bufferType == "instance") {
        currentFrameTiming.instanceBufferBytes += bytes;
    } else if (bufferType == "uniform") {
        currentFrameTiming.uniformBufferBytes += bytes;
    }
}

void PerformanceProfiler::recordBufferUpdate(const std::string& bufferType, size_t oldSize, size_t newSize) {
    memoryStats.bufferSizes[bufferType] = newSize;
    if (newSize > oldSize) {
        recordMemoryTransfer(newSize - oldSize, bufferType);
    }
}

void PerformanceProfiler::recordGPUTiming(const std::string& operation, double timeMs) {
    gpuTimings[operation] = timeMs;
    currentFrameTiming.gpuFrameTime += timeMs;
}

FrameTiming PerformanceProfiler::getCurrentFrameStats() const {
    return currentFrameTiming;
}

DetailedFrameTiming PerformanceProfiler::getCurrentDetailedStats() const {
    return currentDetailedTiming;
}

void PerformanceProfiler::printFrameReport() const {
    double avgFPS = getAverageFPS();
    double avgFrameTime = getAverageFrameTime();
    
    std::cout << "\n=== FRAME PERFORMANCE REPORT ===\n";
    std::cout << "Average FPS: " << std::fixed << std::setprecision(1) << avgFPS << "\n";
    std::cout << "Average Frame Time: " << std::fixed << std::setprecision(2) << avgFrameTime << "ms\n";
    std::cout << "Current Frame Time: " << std::fixed << std::setprecision(2) << currentFrameTiming.cpuFrameTime << "ms\n";
    std::cout << "Memory Bandwidth: " << std::fixed << std::setprecision(2) << currentFrameTiming.memoryBandwidthMBps << " MB/s\n";
    std::cout << "=================================\n";
}

void PerformanceProfiler::printDetailedReport() const {
    std::cout << "\n=== DETAILED TIMING BREAKDOWN ===\n";
    std::cout << "Total Frame Time:    " << std::fixed << std::setprecision(2) << currentDetailedTiming.totalFrameTime << "ms\n";
    std::cout << "  Frustum Culling:   " << std::fixed << std::setprecision(2) << currentDetailedTiming.frustumCullingTime << "ms\n";
    std::cout << "  Occlusion Culling: " << std::fixed << std::setprecision(2) << currentDetailedTiming.occlusionCullingTime << "ms\n";
    std::cout << "  Face Culling:      " << std::fixed << std::setprecision(2) << currentDetailedTiming.faceCullingTime << "ms\n";
    std::cout << "  Memory Transfer:   " << std::fixed << std::setprecision(2) << currentDetailedTiming.memoryTransferTime << "ms\n";
    std::cout << "  Buffer Updates:    " << std::fixed << std::setprecision(2) << currentDetailedTiming.bufferUpdateTime << "ms\n";
    std::cout << "  Command Record:    " << std::fixed << std::setprecision(2) << currentDetailedTiming.commandRecordTime << "ms\n";
    std::cout << "  GPU Submit:        " << std::fixed << std::setprecision(2) << currentDetailedTiming.gpuSubmitTime << "ms\n";
    std::cout << "==================================\n";
}

void PerformanceProfiler::printMemoryReport() const {
    double totalMB = memoryStats.totalBytesTransferred / (1024.0 * 1024.0);
    double avgPerFrame = memoryStats.getAverageTransferPerFrame() / (1024.0 * 1024.0);
    
    std::cout << "\n=== MEMORY BANDWIDTH REPORT ===\n";
    std::cout << "Total Transferred: " << std::fixed << std::setprecision(2) << totalMB << " MB\n";
    std::cout << "Avg Per Frame: " << std::fixed << std::setprecision(2) << avgPerFrame << " MB\n";
    std::cout << "Current Bandwidth: " << std::fixed << std::setprecision(2) << bandwidthTracker.averageBandwidthMBps << " MB/s\n";
    
    std::cout << "\nBuffer Breakdown:\n";
    for (const auto& [type, bytes] : memoryStats.transfersByType) {
        double mb = bytes / (1024.0 * 1024.0);
        std::cout << "  " << type << ": " << std::fixed << std::setprecision(2) << mb << " MB\n";
    }
    std::cout << "===============================\n";
}

void PerformanceProfiler::printCullingReport() const {
    std::cout << "\n=== CULLING EFFICIENCY REPORT ===\n";
    
    if (frustumCullingStats.frameCount > 0) {
        std::cout << "Frustum Culling:\n";
        std::cout << "  Efficiency: " << std::fixed << std::setprecision(1) << frustumCullingStats.getEfficiency() << "%\n";
        std::cout << "  Avg Time: " << std::fixed << std::setprecision(3) << frustumCullingStats.getAverageTime() << "ms\n";
    }
    
    if (occlusionCullingStats.frameCount > 0) {
        std::cout << "Occlusion Culling:\n";
        std::cout << "  Efficiency: " << std::fixed << std::setprecision(1) << occlusionCullingStats.getEfficiency() << "%\n";
        std::cout << "  Avg Time: " << std::fixed << std::setprecision(3) << occlusionCullingStats.getAverageTime() << "ms\n";
    }
    
    if (faceCullingStats.frameCount > 0) {
        std::cout << "Face Culling:\n";
        std::cout << "  Efficiency: " << std::fixed << std::setprecision(1) << faceCullingStats.getEfficiency() << "%\n";
        std::cout << "  Avg Time: " << std::fixed << std::setprecision(3) << faceCullingStats.getAverageTime() << "ms\n";
    }
    
    std::cout << "Total Culling Efficiency: " << std::fixed << std::setprecision(1) << getCullingEfficiency() << "%\n";
    std::cout << "=================================\n";
}

double PerformanceProfiler::getAverageFPS(int frames) const {
    if (frameTimings.empty()) return 0.0;
    
    int samplesToUse = std::min(frames, static_cast<int>(frameTimings.size()));
    double totalTime = 0.0;
    
    for (int i = frameTimings.size() - samplesToUse; i < frameTimings.size(); ++i) {
        totalTime += frameTimings[i];
    }
    
    double avgFrameTimeMs = totalTime / samplesToUse;
    return avgFrameTimeMs > 0.0 ? 1000.0 / avgFrameTimeMs : 0.0;
}

double PerformanceProfiler::getAverageFrameTime(int frames) const {
    if (frameTimings.empty()) return 0.0;
    
    int samplesToUse = std::min(frames, static_cast<int>(frameTimings.size()));
    double totalTime = 0.0;
    
    for (int i = frameTimings.size() - samplesToUse; i < frameTimings.size(); ++i) {
        totalTime += frameTimings[i];
    }
    
    return totalTime / samplesToUse;
}

double PerformanceProfiler::getCullingEfficiency() const {
    int totalProcessed = frustumCullingStats.totalObjectsProcessed + 
                        occlusionCullingStats.totalObjectsProcessed + 
                        faceCullingStats.totalObjectsProcessed;
    
    int totalCulled = frustumCullingStats.totalCulled + 
                     occlusionCullingStats.totalCulled + 
                     faceCullingStats.totalCulled;
    
    return totalProcessed > 0 ? (double)totalCulled / totalProcessed * 100.0 : 0.0;
}

void PerformanceProfiler::resetStats() {
    frameTimings.clear();
    timers.clear();
    
    frustumCullingStats = CullingStats{};
    occlusionCullingStats = CullingStats{};
    faceCullingStats = CullingStats{};
    
    memoryStats = MemoryStats{};
    bandwidthTracker = MemoryBandwidthTracker{};
    
    currentFrameTiming = FrameTiming{};
    currentDetailedTiming = DetailedFrameTiming{};
    
    gpuTimings.clear();
}

void PerformanceProfiler::updateFrameTimings() {
    memoryStats.frameCount++;
}

} // namespace VulkanCube
