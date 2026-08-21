#include "WindowTrackingSystem.h"
#include "WindowManager.h"
#include <cmath>
#include "PerformanceLogger.h"
#include <CameraController.h>
#include <SDL3/SDL.h>

using namespace DirectX;

WindowTrackingSystem::WindowTrackingSystem()
{
    // Pre-cache screen dimensions agar tidak hit SDL setiap frame awal
    SDL_Rect bounds;
    if (SDL_GetDisplayBounds(SDL_GetPrimaryDisplay(), &bounds))
    {
        m_cachedScreenWidth = bounds.w;
        m_cachedScreenHeight = bounds.h;
    }
}

WindowTrackingSystem::~WindowTrackingSystem()
{
    ClearAll();
}

void WindowTrackingSystem::ClearAll()
{
    // Gunakan iterator untuk menghapus dengan aman
    for (auto it = m_trackedWindows.begin(); it != m_trackedWindows.end(); )
    {
        // PENTING: Jangan pernah hancurkan MAIN_VIEWPORT!
        if ((*it)->role != WindowRole::MAIN_VIEWPORT)
        {
            if ((*it)->window) {
                WindowManager::Instance().DestroyWindow((*it)->window);
            }
            m_windowLookup.erase((*it)->name);
            it = m_trackedWindows.erase(it);
        }
        else
        {
            ++it; // Lewati Main Window
        }
    }
}

