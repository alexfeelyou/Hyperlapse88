#include "Framework.h"

namespace
{
#ifdef _DEBUG
    inline constexpr bool s_isDebugMode{ true };
#else
    inline constexpr bool s_isDebugMode{ false };
#endif
}

static WNDPROC s_OriginalWndProc = nullptr;

LRESULT CALLBACK ImGuiHookWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGuiRenderer::HandleMessage(hWnd, msg, wParam, lParam)) {
        return true;
    }

    if (s_OriginalWndProc) {
        return CallWindowProc(s_OriginalWndProc, hWnd, msg, wParam, lParam);
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

Framework* Framework::pInstance = nullptr;

Framework::Framework()
{
    pInstance = this;
    Graphics::Instance().Initialize();

    if (!AudioManager::Instance().Initialize()) {}

    auto mainWin = WindowManager::Instance().CreateGameWindow("Hyperlapse 88", 1600, 900);

    SDL_Window* sdlWin = mainWin->GetSDLWindow();
    SDL_ShowWindow(sdlWin);

    if constexpr (s_isDebugMode)
    {
        // Debug Mode: Windowed, bordered, and automatically maximized for IDE debugging
        SDL_SetWindowBordered(sdlWin, true);
        SDL_SetWindowResizable(sdlWin, true);
        SDL_MaximizeWindow(sdlWin);
    }
    else
    {
        // Release Mode: Locked aspect, borderless, and true fullscreen for production delivery
        SDL_SetWindowBordered(sdlWin, false);
        SDL_SetWindowResizable(sdlWin, false);
        SDL_SetWindowFullscreen(sdlWin, true);
    }

    HWND hwnd = (HWND)SDL_GetPointerProperty(
        SDL_GetWindowProperties(mainWin->GetSDLWindow()),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER,
        NULL
    );

    Input::Instance().Initialize(hwnd);
    ImGuiRenderer::Initialize(hwnd, Graphics::Instance().GetDevice(), Graphics::Instance().GetDeviceContext());
    s_OriginalWndProc = (WNDPROC)SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)ImGuiHookWndProc);

    // Init Scene
    scene = std::make_unique<SceneTitle>();
}

Framework::~Framework()
{
    scene.reset();
    WindowManager::Instance().ClearAll();
    ImGuiRenderer::Finalize();
    pInstance = nullptr;
}

Framework* Framework::Instance() { return pInstance; }
void Framework::ChangeScene(std::unique_ptr<Scene> newScene) { nextScene = std::move(newScene); }

platform::Window* Framework::GetMainWindow() const
{
    return WindowManager::Instance().GetWindowByIndex(0);
}

void Framework::Render(float elapsedTime)
{
    WindowManager::Instance().RenderAll(elapsedTime, scene.get());
}

void Framework::Update(float elapsedTime)
{
    if (nextScene)
    {
        scene = std::move(nextScene); 
        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
    }

    CalculateFrameStats(elapsedTime);
    Input::Instance().Update();
    AudioManager::Instance().Update(elapsedTime);

    ImGuiRenderer::NewFrame(); 

    if (scene) scene->Update(elapsedTime);
}

void Framework::ForceUpdateRender()
{
    static Uint64 lastTime = 0;
    if (lastTime == 0) lastTime = SDL_GetPerformanceCounter();
    Uint64 currentTime = SDL_GetPerformanceCounter();
    float dt = (float)(currentTime - lastTime) / (float)SDL_GetPerformanceFrequency();
    lastTime = currentTime;
    if (dt > 0.05f) dt = 0.05f;
    Update(dt);
    Render(dt);
}

void Framework::CalculateFrameStats(float dt)
{
    static int frames = 0;
    static float timeAccumulator = 0.0f;

    frames++;
    timeAccumulator += dt;

    if (timeAccumulator >= 1.0f)
    {
        frames = 0;
        timeAccumulator -= 1.0f;
    }
}

void Framework::OnResize(int width, int height)
{
    if (scene)
    {
        scene->OnResize(width, height);
    }
}

void Framework::Quit()
{
    PostQuitMessage(0);
    SDL_Event quitEvent;
    SDL_zero(quitEvent);
    quitEvent.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&quitEvent);
}

LRESULT CALLBACK Framework::HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    platform::Window* mainWin = GetMainWindow();
    HWND mainHwnd = NULL;

    if (mainWin && mainWin->GetSDLWindow()) {
        mainHwnd = (HWND)SDL_GetPointerProperty(
            SDL_GetWindowProperties(mainWin->GetSDLWindow()),
            SDL_PROP_WINDOW_WIN32_HWND_POINTER,
            NULL
        );
    }

    if (mainWin && hWnd == mainHwnd)
    {
        if (ImGuiRenderer::HandleMessage(hWnd, msg, wParam, lParam)) return true;
    }
    return 0;
}
