#include "WindowManager.h"
#include "Scene.h" 
#include <algorithm>
#include "System/ImGuiRenderer.h" 
#include "System/Graphics.h"
#include <mutex>
#include "Framework.h"
#include <SDL3/SDL.h> 

void WindowManager::Update(float dt)
{
    if (m_dirtyPriority)
    {
        EnforceWindowPriorities();
        m_dirtyPriority = false;
    }
}

void WindowManager::EnforceWindowPriorities()
{
    std::vector<platform::Window*> sortedWindows;
    sortedWindows.reserve(windows.size());

    for (auto& win : windows)
    {
        if (win.get() != debugWindow && win->GetPriority() < 100)
        {
            sortedWindows.push_back(win.get());
        }
    }

	// Sort windows by priority (lower number = higher priority)
    std::sort(sortedWindows.begin(), sortedWindows.end(),
        [](platform::Window* a, platform::Window* b) {
            return a->GetPriority() < b->GetPriority();
        });

    for (platform::Window* win : sortedWindows)
    {
        if (win->GetSDLWindow())
        {
            SDL_RaiseWindow(win->GetSDLWindow());
        }
    }
}

void WindowManager::RenderAll(float dt, Scene* scene)
{
    bool vsyncApplied = false;

    for (auto& win : windows)
    {
        if (!win->GetSDLWindow()) continue;

        win->BeginRender(0.0f, 0.0f, 0.0f, 1.0f);

        if (win.get() == windows.front().get())
        {
            if (scene) scene->Render(dt, win->GetCamera());
            ImGuiRenderer::Render(Graphics::Instance().GetDeviceContext());
        }

        else
        {
            if (scene && win->ShouldRenderScene()) {
                scene->Render(dt, win->GetCamera());
            }
        }

		// Determine sync interval based on transparency and vsync application
        int syncInterval = vsyncApplied ? 0 : 1;
        if (!vsyncApplied && syncInterval == 1) vsyncApplied = true;

        win->EndRender(syncInterval);
    }
}

void WindowManager::HandleResize(SDL_Window* sdlWindow, int width, int height)
{
    for (auto& win : windows)
    {
        if (win->GetSDLWindow() == sdlWindow)
        {
            win->Resize(width, height);
            return;
        }
    }
}

platform::Window* WindowManager::CreateGameWindow(const char* title, int width, int height)
{
    auto newWindow = std::make_unique<platform::Window>();

    if (!newWindow->Initialize(title, width, height))
    {
        return nullptr;
    }

    newWindow->SetTickCallback([]() {
        Framework::Instance()->Update(0.016f);
        Framework::Instance()->Render(0.016f);
        });

    platform::Window* ptr = newWindow.get();
    windows.push_back(std::move(newWindow));

    MarkPriorityDirty();

    return ptr;
}

void WindowManager::DestroyWindow(platform::Window* targetWindow)
{
    windows.erase(
        std::remove_if(windows.begin(), windows.end(),
            [targetWindow](const std::unique_ptr<platform::Window>& w) {
                return w.get() == targetWindow;
            }),
        windows.end()
    );
}

void WindowManager::ClearAll()
{
    windows.clear();
}