bool WindowTrackingSystem::AddTrackedWindow(
    const TrackedWindowConfig& config,
    std::function<DirectX::XMFLOAT3()> getTargetPos,
    std::function<DirectX::XMFLOAT2()> getTargetSize // Parameter baru
)
{
    auto start = std::chrono::high_resolution_clock::now();

    // =========================================================
    // [1] CEK POOL: DAUR ULANG WINDOW YANG TIDUR
    // =========================================================
    for (auto& tw : m_trackedWindows) {
        if (!tw->isActive && tw->isTransparent == config.isTransparent) {
            tw->isActive = true;
            tw->name = config.name;
            tw->role = config.role;
            tw->trackingOffset = config.trackingOffset;
            tw->getTargetPositionFunc = getTargetPos;
            tw->getTargetSizeFunc = getTargetSize;

            tw->state.targetW = (float)config.width;
            tw->state.targetH = (float)config.height;
            tw->state.actualW = config.width;
            tw->state.actualH = config.height;

            if (tw->window && tw->window->GetSDLWindow()) {
                SDL_Window* sdlWin = tw->window->GetSDLWindow();
                SDL_SetWindowTitle(sdlWin, config.title.c_str());
                SDL_SetWindowSize(sdlWin, config.width, config.height);
                tw->window->SetBackgroundAlpha(config.isTransparent ? 0.0f : 1.0f);
                tw->window->SetPriority(config.priority);
                tw->window->SetAlwaysOnTop(config.isAlwaysOnTop);

                if (config.fpsLimit > 0.0f) tw->window->SetTargetFPS(config.fpsLimit);
                if (config.name == "player") tw->window->SetDraggable(false);

                // Set Posisi Awal agar tidak nge-blink dari koordinat -10000
                if (getTargetPos) {
                    DirectX::XMFLOAT3 initialPos = getTargetPos();
                    float screenX, screenY;
                    WorldToScreenPos(initialPos, screenX, screenY);
                    tw->state.targetX = screenX - (config.width * 0.5f);
                    tw->state.targetY = screenY - (config.height * 0.5f);
                    tw->state.actualX = static_cast<int>(roundf(tw->state.targetX));
                    tw->state.actualY = static_cast<int>(roundf(tw->state.targetY));
                    SDL_SetWindowPosition(sdlWin, tw->state.actualX, tw->state.actualY);
                }

                SDL_ShowWindow(sdlWin); // Bangunkan window!
            }

            m_windowLookup[config.name] = tw.get(); // Masukkan kembali ke lookup
            WindowManager::Instance().MarkPriorityDirty();

            auto end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<float, std::milli> duration = end - start;
            PerformanceLogger::Instance().LogWindowAction("Pooled", config.name, duration.count());

            return true;
        }
    }

    // =========================================================
    // [2] JIKA POOL KOSONG: BUAT BARU (Kode Lama Anda Lanjut Di Sini)
    // =========================================================
    // 1. Create Window via Singleton Manager
    Beyond::Window* window = WindowManager::Instance().CreateGameWindow(
        config.title.c_str(),
        config.width,
        config.height,
        config.isTransparent
    );

    if (!window) return false;

    window->SetPriority(config.priority);
    window->SetAlwaysOnTop(config.isAlwaysOnTop);
    SDL_ShowWindow(window->GetSDLWindow());
    WindowManager::Instance().MarkPriorityDirty();

    if (config.fpsLimit > 0.0f)
    {
        window->SetTargetFPS(config.fpsLimit);
    }

    // Hardcoded logic: Player window usually shouldn't be draggable by mouse
    if (config.name == "player") window->SetDraggable(false);

    // 2. Create Camera for this window
    auto camera = std::make_shared<Camera>();
    window->SetCamera(camera.get());

    // 3. Setup Tracked Object
    auto tracked = std::make_unique<TrackedWindow>();
    tracked->isTransparent = config.isTransparent; // [NEW] Catat jenisnya di sini
    tracked->name = config.name;
    tracked->window = window;
    tracked->role = config.role;
    tracked->camera = camera;
    tracked->trackingOffset = config.trackingOffset;
    tracked->getTargetPositionFunc = getTargetPos;
    tracked->getTargetSizeFunc = getTargetSize;

    tracked->state.targetW = (float)config.width;
    tracked->state.targetH = (float)config.height;
    tracked->state.actualW = config.width;
    tracked->state.actualH = config.height;

    // 4. Initial Position Setup
    if (getTargetPos)
    {
        XMFLOAT3 initialPos = getTargetPos();
        float screenX, screenY;
        WorldToScreenPos(initialPos, screenX, screenY);

        tracked->state.targetX = screenX - (window->GetWidth() * 0.5f);
        tracked->state.targetY = screenY - (window->GetHeight() * 0.5f);
        tracked->state.actualX = static_cast<int>(roundf(tracked->state.targetX));
        tracked->state.actualY = static_cast<int>(roundf(tracked->state.targetY));

        SDL_SetWindowPosition(window->GetSDLWindow(), tracked->state.actualX, tracked->state.actualY);
    }

    m_windowLookup[config.name] = tracked.get();
    m_trackedWindows.push_back(std::move(tracked));

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<float, std::milli> duration = end - start;
    PerformanceLogger::Instance().LogWindowAction("Spawned", config.name, duration.count());

    return true;
}

TrackedWindow* WindowTrackingSystem::GetTrackedWindow(const std::string& name)
{
    auto it = m_windowLookup.find(name);
    if (it != m_windowLookup.end()) return it->second;
    return nullptr;
}

void WindowTrackingSystem::Update(float dt)
{
    // 1. Update Screen Cache (Resolusi layar mungkin berubah)
    m_cacheUpdateTimer += dt;
    if (m_cacheUpdateTimer >= 1.0f)
    {
        m_cachedScreenWidth = GetSystemMetrics(SM_CXSCREEN);
        m_cachedScreenHeight = GetSystemMetrics(SM_CYSCREEN);
        m_cacheUpdateTimer = 0.0f;
    }

    // 2. Update Semua Window
    for (auto& tracked : m_trackedWindows)
    {
        if (!tracked->isActive) continue;
        UpdateSingleWindow(dt, *tracked);
    }
}

