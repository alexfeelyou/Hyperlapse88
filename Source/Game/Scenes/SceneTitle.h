#pragma once

#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <algorithm> 

#include "Scene.h"
#include "Camera.h"
#include "UIOption.h"
#include "System/Gamepad.h"
#include "System/AudioManager.h"
#include "System/Sprite.h" 
#include "PostProcessManager.h"
#include "SceneGame.h"
#include "Framework.h"
#include "Primitive.h"
#include "ResourceManager.h"
#include "System/Input.h"
#include "System/Graphics.h"
#include <imgui.h>

class SceneTitle : public Scene
{
public:
    SceneTitle();
    ~SceneTitle() override = default;

    // Core Loop
    void Update(float elapsedTime) override;
    void Render(float dt, Camera* camera = nullptr) override;
    void OnResize(int width, int height) override;

    // Debug / Tools
    void DrawGUI() override;
    Camera* GetCamera() const { return camera.get(); }

private:
    // --- Subsystems ---
    std::unique_ptr<Camera> camera{};
    std::unique_ptr<Sprite> bgSprite{};
    std::unique_ptr<Sprite> logoSprite{};
    std::unique_ptr<Sprite> copyrightSprite{};
    std::unique_ptr<PostProcessManager> postProcess{};

    // --- Post Process State ---
    struct PostProcessState {
        bool MasterEnabled{ true };
        bool EnableVignette{ false };
        bool EnableLens{ true };
        bool EnableChromatic{ true };
        bool EnableCRT{ true };
        bool EnableBloom{ true };
        bool EnablePSX{ true };
    };

    PostProcessState m_fxState{};
    UberShader::UberData m_uberParams{};

    // --- Sprites ---
    std::unique_ptr<Sprite> m_fadeSprite{};
    std::unique_ptr<Sprite> startSprite{};
    std::unique_ptr<Sprite> m_newGameSprite{};
    std::unique_ptr<Sprite> m_optionSprite{};
    std::unique_ptr<Sprite> m_exitSprite{};

	// --- UI Option Panel ---
    std::unique_ptr<UIOption> m_uiOption{};

    // --- Timers and Alphas ---
    float m_fadeAlpha{ 1.0f };
    float m_bootTimer{ 4.1f };
    float m_copyrightTimer{ 4.0f };
    float m_copyrightAlpha{ 1.0f };
    float m_startAlpha{ 0.0f };
    float m_pulseTimer{ 0.0f };
    float m_gapTimer{ 0.0f };

    // --- Menu Transition States ---
    float m_menuGapTimer{ 0.0f };
    float m_menuAlpha{ 0.0f };
    bool  m_isTransitioningMenu{ false };
    bool  m_isMenuPhase{ false };

	// --- Option Transition States ---
    bool m_isOptionPhase{ false };

    static constexpr float BOOT_FADE_DURATION{ 3.0f };
    static constexpr float GAP_DURATION{ 1.0f };

    bool m_isExiting{ false };
    float m_exitTimer{ 0.0f };

    static constexpr float FX_BASE_SMOOTHNESS{ 0.2f };
    static constexpr float FX_BASE_INTENSITY{ 0.38f };

    // --- Strict Menu States ---
    enum class MenuOption : std::uint8_t {
        NewGame = 0,
        Option,
        Exit,
        Count // Automatic bounds tracker
    };

    MenuOption m_currentSelection{ MenuOption::NewGame };
    std::unique_ptr<Primitive> m_primitive{};

    static constexpr float MENU_START_X = 868.5f;
    static constexpr float MENU_START_Y = 855.0f;
    static constexpr float MENU_ITEM_GAP = 12.0f;

    static constexpr float Y_NEW_GAME = MENU_START_Y;
    static constexpr float Y_OPTION = Y_NEW_GAME + 25.0f + MENU_ITEM_GAP;
    static constexpr float Y_EXIT = Y_OPTION + 32.0f + MENU_ITEM_GAP;

    static constexpr float CURSOR_WIDTH = 16.0f;
    static constexpr float CURSOR_HEIGHT = 16.0f;
    static constexpr float CURSOR_OFFSET_X = 25.0f; // Gap between cursor and text

    static constexpr float CURSOR_SMOOTH_SPEED{ 16.0f }; // Higher value = snappier, Lower = smoother
    static constexpr float COLOR_SMOOTH_SPEED{ 12.0f };

    // --- Modern Frame-Rate Independent Animation Tracks ---
    float m_visualCursorY{ 0.0f };
    bool  m_isCursorInitialized{ false };

    // Smooth interpolators for option text colors (0.0f = Gray, 1.0f = White)
    std::array<float, 3> m_optionWeights{ 1.0f, 0.0f, 0.0f };

    // --- Private Render Helpers ---
    void AnimateMenu(float elapsedTime);
    void RenderMenuOptions(ID3D11DeviceContext* dc);
    void ExecuteMenuSelection() noexcept;

    // --- Input Abstraction Helpers ---
    // Evaluates Keyboard, D-Pad, and Debounced Analog Stick natively
    [[nodiscard]] bool IsUpTriggered() noexcept;
    [[nodiscard]] bool IsDownTriggered() noexcept;
    [[nodiscard]] bool IsConfirmTriggered(bool allowSpace = true) noexcept;

    // --- Analog Stick State Tracking (Debounce) ---
    // Prevents "hyper-scrolling" when holding the analog stick
    bool m_analogUpWasPressed{ false };
    bool m_analogDownWasPressed{ false };

    // Deadzone threshold for the thumbstick to register as an intentional push
    static constexpr float THUMBSTICK_THRESHOLD{ 0.5f };

    // --- Debug GUI Helpers ---
    void GUIPostProcessTab();
};