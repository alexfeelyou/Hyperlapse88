#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <dxgi1_4.h>
#include <psapi.h>     
#include <string_view>
#include <unordered_map>
#include <windows.h>
#include "System/Graphics.h"

// Shared compile-time constant dictating the history length for the ImPlot graph
inline constexpr std::size_t MAX_PROFILE_FRAMES{ 300 };

// Aggregates performance data for a single profiled scope (e.g., "Physics", "Render")
struct ProfileData
{
    // Fixed-size circular buffer prevents vector reallocation every frame
    std::array<float, MAX_PROFILE_FRAMES> timings{};

    // Explicit tracking of the most recent frame's time for instant UI readouts
    float lastFrameTime{ 0.0f };
};

// Holds snapshot hardware metrics
struct SystemMetrics
{
    float fps{ 0.0f };
    float ramUsageMB{ 0.0f };
    float vramUsageMB{ 0.0f };
};

// Manages and stores historical CPU performance metrics
class ProfilerManager
{
public:
    // Enforce strict singleton ownership by deleting copy/move semantics
    ProfilerManager(const ProfilerManager&) = delete;
    ProfilerManager& operator=(const ProfilerManager&) = delete;
    ProfilerManager(ProfilerManager&&) = delete;
    ProfilerManager& operator=(ProfilerManager&&) = delete;

    // Returns a reference to the static local instance (Meyers Singleton)
    [[nodiscard]] static ProfilerManager& Instance() noexcept;

    // Stores the calculated time in the circular buffer
    void PushCpuTime(const char* scopeName, float timeMs);

    // Increments the running draw-call counter for the frame currently in progress
    // count defaults to 1 since most call sites issue exactly one draw call per invocation
    void RecordDrawCall(std::size_t count = 1) noexcept;

    // Advances the internal ring buffer index. Must be called exactly once per frame.
    void EndFrame(float deltaTime) noexcept;

    // Returns the draw-call total captured at the end of the most recently completed frame
    [[nodiscard]] std::size_t GetLastFrameDrawCallCount() const noexcept;

    // Read-only accessor for the UI to draw the graphs
    [[nodiscard]] const std::unordered_map<const char*, ProfileData>& GetCpuData() const noexcept;

    // Returns the rolling per-frame history of total frame time (milliseconds),
    // used to draw the FPS/Frame performance graph in the Editor
    [[nodiscard]] const std::array<float, MAX_PROFILE_FRAMES>& GetFrameTimeHistory() const noexcept;

    // Returns the current offset so ImPlot knows where the circular buffer wraps around
    [[nodiscard]] std::size_t GetCurrentFrameIndex() const noexcept;

	// Returns a snapshot of the current system metrics (FPS, RAM, VRAM)
    [[nodiscard]] const SystemMetrics& GetMetrics() const noexcept;

private:
    // Private constructor/destructor to enforce singleton
    ProfilerManager() = default;
    ~ProfilerManager() = default;

    // Queries Windows and DXGI for memory usage
    void UpdateHardwareMetrics() noexcept;

    // The core data store. Maps a static string literal to its historical timings.
    std::unordered_map<const char*, ProfileData> m_cpuTimers{};
    std::size_t m_currentFrameIndex{ 0 };

    // Rolling per-frame history of total frame time in milliseconds, kept separate
    // from m_cpuTimers since it's a single global value rather than a named scope
    std::array<float, MAX_PROFILE_FRAMES> m_frameTimeHistory{};

    // Running total of draw calls issued so far during the current, still-in-progress frame.
    // Reset to 0 every time EndFrame() is called.
    std::size_t m_drawCallsThisFrame{ 0 };

    // Snapshot of m_drawCallsThisFrame taken at the end of the last completed frame
    std::size_t m_lastFrameDrawCallCount{ 0 };

    SystemMetrics m_metrics{};
    float m_hardwareUpdateTimer{ 0.0f };
};

// RAII probe to measure the lifespan of a C++ scope
class ScopedTimer
{
public:
    // Constructor captures the exact start time
    explicit ScopedTimer(const char* name) noexcept;

    // Destructor automatically captures the end time and pushes it to the manager
    ~ScopedTimer() noexcept;

    // Delete copy/move to prevent accidental timer duplication and corrupted measurements
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    const char* m_name{};
    std::chrono::high_resolution_clock::time_point m_start{};
};


// Zero-Cost Abstraction Macros
// In Debug builds, these macros expand to actual timer objects
// In Release builds, they compile down to nothing ((void)0), ensuring 0% CPU overhead

#ifdef _DEBUG
    // Preprocessor trick to concatenate line numbers, ensuring unique variable names 
    // even if multiple PROFILE_SCOPE macros exist in the same function.
#define CONCAT_IMPL(a, b) a##b
#define MACRO_CONCAT(a, b) CONCAT_IMPL(a, b)

#define PROFILE_SCOPE(name) ScopedTimer MACRO_CONCAT(timer, __LINE__){name}

#define PROFILE_END_FRAME(dt) ProfilerManager::Instance().EndFrame(dt)

#define PROFILE_DRAW_CALL() ProfilerManager::Instance().RecordDrawCall()
#else
#define PROFILE_SCOPE(name) ((void)0)
#define PROFILE_END_FRAME(dt) ((void)0)
#define PROFILE_DRAW_CALL() ((void)0)
#endif