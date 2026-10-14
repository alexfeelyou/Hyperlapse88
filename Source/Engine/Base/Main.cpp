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

        // 0.0 disables the cap entirely for uncapped testing
        constexpr double targetFPS = 120.0;

        // Disable blocking swap chain VSync if software limiter is pacing frames
        WindowManager::Instance().SetVSync(targetFPS <= 0.0);

        bool running = true;
        Uint64 lastTime = SDL_GetPerformanceCounter();
        const Uint64 frequency = SDL_GetPerformanceFrequency();

        // Tracks the absolute next scheduled frame boundary to eliminate pacing drift
        Uint64 nextFrameTicks = lastTime;

        while (running)
        {
            // Just-In-Time Pacing: Wait for the frame boundary before sampling inputs
            // This guarantees zero-latency responsiveness at 30, 60, and 120 FPS
            if (targetFPS > 0.0)
            {
                const Uint64 targetTicksPerFrame{ static_cast<Uint64>(static_cast<double>(frequency) / targetFPS) };
                nextFrameTicks += targetTicksPerFrame;

                const Uint64 currentTicks{ SDL_GetPerformanceCounter() };

                if (currentTicks > nextFrameTicks + targetTicksPerFrame)
                {
                    nextFrameTicks = currentTicks;
                }
                else
                {
                    while (true)
                    {
                        const Uint64 now{ SDL_GetPerformanceCounter() };
                        if (now >= nextFrameTicks) break;

                        const Uint64 remaining{ nextFrameTicks - now };
                        const Uint64 sleepThreshold{ (frequency * 2) / 1000 }; // 2 ms margin

                        if (remaining > sleepThreshold)
                        {
                            const double remainingMs{ static_cast<double>(remaining - sleepThreshold) * 1000.0 / static_cast<double>(frequency) };
                            std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(remainingMs));
                        }
                    }
                }
            }

            // Sample time exactly at frame execution start
            const Uint64 currentTime = SDL_GetPerformanceCounter();
            float elapsedTime = static_cast<float>(currentTime - lastTime) / static_cast<float>(frequency);
            lastTime = currentTime;

            if (elapsedTime > 0.10f) elapsedTime = 0.10f;

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