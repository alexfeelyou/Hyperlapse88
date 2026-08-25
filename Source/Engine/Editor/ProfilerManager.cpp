#include "ProfilerManager.h"

#pragma comment(lib, "psapi.lib") // Auto-link the Windows Process API

ProfilerManager& ProfilerManager::Instance() noexcept
{
    static ProfilerManager s_instance{};
    return s_instance;
}

void ProfilerManager::PushCpuTime(const char* scopeName, float timeMs)
{
    // Retrieve the data block for this scope (creates it if it doesn't exist)
    ProfileData& data{ m_cpuTimers[scopeName] };

    // Store the time in the circular buffer at the current frame index
    data.timings[m_currentFrameIndex] = timeMs;
    data.lastFrameTime = timeMs;
}

void ProfilerManager::EndFrame(float deltaTime) noexcept
{
    m_currentFrameIndex = (m_currentFrameIndex + 1) % MAX_PROFILE_FRAMES;

    // Smooth FPS calculation
    if (deltaTime > 0.0f)
    {
        // Simple low-pass filter to prevent the FPS counter from flickering unreadably
        m_metrics.fps = (m_metrics.fps * 0.9f) + ((1.0f / deltaTime) * 0.1f);
    }

    // Throttle hardware queries to twice per second to prevent profiling overhead
    m_hardwareUpdateTimer += deltaTime;
    if (m_hardwareUpdateTimer >= 0.5f)
    {
        m_hardwareUpdateTimer = 0.0f;
        UpdateHardwareMetrics();
    }
}

void ProfilerManager::UpdateHardwareMetrics() noexcept
{
    constexpr float bytesToMB{ 1.0f / (1024.0f * 1024.0f) };

    // Query System RAM
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
    {
        m_metrics.ramUsageMB = static_cast<float>(pmc.WorkingSetSize) * bytesToMB;
    }

    // Query GPU VRAM via DirectX
    auto* device{ Graphics::Instance().GetDevice() };
    if (!device) return;

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))))
    {
        Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        if (SUCCEEDED(dxgiDevice->GetAdapter(&adapter)))
        {
            // DXGIAdapter3 is required to query Video Memory Info
            Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter3;
            if (SUCCEEDED(adapter.As(&adapter3)))
            {
                DXGI_QUERY_VIDEO_MEMORY_INFO videoMemoryInfo{};
                if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &videoMemoryInfo)))
                {
                    m_metrics.vramUsageMB = static_cast<float>(videoMemoryInfo.CurrentUsage) * bytesToMB;
                }
            }
        }
    }
}

const std::unordered_map<const char*, ProfileData>& ProfilerManager::GetCpuData() const noexcept
{
    return m_cpuTimers;
}

std::size_t ProfilerManager::GetCurrentFrameIndex() const noexcept
{
    return m_currentFrameIndex;
}

const SystemMetrics& ProfilerManager::GetMetrics() const noexcept
{
    return m_metrics;
}

// ScopedTimer Implementation 

ScopedTimer::ScopedTimer(const char* name) noexcept
    : m_name{ name }
    , m_start{ std::chrono::high_resolution_clock::now() }
{}

ScopedTimer::~ScopedTimer() noexcept
{
    // Capture the time the moment the object goes out of scope
    const auto end{ std::chrono::high_resolution_clock::now() };

    // Calculate duration specifically in floating-point milliseconds
    const std::chrono::duration<float, std::milli> duration{ end - m_start };

    // Submit the data to the central manager
    ProfilerManager::Instance().PushCpuTime(m_name, duration.count());
}