void WindowTrackingSystem::UpdateSingleWindow(float dt, TrackedWindow& tracked)
{
    if (!tracked.window || !tracked.camera || !tracked.getTargetPositionFunc)
        return;

    // 1. AMBIL POSISI OS SAAT INI (Kunci Anti-Ghosting / DWM Lag)
    int osX, osY, osW, osH;
    SDL_GetWindowPosition(tracked.window->GetSDLWindow(), &osX, &osY);
    SDL_GetWindowSize(tracked.window->GetSDLWindow(), &osW, &osH);

    bool isBeingDragged = false;
    if (osW != tracked.state.actualW || osH != tracked.state.actualH)
    {
        tracked.state.actualW = osW;
        tracked.state.actualH = osH;
        tracked.state.targetW = (float)osW; // Update target juga agar tidak snap balik
        tracked.state.targetH = (float)osH;
    }

    // 2. UPDATE SIZE
    if (tracked.getTargetSizeFunc)
    {
        DirectX::XMFLOAT2 desiredSize = tracked.getTargetSizeFunc();
        float tSize = min(m_followSpeed * dt, 1.0f);
        tracked.state.targetW += (desiredSize.x - tracked.state.targetW) * tSize;
        tracked.state.targetH += (desiredSize.y - tracked.state.targetH) * tSize;

        int newW = max(10, static_cast<int>(roundf(tracked.state.targetW)));
        int newH = max(10, static_cast<int>(roundf(tracked.state.targetH)));
        int deltaW = abs(newW - osW);
        int deltaH = abs(newH - osH);
        if (deltaW >= 1 || deltaH >= 1)  // sudah ada, ini OK, tapi tambahkan:
        {
            SDL_SetWindowSize(tracked.window->GetSDLWindow(), newW, newH);
            tracked.state.actualW = newW;
            tracked.state.actualH = newH;
        }
    }

    // 3. UPDATE POSITION LOGIC
    if (!isBeingDragged && tracked.role == WindowRole::TRACKED_ENTITY)
    {
        DirectX::XMFLOAT3 targetWorldPos = tracked.getTargetPositionFunc();
        targetWorldPos.x += tracked.trackingOffset.x;
        targetWorldPos.y += tracked.trackingOffset.y;
        targetWorldPos.z += tracked.trackingOffset.z;

        float targetScreenX, targetScreenY;
        WorldToScreenPos(targetWorldPos, targetScreenX, targetScreenY);

        float destX = targetScreenX - (tracked.state.actualW * 0.5f);
        float destY = targetScreenY - (tracked.state.actualH * 0.5f);

        float tPos = min(m_followSpeed * dt, 1.0f);
        tracked.state.targetX += (destX - tracked.state.targetX) * tPos;
        tracked.state.targetY += (destY - tracked.state.targetY) * tPos;

        int newX = static_cast<int>(roundf(tracked.state.targetX));
        int newY = static_cast<int>(roundf(tracked.state.targetY));

        // =========================================================
        // [FIX MUTLAK] OS WINDOW SHAKE (GETARAN FISIK JENDELA)
        // =========================================================
        float trauma = CameraController::Instance().GetTrauma();

        if (trauma > 0.01f) {
            float shakeIntensity = trauma * trauma; // Pangkat 2 untuk natural falloff
            float maxShakePixels = 35.0f; // Jarak loncatan maksimal jendela di monitor!

            // RNG dari -1.0 sampai 1.0
            float randX = ((rand() % 200) / 100.0f) - 1.0f;
            float randY = ((rand() % 200) / 100.0f) - 1.0f;

            int shakeOffsetX = static_cast<int>(randX * shakeIntensity * maxShakePixels);
            int shakeOffsetY = static_cast<int>(randY * shakeIntensity * maxShakePixels);

            // Terapkan getaran langsung ke OS Windows!
            SDL_SetWindowPosition(tracked.window->GetSDLWindow(), newX + shakeOffsetX, newY + shakeOffsetY);

            // PENTING: Biarkan actualX/Y memegang koordinat 'newX' aslinya.
            // Ini menjamin saat getaran selesai, jendela langsung snap-back ke jalur aslinya!
            tracked.state.actualX = newX;
            tracked.state.actualY = newY;
        }
        else {
            // --- LOGIKA NORMAL GUARD (Tanpa Getaran) ---
            float deltaX = fabsf(tracked.state.targetX - tracked.state.actualX);
            float deltaY = fabsf(tracked.state.targetY - tracked.state.actualY);

            if ((newX != tracked.state.actualX || newY != tracked.state.actualY)
                && (deltaX >= 0.5f || deltaY >= 0.5f))
            {
                SDL_SetWindowPosition(tracked.window->GetSDLWindow(), newX, newY);
                tracked.state.actualX = newX;
                tracked.state.actualY = newY;
            }
        }
    }

    // 4. THE MAGIC FIX: Proyeksikan 3D menggunakan posisi OS Asli, BUKAN target.
    UpdateOffCenterProjection(tracked.camera.get(), osX, osY, tracked.state.actualW, tracked.state.actualH, GetUnifiedCameraHeight());
}

