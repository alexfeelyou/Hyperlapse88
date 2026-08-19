#include "Boss.h"
#include "WindowTrackingSystem.h"
#include "System/Graphics.h"
#include "System/Sprite.h"
#include <SDL3/SDL.h>
#include <algorithm>

using namespace DirectX;

// ============================================================
// Constructor / Destructor
// ============================================================

Boss::Boss() {}

Boss::~Boss() {
    if (m_currentPhase)
        m_currentPhase->Exit(this);
}

// ============================================================
// Lifecycle
// ============================================================
void Boss::Initialize(WindowTrackingSystem* windowSystem) {
    m_windowSystem = windowSystem;
    auto device = Graphics::Instance().GetDevice();

    m_faceSprite = std::make_unique<Sprite>(device, "Data/Sprite/Boss/Sprite_Boss_Face_01.png");
    InitializeFaceGrid(device);

    // Material name pool used to randomly glitch the OS window title bar
    m_glitchTitles = {
        "mat_grass.png",
        "mat_stone.png",
        "mat_water.png",
        "mat_wood.png",
        "mat_metal.png",
        "mat_glass.png",
        "mat_magma.png",
        "mat_obsidian.png",
        "mat_error_null.png",
        "mat_fallback.png"
    };
}

void Boss::SpawnHeadWindow() {
    if (!m_windowSystem) return;

    // Register the boss head as a tracked window
    TrackedWindowConfig headCfg = { "navi_head", m_currentTitle, (int)m_windowSize.x, (int)m_windowSize.y, 2 };
    headCfg.role = WindowRole::TRACKED_ENTITY;
    
    headCfg.isAlwaysOnTop = true;
    
    m_windowSystem->AddTrackedWindow(
        headCfg,
        [this]() { return m_position; },
        [this]() { return m_windowSize; }
    );

    auto* headWin = m_windowSystem->GetTrackedWindow("navi_head");
    if (headWin) {
        m_naviWindow = headWin->window;
        m_naviCamera = headWin->camera;
        m_naviWindow->SetDraggable(false);
        m_naviWindow->SetClickThrough(true);
    }

    // Lock the OS window handle and configure its style
    m_hHeadWindow = FindWindowA(nullptr, m_currentTitle.c_str());
    if (m_hHeadWindow) {
        // Remove maximize/minimize buttons
        LONG style = GetWindowLong(m_hHeadWindow, GWL_STYLE);
        style &= ~(WS_MAXIMIZEBOX | WS_MINIMIZEBOX);
        SetWindowLong(m_hHeadWindow, GWL_STYLE, style);

        // Disable the close button
        HMENU hMenu = GetSystemMenu(m_hHeadWindow, FALSE);
        if (hMenu)
            EnableMenuItem(hMenu, SC_CLOSE, MF_BYCOMMAND | MF_DISABLED | MF_GRAYED);

        // Make the window layered + click-through at OS level
        LONG exStyle = GetWindowLong(m_hHeadWindow, GWL_EXSTYLE);
        exStyle |= WS_EX_LAYERED | WS_EX_TRANSPARENT;
        SetWindowLong(m_hHeadWindow, GWL_EXSTYLE, exStyle);

        // Force always-on-top
        SetWindowPos(m_hHeadWindow, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_FRAMECHANGED);
    }
}

// ============================================================
// Phase Management
// ============================================================

void Boss::ChangePhase(std::unique_ptr<INaviPhase> newPhase) {
    if (m_currentPhase)
        m_currentPhase->Exit(this);

    m_currentPhase = std::move(newPhase);

    if (m_currentPhase)
        m_currentPhase->Enter(this);
}

// ============================================================
// Update
// ============================================================

void Boss::Update(float dt) {
    m_glitchTimer += dt;

    if (m_windowSystem)
        m_pixelToUnit = m_windowSystem->GetPixelToUnitRatio();

    UpdateFaceGlitch(dt);

    // Breathing animation: oscillate window size
    float breath = sinf(m_glitchTimer * m_breathSpeed) * m_breathIntensity;
    m_windowSize.x = m_baseWindowSize.x + breath;
    m_windowSize.y = m_baseWindowSize.y + breath;

    if (m_currentPhase)
        m_currentPhase->Update(dt, this);
}

// ============================================================
// Render
// ============================================================

void Boss::Render(ID3D11DeviceContext* context, Camera* currentCamera) {
    if (!currentCamera) return;

    // Only render face content when using the boss's own camera
    if (currentCamera == m_naviCamera.get()) {
        if (m_naviWindow)
            RenderFaceGrid(context, currentCamera);

        if (m_isFaceSpriteVisible && m_faceSprite && m_naviWindow) {
            float unitW = m_windowSize.x / m_pixelToUnit;
            float unitH = m_windowSize.y / m_pixelToUnit;
            m_faceSprite->Render(
                context, currentCamera,
                m_position.x, m_position.y + 0.02f, m_position.z,
                unitW, unitH,
                DirectX::XMConvertToRadians(90.0f), 0.0f, 0.0f,
                1.0f, 1.0f, 1.0f, 1.0f
            );
        }
    }

    if (m_currentPhase)
        m_currentPhase->Render(context, currentCamera, this);
}

// ============================================================
// Window Title
// ============================================================

void Boss::SetWindowTitle(const std::string& newTitle) {
    m_currentTitle = newTitle;
    if (m_hHeadWindow)
        SetWindowTextA(m_hHeadWindow, m_currentTitle.c_str());
}

// ============================================================
// Face Grid Setup
// ============================================================

