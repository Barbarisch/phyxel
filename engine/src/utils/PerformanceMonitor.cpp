#include "utils/PerformanceMonitor.h"
#include "utils/Logger.h"
#include <iomanip>

namespace Phyxel {
namespace Utils {

PerformanceMonitor::PerformanceMonitor() {
    m_frameTimings.reserve(MAX_TIMING_SAMPLES);
    m_detailedTimings.reserve(MAX_TIMING_SAMPLES);
}

void PerformanceMonitor::updateFrameTiming(double deltaTime) {
    m_currentFrameTiming.cpuFrameTime = deltaTime * 1000.0; // Convert to milliseconds
    m_currentFrameTiming.gpuFrameTime = deltaTime * 1000.0; // Placeholder
}

void PerformanceMonitor::printPerformanceStats(
    Timer* timer,
    const FrameTiming& frameTiming,
    ChunkManager* chunkManager,
    Physics::PhysicsWorld* physicsWorld
) {
    float fps = timer->getFPS();
    auto chunkStats = chunkManager->getPerformanceStats();
    
    LOG_INFO("Performance", "Performance Stats:");
    LOG_INFO_FMT("Performance", "  FPS: " << fps);
    LOG_INFO_FMT("Performance", "  CPU Frame Time: " << frameTiming.cpuFrameTime << "ms");
    LOG_INFO_FMT("Performance", "  GPU Frame Time: " << frameTiming.gpuFrameTime << "ms");
    LOG_INFO_FMT("Performance", "  Vertices: " << frameTiming.vertexCount);
    LOG_INFO_FMT("Performance", "  Draw Calls: " << frameTiming.drawCalls);
    LOG_INFO_FMT("Performance", "  Visible Instances: " << frameTiming.visibleInstances);
    LOG_INFO_FMT("Performance", "  Culled Instances: " << frameTiming.culledInstances);
    LOG_INFO_FMT("Performance", "  Total Chunks: " << chunkManager->chunks.size());
    LOG_INFO_FMT("Performance", "  Total Cubes: " << chunkStats.totalCubes);
    LOG_INFO_FMT("Performance", "  Physics Bodies: " << physicsWorld->getRigidBodyCount());
    LOG_INFO("Performance", "---");
}

void PerformanceMonitor::printProfilingInfo(int fps, Input::InputManager* inputManager) {
    if (m_frameTimings.empty()) return;
    
    // Calculate averages
    double avgCpuTime = 0.0;
    int avgVertices = 0;
    int avgVisible = 0;
    int avgCulled = 0;
    
    for (const auto& timing : m_frameTimings) {
        avgCpuTime += timing.cpuFrameTime;
        avgVertices += timing.vertexCount;
        avgVisible += timing.visibleInstances;
        avgCulled += timing.culledInstances;
    }
    
    int samples = m_frameTimings.size();
    avgCpuTime /= samples;
    avgVertices /= samples;
    avgVisible /= samples;
    avgCulled /= samples;
    
    LOG_INFO("Performance", "\n=== FRAME PROFILING ===");
    LOG_INFO_FMT("Performance", "FPS: " << fps);
    LOG_INFO_FMT("Performance", "Avg CPU Frame Time: " << std::fixed << std::setprecision(2) << avgCpuTime << "ms");
    LOG_INFO_FMT("Performance", "Avg Vertices/Frame: " << avgVertices);
    LOG_INFO_FMT("Performance", "Avg Visible Cubes: " << avgVisible);
    LOG_INFO_FMT("Performance", "Avg Culled Cubes: " << avgCulled);
    
    if (avgVisible + avgCulled > 0) {
        LOG_INFO_FMT("Performance", "Culling Efficiency: " << std::fixed << std::setprecision(1) 
                  << (100.0 * avgCulled / (avgVisible + avgCulled)) << "%");
    }
    
    glm::vec3 cameraPos = inputManager->getCameraPosition();
    LOG_INFO_FMT("Performance", "Camera Position: (" << std::fixed << std::setprecision(1) 
              << cameraPos.x << ", " << cameraPos.y << ", " << cameraPos.z << ")");
    LOG_INFO("Performance", "======================");
}

void PerformanceMonitor::printDetailedTimings() {
    if (m_detailedTimings.empty()) return;
    
    // Calculate averages
    DetailedFrameTiming avg = {};
    for (const auto& t : m_detailedTimings) {
        avg.totalFrameTime += t.totalFrameTime;
        avg.physicsTime += t.physicsTime;
        avg.mousePickTime += t.mousePickTime;
        avg.uboFillTime += t.uboFillTime;
        avg.instanceUpdateTime += t.instanceUpdateTime;
        avg.drawCmdUpdateTime += t.drawCmdUpdateTime;
        avg.uniformUploadTime += t.uniformUploadTime;
        avg.commandRecordTime += t.commandRecordTime;
        avg.gpuSubmitTime += t.gpuSubmitTime;
        avg.presentTime += t.presentTime;
    }
    
    int samples = m_detailedTimings.size();
    avg.totalFrameTime /= samples;
    avg.physicsTime /= samples;
    avg.mousePickTime /= samples;
    avg.uboFillTime /= samples;
    avg.instanceUpdateTime /= samples;
    avg.drawCmdUpdateTime /= samples;
    avg.uniformUploadTime /= samples;
    avg.commandRecordTime /= samples;
    avg.gpuSubmitTime /= samples;
    avg.presentTime /= samples;
    
    LOG_DEBUG("Performance", "\n=== DETAILED FRAME TIMING ===");
    LOG_DEBUG_FMT("Performance", "Total Frame Time:    " << std::fixed << std::setprecision(2) << avg.totalFrameTime << "ms");
    LOG_DEBUG_FMT("Performance", "  Physics:           " << std::fixed << std::setprecision(2) << avg.physicsTime << "ms");
    LOG_DEBUG_FMT("Performance", "  UBO Fill:          " << std::fixed << std::setprecision(2) << avg.uboFillTime << "ms");
    LOG_DEBUG_FMT("Performance", "  Instance Update:   " << std::fixed << std::setprecision(2) << avg.instanceUpdateTime << "ms");
    LOG_DEBUG_FMT("Performance", "  Uniform Upload:    " << std::fixed << std::setprecision(2) << avg.uniformUploadTime << "ms");
    LOG_DEBUG_FMT("Performance", "  Command Record:    " << std::fixed << std::setprecision(2) << avg.commandRecordTime << "ms");
    LOG_DEBUG_FMT("Performance", "  GPU Submit:        " << std::fixed << std::setprecision(2) << avg.gpuSubmitTime << "ms");
    LOG_DEBUG_FMT("Performance", "  Present:           " << std::fixed << std::setprecision(2) << avg.presentTime << "ms");
    LOG_DEBUG_FMT("Performance", "  Mouse Pick:        " << std::fixed << std::setprecision(2) << avg.mousePickTime << "ms");
    
    // Calculate percentage breakdown
    double total = avg.totalFrameTime;
    if (total > 0) {
        LOG_DEBUG("Performance", "\nTiming Breakdown:");
        LOG_DEBUG_FMT("Performance", "  Physics:         " << std::fixed << std::setprecision(1) << (avg.physicsTime / total * 100.0) << "%");
        LOG_DEBUG_FMT("Performance", "  UBO Fill:        " << std::fixed << std::setprecision(1) << (avg.uboFillTime / total * 100.0) << "%");
        LOG_DEBUG_FMT("Performance", "  Instance Update: " << std::fixed << std::setprecision(1) << (avg.instanceUpdateTime / total * 100.0) << "%");
        LOG_DEBUG_FMT("Performance", "  Uniform Upload:  " << std::fixed << std::setprecision(1) << (avg.uniformUploadTime / total * 100.0) << "%");
        LOG_DEBUG_FMT("Performance", "  Command Record:  " << std::fixed << std::setprecision(1) << (avg.commandRecordTime / total * 100.0) << "%");
        LOG_DEBUG_FMT("Performance", "  GPU Submit:      " << std::fixed << std::setprecision(1) << (avg.gpuSubmitTime / total * 100.0) << "%");
        LOG_DEBUG_FMT("Performance", "  Present:         " << std::fixed << std::setprecision(1) << (avg.presentTime / total * 100.0) << "%");
    }
    LOG_DEBUG("Performance", "============================");
}

FrameTiming PerformanceMonitor::profileFrame() {
    FrameTiming timing = m_currentFrameTiming;
    
    // Add to history
    addFrameTiming(timing);
    
    return timing;
}

void PerformanceMonitor::addFrameTiming(const FrameTiming& timing) {
    m_frameTimings.push_back(timing);
    
    // Keep only recent samples
    if (m_frameTimings.size() > MAX_TIMING_SAMPLES) {
        m_frameTimings.erase(m_frameTimings.begin());
    }
}

void PerformanceMonitor::addDetailedTiming(const DetailedFrameTiming& timing) {
    m_detailedTimings.push_back(timing);
    
    // Keep only recent samples
    if (m_detailedTimings.size() > MAX_TIMING_SAMPLES) {
        m_detailedTimings.erase(m_detailedTimings.begin());
    }
}

void PerformanceMonitor::clearTimings() {
    m_frameTimings.clear();
    m_detailedTimings.clear();
}

} // namespace Utils
} // namespace Phyxel
