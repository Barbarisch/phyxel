#pragma once

#include "core/Types.h"
#include <chrono>
#include <vector>

namespace Phyxel {

class Timer {
public:
    Timer();
    
    // Basic timing
    void start();
    void stop();
    double getElapsed() const; // in milliseconds
    
    // Frame timing
    void frameStart();
    void frameEnd();
    void update();  // Updates FPS calculations
    double getFrameTime() const;
    int getFPS() const;
    
    // Profiling
    void addFrameTiming(const FrameTiming& timing);
    void addDetailedTiming(const DetailedFrameTiming& timing);
    
    void printProfilingInfo() const;
    void printDetailedTimings() const;
    
private:
    std::chrono::high_resolution_clock::time_point m_startTime;
    std::chrono::high_resolution_clock::time_point m_endTime;
    std::chrono::high_resolution_clock::time_point m_frameStartTime;
    std::chrono::high_resolution_clock::time_point m_frameEndTime;
    
    std::vector<FrameTiming> m_frameTimings;
    std::vector<DetailedFrameTiming> m_detailedTimings;
    
    // FPS calculation
    double m_lastTime = 0.0;
    int m_frameCount = 0;
    int m_fps = 0;
};

} // namespace Phyxel
