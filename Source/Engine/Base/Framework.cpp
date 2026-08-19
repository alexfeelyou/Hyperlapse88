#include "Framework.h"

// ========================================================
// Jembatan Win32 ke ImGui
// ========================================================
static WNDPROC s_OriginalWndProc = nullptr;

LRESULT CALLBACK ImGuiHookWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // 1. Berikan event klik/keyboard ke ImGui terlebih dahulu
    if (ImGuiRenderer::HandleMessage(hWnd, msg, wParam, lParam)) {
        return true;
    }

    // 2. Teruskan sisa pesannya ke sistem SDL3
    if (s_OriginalWndProc) {
        return CallWindowProc(s_OriginalWndProc, hWnd, msg, wParam, lParam);
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}
// ========================================================

Framework* Framework::pInstance = nullptr;

Framework::Framework()
{
    pInstance = this;
    Graphics::Instance().Initialize();

    if (!AudioManager::Instance().Initialize()) {  }

    AttackParamManager::Instance().Load("AttackParams.json");

    // Buat Main Window (Fullscreen Borderless)
    auto mainWin = WindowManager::Instance().CreateGameWindow("Main Window (close here)", 1600, 900);
    mainWin->SetPriority(0);
    mainWin->SetDraggable(false);

    SDL_ShowWindow(mainWin->GetSDLWindow());
    // Tambahkan flag Resizable agar bisa di-drag ujungnya
    SDL_SetWindowResizable(mainWin->GetSDLWindow(), true);
    SDL_SetWindowBordered(mainWin->GetSDLWindow(), true);
    //SDL_SetWindowPosition(mainWin->GetSDLWindow(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_SetWindowPosition(mainWin->GetSDLWindow(), 5, 35);

    // Posisikan di tengah saat awal

    HWND hwnd = (HWND)SDL_GetPointerProperty(
        SDL_GetWindowProperties(mainWin->GetSDLWindow()),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER,
        NULL
    );

    Input::Instance().Initialize(hwnd);
    ImGuiRenderer::Initialize(hwnd, Graphics::Instance().GetDevice(), Graphics::Instance().GetDeviceContext());
    s_OriginalWndProc = (WNDPROC)SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)ImGuiHookWndProc);

    // Init Scene
#if 1
    scene = std::make_unique<SceneTitle>();
#else
    scene = std::make_unique<SceneBoss>();
#endif
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

Beyond::Window* Framework::GetMainWindow() const
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
        scene = std::move(nextScene); // SceneBoss destructor runs here
        // SceneBoss destructor re-enables ViewportsEnable but platform
        // functions (Platform_CreateWindow etc.) are NULL — this crashes NewFrame.
        // Force it back off until a scene explicitly sets it up.
        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
    }

    CalculateFrameStats(elapsedTime);
    Input::Instance().Update();
    AudioManager::Instance().Update(elapsedTime);

    ImGuiRenderer::NewFrame(); // Now safe

    if (scene) scene->Update(elapsedTime);

    if (auto* boss = dynamic_cast<SceneBoss*>(scene.get())) {
        if (boss->IsPendingSceneChange()) {
            ChangeScene(std::make_unique<SceneTitle>());
            return;
        }
    }
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
        //float fps = static_cast<float>(frames);
        //std::ostringstream outs;
        //outs.precision(6);

        //// Menggunakan judul resmi game barumu
        //outs << "FPS: " << fps << " (" << (1000.0f / fps) << " ms)";

        // Loop melalui semua window yang ada di WindowManager
        //size_t index = 0;
        //while (Beyond::Window* win = WindowManager::Instance().GetWindowByIndex(index))
        //{
        //    // Hanya update title pada window yang tidak disembunyikan (visible)
        //    if (win->IsVisible() && win->GetSDLWindow())
        //    {
        //        SDL_SetWindowTitle(win->GetSDLWindow(), outs.str().c_str());
        //    }
        //    index++;
        //}

        frames = 0;
        timeAccumulator -= 1.0f;
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
    Beyond::Window* mainWin = GetMainWindow();
    HWND mainHwnd = NULL;

    // Ekstrak HWND dari main window yang menggunakan SDL3
    if (mainWin && mainWin->GetSDLWindow()) {
        mainHwnd = (HWND)SDL_GetPointerProperty(
            SDL_GetWindowProperties(mainWin->GetSDLWindow()),
            SDL_PROP_WINDOW_WIN32_HWND_POINTER,
            NULL
        );
    }

    // Bandingkan hWnd dengan mainHwnd hasil ekstrak
    if (mainWin && hWnd == mainHwnd)
    {
        if (ImGuiRenderer::HandleMessage(hWnd, msg, wParam, lParam)) return true;
    }
    return 0;
}
void Framework::OnSubWindowClosed(Uint32 sdlWindowID)
{
    // Casting ke SceneBoss untuk mengakses fungsi spesifiknya
    SceneBoss* boss = dynamic_cast<SceneBoss*>(scene.get());
    if (boss) {
        boss->CloseSubWindowBySDLID(sdlWindowID);
    }
}
