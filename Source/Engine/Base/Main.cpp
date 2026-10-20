#include <exception> 
#include <iostream> 
#include <memory>
#include <SDL3/SDL.h> 
#include <thread>
#include <windows.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#include "Framework.h"
#include "ProfilerManager.h"
#include "WindowManager.h"

namespace
{
    // High-precision frame pacer implementing hybrid sleep-spin wait to eliminate input latency and drift
    class FramePacer final
    {
    public:
        explicit FramePacer(double targetFps) noexcept
            : m_targetFps{ targetFps }
            , m_frequency{ SDL_GetPerformanceFrequency() }
            , m_lastTime{ SDL_GetPerformanceCounter() }
            , m_nextFrameTicks{ m_lastTime }
        {}

        // Paces the frame boundary and returns the sanitized delta time in seconds
        [[nodiscard]] float Pace() noexcept
        {
            // When targetFps <= 0.0, software rate limiting is bypassed for uncapped throughput
            if (m_targetFps > 0.0)
            {
                const auto targetTicksPerFrame{ static_cast<Uint64>(static_cast<double>(m_frequency) / m_targetFps) };
                m_nextFrameTicks += targetTicksPerFrame;

                const Uint64 currentTicks{ SDL_GetPerformanceCounter() };

                // Catch-up clamp: if a major hitch occurred, advance nextFrameTicks to prevent catch-up bursts
                if (currentTicks > m_nextFrameTicks + targetTicksPerFrame)
                {
                    m_nextFrameTicks = currentTicks;
                }
                else
                {
                    // 2 ms margin threshold to ensure the OS timer never oversleeps past the frame boundary
                    const Uint64 sleepThreshold{ (m_frequency * 2) / 1000 };

                    while (true)
                    {
                        const Uint64 now{ SDL_GetPerformanceCounter() };
                        if (now >= m_nextFrameTicks)
                        {
                            break;
                        }

                        const Uint64 remaining{ m_nextFrameTicks - now };
                        if (remaining > sleepThreshold)
                        {
                            const double remainingMs{ static_cast<double>(remaining - sleepThreshold) * 1000.0 / static_cast<double>(m_frequency) };
                            std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(remainingMs));
                        }
                    }
                }
            }

            // Sample time precisely at the start of frame simulation
            const Uint64 currentTime{ SDL_GetPerformanceCounter() };
            float elapsedTime{ static_cast<float>(currentTime - m_lastTime) / static_cast<float>(m_frequency) };
            m_lastTime = currentTime;

            // Clamp max delta time to 100 ms to prevent physics explosions during breakpoints or window drags
            if (elapsedTime > 0.10f)
            {
                elapsedTime = 0.10f;
            }

            return elapsedTime;
        }

    private:
        double m_targetFps{ 0.0 };
        Uint64 m_frequency{ 0 };
        Uint64 m_lastTime{ 0 };
        Uint64 m_nextFrameTicks{ 0 };
    };
}

int main(int argc, char* argv[])
{
    // Init SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        MessageBoxA(NULL, SDL_GetError(), "SDL Init Failed", MB_OK | MB_ICONERROR);
        return -1;
    }

    try
    {
        // Enforce 1 ms OS timer resolution across the entire process lifetime
        struct ScopedTimerResolution final
        {
            ScopedTimerResolution() noexcept { timeBeginPeriod(1); }
            ~ScopedTimerResolution() noexcept { timeEndPeriod(1); }
            ScopedTimerResolution(const ScopedTimerResolution&) = delete;
            ScopedTimerResolution& operator=(const ScopedTimerResolution&) = delete;
        };
        const ScopedTimerResolution timerResolution{};

        auto framework = std::make_unique<Framework>();

        // Engine Frame Pacing Configuration:
        // Set s_targetFps to 0.0 for pure uncapped throughput diagnostics.
        // Set s_targetFps to 120.0 (or 60.0) for smooth, drift-free production gameplay.
        constexpr double s_targetFps{ 0.0 };

        // VSync Configuration:
        // Must be false during uncapped throughput testing to allow DXGI tearing.
        constexpr bool s_enableVsync{ false };
        WindowManager::Instance().SetVSync(s_enableVsync);

        FramePacer pacer{ s_targetFps };
        bool running{ true };

        while (running)
        {
            const float elapsedTime{ pacer.Pace() };

            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                if (event.type == SDL_EVENT_QUIT) running = false;
                if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
                {
                    running = false;
                }

                if (event.type == SDL_EVENT_WINDOW_RESIZED || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
                {
                    SDL_Window* resizedWin = SDL_GetWindowFromID(event.window.windowID);
                    if (resizedWin) {
                        WindowManager::Instance().HandleResize(resizedWin, event.window.data1, event.window.data2);

                        if (framework) {
                            framework->OnResize(event.window.data1, event.window.data2);
                        }
                    }
                }
            }

            if (framework)
            {
                framework->Update(elapsedTime);
                framework->Render(elapsedTime);
            }

            PROFILE_END_FRAME(elapsedTime);

            if (!WindowManager::Instance().HasWindows())
            {
                running = false;
            }
        }
    }

    catch (const std::exception& e)
    {
        std::string errorMessage = "Runtime Error Occurred:\n";
        errorMessage += e.what();
        MessageBoxA(NULL, errorMessage.c_str(), "CRITICAL ERROR", MB_OK | MB_ICONERROR);

        OutputDebugStringA(errorMessage.c_str());

        SDL_Quit();
        return -1;
    }
    catch (...)
    {
        MessageBoxA(NULL, "Unknown Error Occurred!", "CRITICAL ERROR", MB_OK | MB_ICONERROR);
        SDL_Quit();
        return -1;
    }

    SDL_Quit();
    return 0;
}