void WindowTrackingSystem::UpdateOffCenterProjection(Camera* targetCam, int winX, int winY, int winW, int winH, float camHeight)
{
    int screenW, screenH;
    GetScreenDimensions(screenW, screenH);

    // [FIX 3] Gunakan posisi kamera saat ini (yang sudah mengandung Shake dari CameraController)
    // Jangan dipaksa ke (0, camHeight, 0) agar getarannya sinkron di semua jendela.
    DirectX::XMFLOAT3 currentPos = targetCam->GetPosition();
    targetCam->SetPosition(currentPos);

    // Pastikan arah hadap kamera portal selalu tegak lurus ke bawah (Top-Down)
    // agar proyeksi portal tetap konsisten dengan koordinat Desktop.
    targetCam->LookAt({ currentPos.x, 0.0f, currentPos.z });

    float nearZ = 0.1f;
    float farZ = 1000.0f;
    float halfFovTan = tanf(DirectX::XMConvertToRadians(m_fov) * 0.5f);

    float halfHeight = nearZ * halfFovTan;
    float halfWidth = halfHeight * ((float)screenW / screenH);

    double screenWd = (double)screenW;
    double screenHd = (double)screenH;

    float l = (float)((winX / screenWd) * 2.0 - 1.0);
    float r = (float)(((winX + winW) / screenWd) * 2.0 - 1.0);
    float t = (float)(1.0 - (winY / screenHd) * 2.0);
    float b = (float)(1.0 - ((winY + winH) / screenHd) * 2.0);

    targetCam->SetOffCenterProjection(l * halfWidth, r * halfWidth, b * halfHeight, t * halfHeight, nearZ, farZ);
}
// =========================================================
// MATH HELPERS
// =========================================================
void WindowTrackingSystem::GetScreenDimensions(int& outWidth, int& outHeight)
{
    if (m_cachedScreenWidth > 0) {
        outWidth = m_cachedScreenWidth;
        outHeight = m_cachedScreenHeight;
        return;
    }

    // SDL3: Pengganti GetSystemMetrics(SM_CXSCREEN)
    SDL_Rect bounds;
    if (SDL_GetDisplayBounds(SDL_GetPrimaryDisplay(), &bounds)) {
        outWidth = bounds.w;
        outHeight = bounds.h;

        m_cachedScreenWidth = outWidth;
        m_cachedScreenHeight = outHeight;
    }
}

void WindowTrackingSystem::WorldToScreenPos(const DirectX::XMFLOAT3& worldPos, float& outScreenX, float& outScreenY)
{
    int screenW, screenH;
    GetScreenDimensions(screenW, screenH);

    // Konversi sederhana: Tengah layar + (Posisi World * Ratio)
    // Note: Z world menjadi Y layar (negatif) karena coordinate system game ini
    outScreenX = (screenW * 0.5f) + (worldPos.x * m_pixelToUnitRatio);
    outScreenY = (screenH * 0.5f) - (worldPos.z * m_pixelToUnitRatio);
}

float WindowTrackingSystem::GetUnifiedCameraHeight()
{
    int screenW, screenH;
    GetScreenDimensions(screenW, screenH);
    float halfFovTan = tanf(XMConvertToRadians(m_fov) * 0.5f);
    // Rumus trigonometri untuk mencari ketinggian kamera agar 1 unit world = X pixel layar
    return (screenH * 0.5f) / (m_pixelToUnitRatio * halfFovTan);
}

