#pragma once

#include "core/Types.h"
#include "core/ChunkManager.h"
#include "physics/PhysicsWorld.h"
#include "input/InputManager.h"
#include "utils/Timer.h"
#include <vector>
#include <memory>

namespace Phyxel {
namespace Utils {

/**
 * @brief Manages performance monitoring, profiling, and statistics collection
 * 
 * Tracks frame timing, rendering statistics, and provides formatted output
 * for performance analysis. Maintains rolling averages and detailed breakdowns.
 */
class PerformanceMonitor {
public:
    PerformanceMonitor();
    ~PerformanceMonitor() = default;

    // Performance tracking
    void updateFrameTiming(double deltaTime);
    FrameTiming profileFrame();
    
    // Statistics output
    void printPerformanceStats(
        Timer* timer,
        const FrameTiming& frameTiming,
        ChunkManager* chunkManager,
        Physics::PhysicsWorld* physicsWorld
    );
    
    void printProfilingInfo(int fps, Input::InputManager* inputManager);
    void printDetailedTimings();
    
    // State management
    void addFrameTiming(const FrameTiming& timing);
    void addDetailedTiming(const DetailedFrameTiming& timing);
    void clearTimings();
    
    // Accessors
    const std::vector<FrameTiming>& getFrameTimings() const { return m_frameTimings; }
    const std::vector<DetailedFrameTiming>& getDetailedTimings() const { return m_detailedTimings; }
    FrameTiming& getCurrentFrameTiming() { return m_currentFrameTiming; }
    const FrameTiming& getCurrentFrameTiming() const { return m_currentFrameTiming; }

private:
    FrameTiming m_currentFrameTiming;
    std::vector<FrameTiming> m_frameTimings;
    std::vector<DetailedFrameTiming> m_detailedTimings;
    
    static constexpr size_t MAX_TIMING_SAMPLES = 60; // Keep last 60 frames
};

} // namespace Utils
} // namespace Phyxel
