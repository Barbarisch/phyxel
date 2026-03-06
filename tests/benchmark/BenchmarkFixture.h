#pragma once

#include <gtest/gtest.h>
#include <chrono>
#include <vector>
#include <string>
#include <iomanip>
#include <iostream>

namespace Phyxel {
namespace Testing {

/**
 * @brief Result of a single benchmark iteration
 */
struct BenchmarkResult {
    std::string name;
    double averageMs;
    double minMs;
    double maxMs;
    double stdDevMs;
    size_t iterations;
    size_t operationsPerIteration;
    
    double opsPerSecond() const {
        if (averageMs == 0.0) return 0.0;
        return (operationsPerIteration * 1000.0) / averageMs;
    }
};

/**
 * @brief Base fixture for benchmark tests
 * 
 * Provides timing utilities and result reporting for performance benchmarks.
 * Unlike GoogleTest benchmarks, this is simple and integrated with our test suite.
 */
class BenchmarkFixture : public ::testing::Test {
protected:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = std::chrono::time_point<Clock>;
    using Duration = std::chrono::duration<double, std::milli>;

    /**
     * @brief Run a benchmark with the given function
     * 
     * @param name Benchmark name
     * @param iterations Number of times to run
     * @param opsPerIteration Operations performed per iteration (for ops/sec calculation)
     * @param func Function to benchmark
     * @return BenchmarkResult with timing statistics
     */
    template<typename Func>
    BenchmarkResult runBenchmark(const std::string& name, size_t iterations, size_t opsPerIteration, Func func) {
        std::vector<double> timings;
        timings.reserve(iterations);
        
        // Warmup run
        func();
        
        // Actual benchmark runs
        for (size_t i = 0; i < iterations; ++i) {
            auto start = Clock::now();
            func();
            auto end = Clock::now();
            
            Duration elapsed = end - start;
            timings.push_back(elapsed.count());
        }
        
        // Calculate statistics
        double sum = 0.0;
        double minTime = timings[0];
        double maxTime = timings[0];
        
        for (double time : timings) {
            sum += time;
            minTime = std::min(minTime, time);
            maxTime = std::max(maxTime, time);
        }
        
        double average = sum / iterations;
        
        // Calculate standard deviation
        double variance = 0.0;
        for (double time : timings) {
            double diff = time - average;
            variance += diff * diff;
        }
        double stdDev = std::sqrt(variance / iterations);
        
        BenchmarkResult result{
            name,
            average,
            minTime,
            maxTime,
            stdDev,
            iterations,
            opsPerIteration
        };
        
        printResult(result);
        return result;
    }
    
    /**
     * @brief Print benchmark result in a nice format
     */
    void printResult(const BenchmarkResult& result) {
        std::cout << "\n[BENCHMARK] " << result.name << "\n";
        std::cout << "  Iterations:  " << result.iterations << "\n";
        std::cout << "  Average:     " << std::fixed << std::setprecision(3) << result.averageMs << " ms\n";
        std::cout << "  Min:         " << std::fixed << std::setprecision(3) << result.minMs << " ms\n";
        std::cout << "  Max:         " << std::fixed << std::setprecision(3) << result.maxMs << " ms\n";
        std::cout << "  Std Dev:     " << std::fixed << std::setprecision(3) << result.stdDevMs << " ms\n";
        
        if (result.operationsPerIteration > 0) {
            std::cout << "  Throughput:  " << std::fixed << std::setprecision(0) 
                      << result.opsPerSecond() << " ops/sec\n";
        }
    }
    
    /**
     * @brief Simple timer for measuring code blocks
     */
    class ScopedTimer {
    public:
        ScopedTimer(const std::string& name) : name_(name), start_(Clock::now()) {}
        
        ~ScopedTimer() {
            auto end = Clock::now();
            Duration elapsed = end - start_;
            std::cout << "[TIMER] " << name_ << ": " 
                      << std::fixed << std::setprecision(3) << elapsed.count() << " ms\n";
        }
        
    private:
        std::string name_;
        TimePoint start_;
    };
};

} // namespace Testing
} // namespace Phyxel
