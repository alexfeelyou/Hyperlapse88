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
    platform::Window* CreateGameWindow(const char* title, int width, int height, bool isTransparent = false);
    void DestroyWindow(platform::Window* targetWindow);
    void EnforceWindowPriorities();
    void MarkPriorityDirty() { m_dirtyPriority = true; }

    void SetDebugWindow(platform::Window* win) { debugWindow = win; }
    platform::Window* GetDebugWindow() const { return debugWindow; }

    bool HasWindows() const { return !windows.empty(); }

    platform::Window* GetWindowByIndex(size_t index)
    {
        if (index < windows.size()) return windows[index].get();
        return nullptr;
    }

    void SetTopmost(bool enabled) { m_topmostEnabled = enabled; MarkPriorityDirty(); }
    bool IsTopmost() const { return m_topmostEnabled; }

private:
    WindowManager() = default;
    ~WindowManager() = default;
    WindowManager(const WindowManager&) = delete;
    void operator=(const WindowManager&) = delete;

    bool m_topmostEnabled = false;
private:
    std::vector<std::unique_ptr<platform::Window>> windows;

    platform::Window* debugWindow = nullptr;

    bool m_dirtyPriority = false;
};