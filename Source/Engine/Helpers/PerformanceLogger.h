#pragma once

#include <string>
#include <fstream>
#include <chrono>
#include <unordered_map>
#include <mutex>
#include <SDL3/SDL.h>

enum class PerfBucket {
    Logic,
    Render3D,
    WindowOS
};

class PerformanceLogger
{
public:
    static PerformanceLogger& Instance() {
        static PerformanceLogger instance;
        return instance;
    }

    void Initialize();
    void Shutdown();

    // Logging Events
    void LogInfo(const std::string& message);
    void LogWindowAction(const std::string& action, const std::string& windowName, float latencyMs);
    void LogBoundaryClamp(const std::string& windowName, int oldX, int oldY, int newX, int newY);

    // Profiling Timers
    void StartTimer(PerfBucket bucket);
    void StopTimer(PerfBucket bucket);

    // Per-Frame Check
    void EndFrameCheck(float currentFps, int activeWindows);

private:
    PerformanceLogger() = default;
    ~PerformanceLogger() { Shutdown(); }

    std::ofstream m_logFile;
    std::mutex m_mutex;

    // Timers
    std::chrono::high_resolution_clock::time_point m_bucketStarts[3];
    float m_bucketAccumulators[3] = { 0.0f, 0.0f, 0.0f };

    // FPS Tracking (Debounce agar tidak spam per frame)
    float m_fpsCooldown = 0.0f;
    float m_lowestFps = 999.0f;
    int m_maxWindows = 0;
    int m_maxStableWindows = 0;
    int m_totalClamps = 0;
};