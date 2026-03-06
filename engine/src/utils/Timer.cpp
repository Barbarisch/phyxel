#include "utils/Timer.h"
#include "utils/Logger.h"
#include <iostream>
#include <GLFW/glfw3.h>
#include <numeric>

namespace Phyxel {

Timer::Timer() {
    m_lastTime = glfwGetTime();
}

void Timer::start() {
    m_startTime = std::chrono::high_resolution_clock::now();
}

void Timer::stop() {
    m_endTime = std::chrono::high_resolution_clock::now();
}

double Timer::getElapsed() const {
    return std::chrono::duration<double, std::milli>(m_endTime - m_startTime).count();
}

void Timer::frameStart() {
    m_frameStartTime = std::chrono::high_resolution_clock::now();
}

void Timer::frameEnd() {
    m_frameEndTime = std::chrono::high_resolution_clock::now();
}

void Timer::update() {
    // Update FPS calculation
    m_frameCount++;
    double currentTime = glfwGetTime();
    if (currentTime - m_lastTime >= 1.0) {
        m_fps = m_frameCount;
        m_frameCount = 0;
        m_lastTime = currentTime;
    }
}

double Timer::getFrameTime() const {
    return std::chrono::duration<double, std::milli>(m_frameEndTime - m_frameStartTime).count();
}

int Timer::getFPS() const {
    return m_fps;
}

void Timer::addFrameTiming(const FrameTiming& timing) {
    m_frameTimings.push_back(timing);
    if (m_frameTimings.size() > 60) {
        m_frameTimings.erase(m_frameTimings.begin());
    }
}

void Timer::addDetailedTiming(const DetailedFrameTiming& timing) {
    m_detailedTimings.push_back(timing);
    if (m_detailedTimings.size() > 60) {
        m_detailedTimings.erase(m_detailedTimings.begin());
    }
}

void Timer::printProfilingInfo() const {
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
    LOG_INFO_FMT("Performance", "FPS: " << m_fps);
    LOG_INFO_FMT("Performance", "Avg CPU Frame Time: " << avgCpuTime << "ms");
    LOG_INFO_FMT("Performance", "Avg Vertices/Frame: " << avgVertices);
    LOG_INFO_FMT("Performance", "Avg Visible Cubes: " << avgVisible);
    LOG_INFO_FMT("Performance", "Avg Culled Cubes: " << avgCulled);
    LOG_INFO_FMT("Performance", "Culling Efficiency: " << (100.0 * avgCulled / (avgVisible + avgCulled)) << "%");
    LOG_INFO_FMT("Performance", "Memory Usage: Instance Buffer = " << (avgVisible * sizeof(InstanceData)) << " bytes");
    LOG_INFO("Performance", "======================");
}

void Timer::printDetailedTimings() const {
    if (m_detailedTimings.empty()) return;
    
    // Calculate averages
    DetailedFrameTiming avg = {};
    for (const auto& t : m_detailedTimings) {
        avg.totalFrameTime += t.totalFrameTime;
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
    avg.mousePickTime /= samples;
    avg.uboFillTime /= samples;
    avg.instanceUpdateTime /= samples;
    avg.drawCmdUpdateTime /= samples;
    avg.uniformUploadTime /= samples;
    avg.commandRecordTime /= samples;
    avg.gpuSubmitTime /= samples;
    avg.presentTime /= samples;
    
    LOG_INFO("Performance", "\n=== DETAILED FRAME TIMING ===");
    LOG_INFO_FMT("Performance", "Total Frame Time: " << avg.totalFrameTime << "ms");
    LOG_INFO_FMT("Performance", "  Mouse Picking: " << avg.mousePickTime << "ms");
    LOG_INFO_FMT("Performance", "  UBO Fill: " << avg.uboFillTime << "ms");
    LOG_INFO_FMT("Performance", "  Instance Update: " << avg.instanceUpdateTime << "ms");
    LOG_INFO_FMT("Performance", "  Draw Cmd Update: " << avg.drawCmdUpdateTime << "ms");
    LOG_INFO_FMT("Performance", "  Uniform Upload: " << avg.uniformUploadTime << "ms");
    LOG_INFO_FMT("Performance", "  Command Record: " << avg.commandRecordTime << "ms");
    LOG_INFO_FMT("Performance", "  GPU Submit: " << avg.gpuSubmitTime << "ms");
    LOG_INFO_FMT("Performance", "  Present: " << avg.presentTime << "ms");
    LOG_INFO("Performance", "=============================");
}

} // namespace Phyxel
