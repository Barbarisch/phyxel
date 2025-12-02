#pragma once

#include <gtest/gtest.h>
#include <chrono>
#include <memory>
#include <vector>
#include <iostream>
#include <iomanip>

namespace VulkanCube {
namespace Testing {

/**
 * @brief Result of a stress test run
 */
struct StressTestResult {
    std::string name;
    size_t iterations;
    double totalTimeMs;
    double avgTimeMs;
    double minTimeMs;
    double maxTimeMs;
    size_t peakMemoryMB;
    size_t finalMemoryMB;
    bool success;
    std::string failureReason;
};

/**
 * @brief Base fixture for stress tests
 * 
 * Provides utilities for running long-duration, high-load tests
 * that push the system to its limits.
 */
class StressTestFixture : public ::testing::Test {
protected:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = std::chrono::time_point<Clock>;
    using Duration = std::chrono::duration<double, std::milli>;

    /**
     * @brief Run a stress test with the given parameters
     * 
     * @param name Test name
     * @param iterations Number of iterations to run
     * @param func Function to stress test
     * @param cleanupFunc Optional cleanup between iterations
     * @return StressTestResult with timing and resource usage data
     */
    template<typename Func, typename CleanupFunc = std::function<void()>>
    StressTestResult runStressTest(
        const std::string& name,
        size_t iterations,
        Func func,
        CleanupFunc cleanupFunc = [](){})
    {
        StressTestResult result;
        result.name = name;
        result.iterations = iterations;
        result.success = true;
        result.totalTimeMs = 0.0;
        result.minTimeMs = std::numeric_limits<double>::max();
        result.maxTimeMs = 0.0;
        result.peakMemoryMB = 0;
        result.finalMemoryMB = 0;
        
        std::cout << "\n[STRESS TEST] " << name << " (" << iterations << " iterations)\n";
        
        auto startTime = Clock::now();
        
        try {
            for (size_t i = 0; i < iterations; ++i) {
                auto iterStart = Clock::now();
                
                func(i);
                
                auto iterEnd = Clock::now();
                Duration elapsed = iterEnd - iterStart;
                double iterMs = elapsed.count();
                
                result.totalTimeMs += iterMs;
                result.minTimeMs = std::min(result.minTimeMs, iterMs);
                result.maxTimeMs = std::max(result.maxTimeMs, iterMs);
                
                // Progress indicator every 10%
                if (iterations >= 10 && (i + 1) % (iterations / 10) == 0) {
                    size_t percent = ((i + 1) * 100) / iterations;
                    std::cout << "  Progress: " << percent << "% (" << (i + 1) << "/" << iterations << ")\n";
                }
                
                // Cleanup between iterations
                cleanupFunc();
            }
            
        } catch (const std::exception& e) {
            result.success = false;
            result.failureReason = e.what();
        }
        
        auto endTime = Clock::now();
        Duration totalElapsed = endTime - startTime;
        
        result.avgTimeMs = result.totalTimeMs / iterations;
        
        printResult(result);
        return result;
    }
    
    /**
     * @brief Run a stress test with memory tracking
     */
    template<typename Func>
    StressTestResult runStressTestWithMemoryTracking(
        const std::string& name,
        size_t iterations,
        Func func)
    {
        // Note: Actual memory tracking would require platform-specific APIs
        // This is a simplified version
        return runStressTest(name, iterations, func);
    }
    
    /**
     * @brief Print stress test results
     */
    void printResult(const StressTestResult& result) {
        std::cout << "\n[STRESS RESULTS] " << result.name << "\n";
        std::cout << "  Status:          " << (result.success ? "SUCCESS" : "FAILED") << "\n";
        
        if (!result.success) {
            std::cout << "  Failure:         " << result.failureReason << "\n";
        }
        
        std::cout << "  Iterations:      " << result.iterations << "\n";
        std::cout << "  Total Time:      " << std::fixed << std::setprecision(2) 
                  << result.totalTimeMs << " ms\n";
        std::cout << "  Average:         " << std::fixed << std::setprecision(3) 
                  << result.avgTimeMs << " ms/iteration\n";
        std::cout << "  Min:             " << std::fixed << std::setprecision(3) 
                  << result.minTimeMs << " ms\n";
        std::cout << "  Max:             " << std::fixed << std::setprecision(3) 
                  << result.maxTimeMs << " ms\n";
        
        if (result.peakMemoryMB > 0) {
            std::cout << "  Peak Memory:     " << result.peakMemoryMB << " MB\n";
            std::cout << "  Final Memory:    " << result.finalMemoryMB << " MB\n";
        }
    }
    
    /**
     * @brief Simple timer for measuring time blocks
     */
    class ScopedTimer {
    public:
        ScopedTimer(const std::string& name) : name_(name), start_(Clock::now()) {}
        
        ~ScopedTimer() {
            auto end = Clock::now();
            Duration elapsed = end - start_;
            std::cout << "[TIMER] " << name_ << ": " 
                      << std::fixed << std::setprecision(2) << elapsed.count() << " ms\n";
        }
        
        double elapsedMs() const {
            auto end = Clock::now();
            Duration elapsed = end - start_;
            return elapsed.count();
        }
        
    private:
        std::string name_;
        TimePoint start_;
    };
};

} // namespace Testing
} // namespace VulkanCube
