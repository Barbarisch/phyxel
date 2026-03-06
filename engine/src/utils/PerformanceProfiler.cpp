#include "utils/PerformanceProfiler.h"
#include "utils/Logger.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <sstream>
#include <functional>

namespace Phyxel {

PerformanceProfiler::PerformanceProfiler() {
    frameTimings.reserve(MAX_FRAME_SAMPLES);
}

void PerformanceProfiler::startFrame() {
    frameStartTime = std::chrono::high_resolution_clock::now();
    bandwidthTracker.startFrame();
    
    // Reset current frame timing
    currentFrameTiming = FrameTiming{};
    currentDetailedTiming = DetailedFrameTiming{};

    // Start root scope
    currentRoot = std::make_shared<ProfilerNode>();
    currentRoot->name = "Frame";
    currentRoot->startTime = frameStartTime;
    currentNode = currentRoot.get();
}

void PerformanceProfiler::endFrame() {
    auto frameEndTime = std::chrono::high_resolution_clock::now();
    double frameTimeMs = std::chrono::duration<double, std::milli>(frameEndTime - frameStartTime).count();
    
    // Close root scope
    if (currentRoot) {
        currentRoot->durationMs = frameTimeMs;
        lastFrameRoot = currentRoot;
        currentRoot = nullptr;
        currentNode = nullptr;
    }

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

void PerformanceProfiler::startScope(const std::string& name) {
    if (!currentNode) return;

    // Check if child with this name already exists (for loops calling same function)
    // Actually, for a flame graph, we usually want separate entries if they are sequential,
    // but for a summary tree, we might merge.
    // Let's append for now to see the sequence, or we can merge.
    // Merging is better for "Total time spent in X".
    // But for "Frame Timeline", sequence is better.
    // Let's do sequence (append) for now, as it's simpler and preserves order.
    
    auto newNode = std::make_shared<ProfilerNode>();
    newNode->name = name;
    newNode->startTime = std::chrono::high_resolution_clock::now();
    newNode->parent = currentNode;
    
    currentNode->children.push_back(newNode);
    currentNode = newNode.get();
}

void PerformanceProfiler::endScope() {
    if (!currentNode || !currentNode->parent) return; // Don't pop root

    auto endTime = std::chrono::high_resolution_clock::now();
    currentNode->durationMs = std::chrono::duration<double, std::milli>(endTime - currentNode->startTime).count();
    
    currentNode = currentNode->parent;
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
    
    LOG_INFO("Performance", "\n=== FRAME PERFORMANCE REPORT ===");
    LOG_INFO_FMT("Performance", "Average FPS: " << std::fixed << std::setprecision(1) << avgFPS);
    LOG_INFO_FMT("Performance", "Average Frame Time: " << std::fixed << std::setprecision(2) << avgFrameTime << "ms");
    LOG_INFO_FMT("Performance", "Current Frame Time: " << std::fixed << std::setprecision(2) << currentFrameTiming.cpuFrameTime << "ms");
    LOG_INFO_FMT("Performance", "Memory Bandwidth: " << std::fixed << std::setprecision(2) << currentFrameTiming.memoryBandwidthMBps << " MB/s");
    LOG_INFO("Performance", "=================================");
}

void PerformanceProfiler::printDetailedReport() const {
    LOG_INFO("Performance", "\n=== DETAILED TIMING BREAKDOWN ===");
    LOG_INFO_FMT("Performance", "Total Frame Time:    " << std::fixed << std::setprecision(2) << currentDetailedTiming.totalFrameTime << "ms");
    LOG_INFO_FMT("Performance", "  Frustum Culling:   " << std::fixed << std::setprecision(2) << currentDetailedTiming.frustumCullingTime << "ms");
    LOG_INFO_FMT("Performance", "  Occlusion Culling: " << std::fixed << std::setprecision(2) << currentDetailedTiming.occlusionCullingTime << "ms");
    LOG_INFO_FMT("Performance", "  Face Culling:      " << std::fixed << std::setprecision(2) << currentDetailedTiming.faceCullingTime << "ms");
    LOG_INFO_FMT("Performance", "  Memory Transfer:   " << std::fixed << std::setprecision(2) << currentDetailedTiming.memoryTransferTime << "ms");
    LOG_INFO_FMT("Performance", "  Buffer Updates:    " << std::fixed << std::setprecision(2) << currentDetailedTiming.bufferUpdateTime << "ms");
    LOG_INFO_FMT("Performance", "  Command Record:    " << std::fixed << std::setprecision(2) << currentDetailedTiming.commandRecordTime << "ms");
    LOG_INFO_FMT("Performance", "  GPU Submit:        " << std::fixed << std::setprecision(2) << currentDetailedTiming.gpuSubmitTime << "ms");
    LOG_INFO("Performance", "==================================");
}

void PerformanceProfiler::printMemoryReport() const {
    double totalMB = memoryStats.totalBytesTransferred / (1024.0 * 1024.0);
    double avgPerFrame = memoryStats.getAverageTransferPerFrame() / (1024.0 * 1024.0);
    
    LOG_INFO("Performance", "\n=== MEMORY BANDWIDTH REPORT ===");
    LOG_INFO_FMT("Performance", "Total Transferred: " << std::fixed << std::setprecision(2) << totalMB << " MB");
    LOG_INFO_FMT("Performance", "Avg Per Frame: " << std::fixed << std::setprecision(2) << avgPerFrame << " MB");
    LOG_INFO_FMT("Performance", "Current Bandwidth: " << std::fixed << std::setprecision(2) << bandwidthTracker.averageBandwidthMBps << " MB/s");
    
    LOG_INFO("Performance", "\nBuffer Breakdown:");
    for (const auto& [type, bytes] : memoryStats.transfersByType) {
        double mb = bytes / (1024.0 * 1024.0);
        LOG_INFO_FMT("Performance", "  " << type << ": " << std::fixed << std::setprecision(2) << mb << " MB");
    }
    LOG_INFO("Performance", "===============================");
}

void PerformanceProfiler::printCullingReport() const {
    LOG_INFO("Performance", "\n=== CULLING EFFICIENCY REPORT ===");
    
    if (frustumCullingStats.frameCount > 0) {
        LOG_INFO("Performance", "Frustum Culling:");
        LOG_INFO_FMT("Performance", "  Efficiency: " << std::fixed << std::setprecision(1) << frustumCullingStats.getEfficiency() << "%");
        LOG_INFO_FMT("Performance", "  Avg Time: " << std::fixed << std::setprecision(3) << frustumCullingStats.getAverageTime() << "ms");
    }
    
    if (occlusionCullingStats.frameCount > 0) {
        LOG_INFO("Performance", "Occlusion Culling:");
        LOG_INFO_FMT("Performance", "  Efficiency: " << std::fixed << std::setprecision(1) << occlusionCullingStats.getEfficiency() << "%");
        LOG_INFO_FMT("Performance", "  Avg Time: " << std::fixed << std::setprecision(3) << occlusionCullingStats.getAverageTime() << "ms");
    }
    
    if (faceCullingStats.frameCount > 0) {
        LOG_INFO("Performance", "Face Culling:");
        LOG_INFO_FMT("Performance", "  Efficiency: " << std::fixed << std::setprecision(1) << faceCullingStats.getEfficiency() << "%");
        LOG_INFO_FMT("Performance", "  Avg Time: " << std::fixed << std::setprecision(3) << faceCullingStats.getAverageTime() << "ms");
    }
    
    LOG_INFO_FMT("Performance", "Total Culling Efficiency: " << std::fixed << std::setprecision(1) << getCullingEfficiency() << "%");
    LOG_INFO("Performance", "=================================");
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

    // Reset hierarchy
    currentRoot = nullptr;
    currentNode = nullptr;
    lastFrameRoot = nullptr;
}

void PerformanceProfiler::updateFrameTimings() {
    memoryStats.frameCount++;
}

std::string PerformanceProfiler::dumpFrameToJSON() const {
    if (!lastFrameRoot) return "{}";

    std::stringstream ss;
    
    std::function<void(const ProfilerNode*)> dumpNode = 
        [&](const ProfilerNode* node) {
        ss << "{\"name\": \"" << node->name << "\", ";
        ss << "\"durationMs\": " << std::fixed << std::setprecision(3) << node->durationMs << ", ";
        ss << "\"callCount\": " << node->callCount << ", ";
        ss << "\"children\": [";
        
        for (size_t i = 0; i < node->children.size(); ++i) {
            dumpNode(node->children[i].get());
            if (i < node->children.size() - 1) {
                ss << ", ";
            }
        }
        
        ss << "]}";
    };

    dumpNode(lastFrameRoot.get());
    return ss.str();
}

} // namespace Phyxel