void WindowTrackingSystem::RemoveTrackedWindow(const std::string& name)
{
    // 1. Cek apakah window ada di lookup table
    auto it = m_windowLookup.find(name);
    if (it == m_windowLookup.end()) return; // Tidak ketemu, keluar.

    TrackedWindow* trackedInfo = it->second;

    // 2. [POOLING LOGIC] Sembunyikan window fisik, jangan dihancurkan!
    if (trackedInfo && trackedInfo->window && trackedInfo->window->GetSDLWindow())
    {
        SDL_Window* sdlWin = trackedInfo->window->GetSDLWindow();
        SDL_HideWindow(sdlWin);
        SDL_SetWindowPosition(sdlWin, -10000, -10000); // Lempar jauh dari layar

        trackedInfo->isActive = false;
        trackedInfo->name = "POOL_" + name; // Ganti nama agar tidak bentrok
    }

    // 3. Hapus dari Map Lookup saja agar ID-nya bisa dipakai lagi
    m_windowLookup.erase(it);

    // CATATAN: Kita TIDAK melakukan erase pada m_trackedWindows 
    // agar objeknya tetap hidup sebagai pool.
}

void WindowTrackingSystem::RegisterWindow(Beyond::Window* window, WindowRole role, std::shared_ptr<Camera> camera)
{
    if (!window) return;

    auto tracked = std::make_unique<TrackedWindow>();
    tracked->name = "main_window"; // ID statis agar mudah dicari
    tracked->window = window;
    tracked->role = role;
    tracked->camera = camera;

    // Berikan fungsi dummy (kosong) agar tidak crash saat di-update
    tracked->getTargetPositionFunc = []() { return DirectX::XMFLOAT3(0, 0, 0); };

    // Ambil data posisi & ukuran awal dari OS Windows
    int osX, osY, osW, osH;
    SDL_GetWindowPosition(window->GetSDLWindow(), &osX, &osY);
    SDL_GetWindowSize(window->GetSDLWindow(), &osW, &osH);

    tracked->state.actualX = osX;
    tracked->state.actualY = osY;
    tracked->state.actualW = osW;
    tracked->state.actualH = osH;
    tracked->state.targetX = (float)osX;
    tracked->state.targetY = (float)osY;
    tracked->state.targetW = (float)osW;
    tracked->state.targetH = (float)osH;

    m_windowLookup[tracked->name] = tracked.get();
    m_trackedWindows.push_back(std::move(tracked));
}

void WindowTrackingSystem::UpdateWindowBounds(int windowIndex, int width, int height)
{
    if (windowIndex == 0) // Index 0 adalah Main Window
    {
        TrackedWindow* tracked = GetTrackedWindow("main_window");
        if (tracked)
        {
            tracked->state.actualW = width;
            tracked->state.actualH = height;
            tracked->state.targetW = (float)width;
            tracked->state.targetH = (float)height;
        }
    }
}

std::unique_ptr<TrackedWindow> WindowTrackingSystem::ExtractForPool(const std::string& name)
{
    auto it = m_windowLookup.find(name);
    if (it == m_windowLookup.end()) return nullptr;

    auto vecIt = std::find_if(m_trackedWindows.begin(), m_trackedWindows.end(),
        [&name](const std::unique_ptr<TrackedWindow>& p) {
            return p->name == name;
        });

    if (vecIt != m_trackedWindows.end())
    {
        std::unique_ptr<TrackedWindow> extracted = std::move(*vecIt);
        m_trackedWindows.erase(vecIt);
        m_windowLookup.erase(it);
        return extracted;
    }
    return nullptr;
}

void WindowTrackingSystem::RestoreFromPool(std::unique_ptr<TrackedWindow> window)
{
    if (!window) return;
    m_windowLookup[window->name] = window.get();
    m_trackedWindows.push_back(std::move(window));
}