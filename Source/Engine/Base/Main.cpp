#include <exception> 
#include <iostream> 
#include <memory>
#include <SDL3/SDL.h> 
#include <thread>
#include <windows.h>
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
        auto framework = std::make_unique<Framework>();

        bool running = true;
        Uint64 lastTime = SDL_GetPerformanceCounter();
        const Uint64 frequency = SDL_GetPerformanceFrequency();

        // 0.0 disables the cap entirely for uncapped testing
        constexpr double targetFPS = 120.0;

        while (running)
        {
            Uint64 frameStart = SDL_GetPerformanceCounter();


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
                        // Tell DirectX to rebuild the swap chain buffers
                        WindowManager::Instance().HandleResize(resizedWin, event.window.data1, event.window.data2);

                        // Tell the game logic to update Camera FOV and post-processing resolutions
                        if (framework) {
                            framework->OnResize(event.window.data1, event.window.data2);
                        }
                    }
                }
            }

            Uint64 currentTime = SDL_GetPerformanceCounter();
            float elapsedTime = (float)(currentTime - lastTime) / (float)frequency;
            lastTime = currentTime;

            if (elapsedTime > 0.05f) elapsedTime = 0.05f;

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

            // High-Precision Hybrid Frame Limiter
            if (targetFPS > 0.0)
            {
                const Uint64 targetTicksPerFrame{ static_cast<Uint64>(frequency / targetFPS) };

                while (true)
                {
                    const Uint64 now{ SDL_GetPerformanceCounter() };
                    const Uint64 ticksPassed{ now - frameStart };

                    if (ticksPassed >= targetTicksPerFrame)
                    {
                        break;
                    }

                    const Uint64 remaining{ targetTicksPerFrame - ticksPassed };

                    const Uint64 sleepThreshold{ (frequency * 2) / 1000 };

                    if (remaining > sleepThreshold)
                    {
                        const double remainingMs{ static_cast<double>(remaining - sleepThreshold) * 1000.0 / static_cast<double>(frequency) };
                        std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(remainingMs));
                    }
                    // For the final <2ms, the loop spinlocks (busy-waits) for pinpoint microsecond accuracy
                }
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