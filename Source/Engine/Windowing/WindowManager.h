#pragma once

#include <vector>
#include <memory>
#include <algorithm>
#include <windows.h>
#include "RenderWindow.h"

class Scene;

class WindowManager
{
public:
    static WindowManager& Instance()
    {
        static WindowManager instance;
        return instance;
    }

	// Core Functions
    void Update(float dt);
    void RenderAll(float dt, Scene* scene);
    void HandleResize(SDL_Window* sdlWindow, int width, int height);
    void ClearAll();

	// Users Functions
    platform::Window* CreateGameWindow(const char* title, int width, int height);
    void DestroyWindow(platform::Window* targetWindow);

    void SetDebugWindow(platform::Window* win) { debugWindow = win; }
    platform::Window* GetDebugWindow() const { return debugWindow; }

    bool HasWindows() const { return !windows.empty(); }

    platform::Window* GetWindowByIndex(size_t index)
    {
        if (index < windows.size()) return windows[index].get();
        return nullptr;
    }

private:
    WindowManager() = default;
    ~WindowManager() = default;
    WindowManager(const WindowManager&) = delete;
    void operator=(const WindowManager&) = delete;

private:
    std::vector<std::unique_ptr<platform::Window>> windows;

    platform::Window* debugWindow = nullptr;
};