void Boss::InitializeFaceGrid(ID3D11Device* device) {
    m_faceTextures.clear();

    // Load all Sprite_Boss_XX.png source textures
    int totalSourceTextures = 14;
    for (int i = 1; i <= totalSourceTextures; ++i) {
        std::string numStr = (i < 10) ? "0" + std::to_string(i) : std::to_string(i);
        m_faceTextures.push_back(
            std::make_unique<Sprite>(device, ("Data/Sprite/Boss/Sprite_Boss_" + numStr + ".png").c_str())
        );
    }

    SetGridResolution(m_faceParams.gridResolution);
}

void Boss::SetGridResolution(int res) {
    m_faceParams.gridResolution = res;
    m_faceGrid.assign(res, std::vector<FaceTile>(res));
    RandomizeFaceGrid();
}

void Boss::RandomizeFaceGrid() {
    if (m_faceTextures.empty()) return;

    int res = m_faceParams.gridResolution;
    float diff = m_faceParams.maxInterval - m_faceParams.minInterval;

    for (int r = 0; r < res; ++r) {
        for (int c = 0; c < res; ++c) {
            auto& tile = m_faceGrid[r][c];
            tile.texIdx = rand() % (int)m_faceTextures.size();
            tile.interval = m_faceParams.minInterval + ((rand() % 1000) / 1000.0f) * max(0.001f, diff);
            tile.timer = ((rand() % 100) / 100.0f) * tile.interval;
            tile.size = 1;
        }
    }
}

// ============================================================
// Face Grid Update (Glitch Logic)
// ============================================================

void Boss::UpdateFaceGlitch(float dt) {
    m_breathTimer += dt;
    if (m_faceTextures.empty() || !m_faceParams.enableGlitch) return;

    int res = m_faceParams.gridResolution;
    float diff = m_faceParams.maxInterval - m_faceParams.minInterval;
    bool triggerTitleChange = false;

    for (int r = 0; r < res; ++r) {
        for (int c = 0; c < res; ++c) {
            auto& tile = m_faceGrid[r][c];
            tile.timer += dt;

            // Random per-frame flicker
            if (m_faceParams.flickerChance > 0.0f &&
                ((rand() % 10000) / 100.0f) < m_faceParams.flickerChance)
            {
                tile.texIdx = rand() % (int)m_faceTextures.size();
            }

            // Interval tick: swap texture and randomize next interval
            if (tile.timer >= tile.interval) {
                tile.timer = 0.0f;
                tile.texIdx = rand() % (int)m_faceTextures.size();
                tile.interval = m_faceParams.minInterval + ((rand() % 1000) / 1000.0f) * max(0.001f, diff);

                // Chance to become a 2x2 merged tile
                if (r < res - 1 && c < res - 1 && (rand() % 100) < m_faceParams.chance2x2)
                    tile.size = 2;
                else
                    tile.size = 1;

                // Chance to glitch the tile's tint color
                if ((rand() % 100) < m_faceParams.colorGlitchChance) {
                    tile.color.x = 0.2f + ((rand() % 70) / 100.0f);
                    tile.color.y = 0.2f + ((rand() % 70) / 100.0f);
                    tile.color.z = 0.2f + ((rand() % 70) / 100.0f);
                }
                else {
                    tile.color = { 1.0f, 1.0f, 1.0f };
                }

                triggerTitleChange = true;
            }
        }
    }

    // Occasionally glitch the OS window title bar to a random material name (30% chance per tick)
    if (triggerTitleChange && !m_glitchTitles.empty()) {
        if ((rand() % 100) < 30) {
            int randIdx = rand() % (int)m_glitchTitles.size();
            SetWindowTitle(m_glitchTitles[randIdx]);
        }
    }
}

// ============================================================
// Face Grid Render (Batched by texture to minimize draw calls)
// ============================================================

void Boss::RenderFaceGrid(ID3D11DeviceContext* context, Camera* currentCamera) {
    if (!currentCamera || m_faceTextures.empty()) return;

    int res = m_faceParams.gridResolution;
    DirectX::XMFLOAT3 bossPos = this->GetPosition();
    float currentWindowUnitSize = m_windowSize.x / m_pixelToUnit;
    float dynamicFaceSize = currentWindowUnitSize * (m_faceParams.faceTotalSize / 5.0f);

    int currentLimit = std::clamp((int)m_currentGridLimit, 1, res);
    float baseTileSize = dynamicFaceSize / currentLimit;
    float startOffset = -dynamicFaceSize * 0.5f + baseTileSize * 0.5f;

    // Batch tiles per texture to reduce state changes
    for (size_t texIdx = 0; texIdx < m_faceTextures.size(); ++texIdx) {
        std::vector<Sprite::Sprite3DBatchData> batchData;

        for (int r = 0; r < currentLimit; ++r) {
            for (int c = 0; c < currentLimit; ++c) {
                // Map display cell to source grid cell proportionally
                int srcR = (r * res) / currentLimit;
                int srcC = (c * res) / currentLimit;
                const auto& tile = m_faceGrid[srcR][srcC];

                if (tile.texIdx == static_cast<int>(texIdx)) {
                    batchData.push_back({
                        bossPos.x + startOffset + (c * baseTileSize),
                        bossPos.y + 0.01f,
                        bossPos.z + startOffset + (r * baseTileSize),
                        baseTileSize, baseTileSize, 0.0f, 0.0f, 0.0f, 0.0f,
                        DirectX::XMConvertToRadians(90.0f), 0.0f, 0.0f,
                        tile.color.x, tile.color.y, tile.color.z, 1.0f
                        });
                }
            }
        }

        if (!batchData.empty())
            m_faceTextures[texIdx]->Render3DBatch(context, currentCamera, batchData);
    }
}