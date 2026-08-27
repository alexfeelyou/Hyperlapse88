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
static LONG s_lockedWidth = 0;
static LONG s_lockedHeight = 0;

LRESULT CALLBACK ImGuiHookWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGuiRenderer::HandleMessage(hWnd, msg, wParam, lParam)) {
        return true;
    }

    if constexpr (s_isDebugMode)
    {
        // Only block restore/size/move while the window is maximized.
        // If it's minimized, let SC_RESTORE through so Alt+Tab / taskbar
        // clicks can bring it back
        if (msg == WM_SYSCOMMAND)
        {
            WPARAM cmd = wParam & 0xFFF0;
            if (!IsIconic(hWnd))
            {
                if (cmd == SC_RESTORE || cmd == SC_SIZE || cmd == SC_MOVE)
                {
                    return 0;
                }
            }
        }

        if (msg == WM_NCLBUTTONDOWN && wParam == HTCAPTION)
        {
            return 0;
        }

        // Belt-and-suspenders: even if something re-adds WS_SIZEBOX later,
        // this clamps the trackable size to the locked maximized size
        if (msg == WM_GETMINMAXINFO && s_lockedWidth > 0 && s_lockedHeight > 0)
        {
            MINMAXINFO* mmi = (MINMAXINFO*)lParam;
            mmi->ptMinTrackSize.x = s_lockedWidth;
            mmi->ptMinTrackSize.y = s_lockedHeight;
            mmi->ptMaxTrackSize.x = s_lockedWidth;
            mmi->ptMaxTrackSize.y = s_lockedHeight;
            return 0;
        }
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
        // Ensure it's bordered and temporarily allow resizing so SDL can maximize it properly
        SDL_SetWindowBordered(sdlWin, true);
        SDL_SetWindowResizable(sdlWin, true);

        // Maximize the window to fill the screen (gets full resolution like 1920x1080)
        SDL_MaximizeWindow(sdlWin);

        // Tell SDL it's no longer resizable BEFORE we touch the raw Win32 style,
        // so SDL doesn't re-apply its own style bits over our changes afterward.
        SDL_SetWindowResizable(sdlWin, false);

        // Fetch the native Win32 handle to lock down the window frame
        HWND hwnd = (HWND)SDL_GetPointerProperty(
            SDL_GetWindowProperties(sdlWin),
            SDL_PROP_WINDOW_WIN32_HWND_POINTER,
            NULL
        );

        if (hwnd)
        {
            LONG style = GetWindowLong(hwnd, GWL_STYLE);
            style &= ~WS_SIZEBOX;       // Removes the dragging border (disables manual resizing)
            style &= ~WS_MAXIMIZEBOX;   // Greys out/disables the maximize button so it cannot be clicked
            SetWindowLong(hwnd, GWL_STYLE, style);

            // Force Windows to redraw the window frame with the locked styles
            SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

            // Cache the maximized size so WM_GETMINMAXINFO can hard-lock it below
            RECT rect;
            GetWindowRect(hwnd, &rect);
            s_lockedWidth = rect.right - rect.left;
            s_lockedHeight = rect.bottom - rect.top;
        }
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
    EditorManager::Instance().Initialize();

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
    // Tracks rendering submission time
    PROFILE_SCOPE("Framework::Render");

    WindowManager::Instance().RenderAll(elapsedTime, scene.get());
}

void Framework::Update(float elapsedTime)
{
    // It tracks the duration of the entire Update function
    PROFILE_SCOPE("Framework::Update Total");

    if (nextScene)
    {
        scene = std::move(nextScene);
    }

    CalculateFrameStats(elapsedTime);
    Input::Instance().Update();
    AudioManager::Instance().Update(elapsedTime);

    ImGuiRenderer::NewFrame(); 

    EditorManager::Instance().Draw(scene.get());

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
