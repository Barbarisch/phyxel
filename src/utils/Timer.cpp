#include "utils/Timer.h"
#include <iostream>
#include <GLFW/glfw3.h>
#include <numeric>

namespace VulkanCube {

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
    
    std::cout << "\n=== FRAME PROFILING ===\n";
    std::cout << "FPS: " << m_fps << "\n";
    std::cout << "Avg CPU Frame Time: " << avgCpuTime << "ms\n";
    std::cout << "Avg Vertices/Frame: " << avgVertices << "\n";
    std::cout << "Avg Visible Cubes: " << avgVisible << "\n";
    std::cout << "Avg Culled Cubes: " << avgCulled << "\n";
    std::cout << "Culling Efficiency: " << (100.0 * avgCulled / (avgVisible + avgCulled)) << "%\n";
    std::cout << "Memory Usage: Instance Buffer = " << (avgVisible * sizeof(InstanceData)) << " bytes\n";
    std::cout << "======================\n";
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
    
    std::cout << "\n=== DETAILED FRAME TIMING ===\n";
    std::cout << "Total Frame Time: " << avg.totalFrameTime << "ms\n";
    std::cout << "  Mouse Picking: " << avg.mousePickTime << "ms\n";
    std::cout << "  UBO Fill: " << avg.uboFillTime << "ms\n";
    std::cout << "  Instance Update: " << avg.instanceUpdateTime << "ms\n";
    std::cout << "  Draw Cmd Update: " << avg.drawCmdUpdateTime << "ms\n";
    std::cout << "  Uniform Upload: " << avg.uniformUploadTime << "ms\n";
    std::cout << "  Command Record: " << avg.commandRecordTime << "ms\n";
    std::cout << "  GPU Submit: " << avg.gpuSubmitTime << "ms\n";
    std::cout << "  Present: " << avg.presentTime << "ms\n";
    std::cout << "=============================\n";
}

} // namespace VulkanCube
