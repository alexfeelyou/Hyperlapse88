#include "SceneTitle.h"

namespace {
    [[nodiscard]] constexpr float CustomLerp(float a, float b, float t) noexcept
    {
        return (1.0f - t) * a + t * b;
    }
}

SceneTitle::SceneTitle()
{
    camera = std::make_unique<Camera>();
    camera->SetOrthographic(1920.0f, 1080.0f, 0.1f, 1000.0f);
    camera->SetPosition(0.0f, 0.0f, -10.0f);

    float screenW = 1920.0f;
    float screenH = 1080.0f;
    if (const auto* window = Framework::Instance()->GetMainWindow())
    {
        screenW = static_cast<float>(window->GetWidth());
        screenH = static_cast<float>(window->GetHeight());
    }

    postProcess = std::make_unique<PostProcessManager>();
    postProcess->Initialize(static_cast<int>(screenW), static_cast<int>(screenH));
    postProcess->SetEnabled(true);

    // Automatically load this scene's unique post-process profile on boot
    postProcess->LoadConfig(GetPostProcessProfilePath());

    // Load Assets
    auto device = Graphics::Instance().GetDevice();
    bgSprite = std::make_unique<Sprite>(device, "Data/Sprite/Scene Title/Back_Title.png");
    logoSprite = std::make_unique<Sprite>(device, "Data/Sprite/Scene Title/Sprite_Title_Logo.png");
    copyrightSprite = std::make_unique<Sprite>(device, "Data/Sprite/Scene Title/Sprite_Title_Copyright.png");
    startSprite = std::make_unique<Sprite>(device, "Data/Sprite/Scene Title/Sprite_Title_Start.png");
    m_fadeSprite = std::make_unique<Sprite>(device, "Data/Sprite/Scene Game/Black.png");

    // Load New Menu Assets
    m_newGameSprite = std::make_unique<Sprite>(device, "Data/Sprite/Scene Title/Sprite_Title_Newgame.png");
    m_optionSprite = std::make_unique<Sprite>(device, "Data/Sprite/Scene Title/Sprite_Title_Option.png");
    m_exitSprite = std::make_unique<Sprite>(device, "Data/Sprite/Scene Title/Sprite_Title_Exit.png");
    m_primitive = std::make_unique<Primitive>(device);
    m_uiOption = std::make_unique<UIOption>();
    m_uiOption->Initialize(m_primitive.get());
}

bool SceneTitle::IsUpTriggered() noexcept
{
    auto& input = Input::Instance();
    auto& keyboard = input.GetKeyboard();
    auto& gamePad = input.GetGamePad();

    // Keyboard Check
    const bool isKeyboardUp = keyboard.IsTriggered('W') || keyboard.IsTriggered(VK_UP);

    // GamePad D-Pad Check 
    const bool isGamepadDpadUp = (gamePad.GetButtonDown() & GamePad::BTN_UP) != 0;

    // GamePad Analog Stick Check & Debouncing
    const float leftY = gamePad.GetAxisLY();
    const bool isAnalogPushedUp = (leftY > THUMBSTICK_THRESHOLD);

    // Only true if pushed now, but wasn't pushed last frame
    const bool analogUpTriggered = (isAnalogPushedUp && !m_analogUpWasPressed);
    m_analogUpWasPressed = isAnalogPushedUp; 

	// Update global state for last used device
    if (isKeyboardUp) input.SetLastUsedDevice(InputDevice::Keyboard);
    if (isGamepadDpadUp || analogUpTriggered) input.SetLastUsedDevice(InputDevice::Gamepad);

    return isKeyboardUp || isGamepadDpadUp || analogUpTriggered;
}

bool SceneTitle::IsDownTriggered() noexcept
{
    auto& input = Input::Instance();
    auto& keyboard = input.GetKeyboard();
    auto& gamePad = input.GetGamePad();

    const bool isKeyboardDown = keyboard.IsTriggered('S') || keyboard.IsTriggered(VK_DOWN);
    const bool isGamepadDpadDown = (gamePad.GetButtonDown() & GamePad::BTN_DOWN) != 0;

    const float leftY = gamePad.GetAxisLY();
    const bool isAnalogPushedDown = (leftY < -THUMBSTICK_THRESHOLD);

    const bool analogDownTriggered = (isAnalogPushedDown && !m_analogDownWasPressed);
    m_analogDownWasPressed = isAnalogPushedDown;

	// Update global state for last used device
    if (isKeyboardDown) input.SetLastUsedDevice(InputDevice::Keyboard);
    if (isGamepadDpadDown || analogDownTriggered) input.SetLastUsedDevice(InputDevice::Gamepad);

    return isKeyboardDown || isGamepadDpadDown || analogDownTriggered;
}

bool SceneTitle::IsConfirmTriggered(bool allowSpace) noexcept
{
    // Natively handle single-frame trigger isolation
    auto& input = Input::Instance();

    bool isKeyboardConfirm = input.GetKeyboard().IsTriggered(VK_RETURN);
    if (allowSpace)
    {
        isKeyboardConfirm = isKeyboardConfirm || input.GetKeyboard().IsTriggered(VK_SPACE);
    }

    const bool isGamepadConfirm = (input.GetGamePad().GetButtonDown() & GamePad::BTN_A) != 0;

	// Update global state for last used device
    if (isKeyboardConfirm) input.SetLastUsedDevice(InputDevice::Keyboard);
    if (isGamepadConfirm) input.SetLastUsedDevice(InputDevice::Gamepad);

    return isKeyboardConfirm || isGamepadConfirm;
}

void SceneTitle::Update(float elapsedTime)
{
	// Exit Phase: Fade Out and Transition to Game Scene
    if (m_isExiting)
    {
        m_exitTimer += elapsedTime;
        m_fadeAlpha = std::clamp(m_exitTimer / BOOT_FADE_DURATION, 0.0f, 1.0f);

        if (m_exitTimer >= BOOT_FADE_DURATION)
        {
            Framework::Instance()->ChangeScene([]() { return std::make_unique<SceneGame>(); });
        }
        return;
    }

	// Boot Phase: Fade In Logo and Background
    if (m_bootTimer > 0.0f)
    {
        m_bootTimer -= elapsedTime;
        m_fadeAlpha = std::clamp(m_bootTimer / BOOT_FADE_DURATION, 0.0f, 1.0f);
        return;
    }

	// Copyright Phase: Display Copyright Notice
    if (m_copyrightTimer > 0.0f)
    {
        m_fadeAlpha = 0.0f;
        m_copyrightTimer -= elapsedTime;
        return;
    }

	// After the boot and copyright phases, we can safely set
    m_fadeAlpha = 0.0f;

    // Fade out Copyright
    if (m_copyrightAlpha > 0.0f)
    {
        m_copyrightAlpha = (std::max)(m_copyrightAlpha - (elapsedTime * 0.8f), 0.0f);
    }
    // Wait for Gap
    else if (m_gapTimer < GAP_DURATION)
    {
        m_gapTimer += elapsedTime;
    }
    // Idle Menu State & Navigation
    else if (m_isMenuPhase)
    {
        constexpr int maxOptions = static_cast<int>(MenuOption::Count);
        int current = static_cast<int>(m_currentSelection);

		// If the user is in the Option Panel, we do not want to process main menu navigation
        if (m_isOptionPhase)
        {
            // Allow the user to press Confirm or Exit (e.g. ESC) to close the menu
            if (IsConfirmTriggered() || Input::Instance().GetKeyboard().IsTriggered(VK_ESCAPE))
            {
                m_isOptionPhase = false;
            }

            if (m_uiOption) m_uiOption->Update(elapsedTime);

            return; // EXIT EARLY: Do not process main menu navigation
        }

		// Up navigation with wrap-around
        if (IsUpTriggered())
        {
            current = (current + maxOptions - 1) % maxOptions;
            m_currentSelection = static_cast<MenuOption>(current);
        }
		// Down navigation with wrap-around
        else if (IsDownTriggered())
        {
            current = (current + 1) % maxOptions;
            m_currentSelection = static_cast<MenuOption>(current);
        }
		// Confirm selection
        else if (IsConfirmTriggered())
        {
            ExecuteMenuSelection();
        }
    }
    // Transition State: Fading out Start, Fading in Menu
    else if (m_isTransitioningMenu)
    {
        if (m_startAlpha > 0.0f)
        {
            m_startAlpha = (std::max)(m_startAlpha - (elapsedTime * 2.0f), 0.0f);
        }
        else if (m_menuGapTimer < GAP_DURATION)
        {
            m_menuGapTimer += elapsedTime;
        }
        else
        {
            m_menuAlpha = (std::min)(m_menuAlpha + (elapsedTime * 2.0f), 1.0f);
            if (m_menuAlpha >= 1.0f)
            {
                m_isMenuPhase = true;
                m_isTransitioningMenu = false;
            }
        }
    }
    // Start Fade In
    else if (m_pulseTimer == 0.0f)
    {
        m_startAlpha = (std::min)(m_startAlpha + (elapsedTime * 2.0f), 1.0f);
        if (m_startAlpha >= 1.0f)
        {
            m_pulseTimer = 0.5236f;
        }
    }
    // Start Pulse & Input Listening
    else
    {
        m_pulseTimer += elapsedTime;
        m_startAlpha = 0.6f + 0.4f * sinf(m_pulseTimer * 3.0f);

        // Only trigger once. Setting flag locks out further presses automatically
        if (IsConfirmTriggered(false))
        {
            m_isTransitioningMenu = true;
            // Play SE here if needed: AudioManager::Instance().PlaySFX(...)
        }
    }

    // Visual simulation interpolation
    AnimateMenu(elapsedTime);
}

void SceneTitle::Render(float dt, Camera* targetCamera)
{
    auto dc = Graphics::Instance().GetDeviceContext();
    auto rs = Graphics::Instance().GetRenderState();

    if (postProcess->IsEnabled())
    {
        postProcess->BeginCapture();
    }
    else
    {
        ID3D11RenderTargetView* currentRTV = nullptr;
        ID3D11DepthStencilView* currentDSV = nullptr;
        dc->OMGetRenderTargets(1, &currentRTV, &currentDSV);

        if (currentRTV) {
            float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
            dc->ClearRenderTargetView(currentRTV, clearColor);
            currentRTV->Release();
        }
        if (currentDSV) {
            dc->ClearDepthStencilView(currentDSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
            currentDSV->Release();
        }
    }

    dc->OMSetBlendState(rs->GetBlendState(BlendState::Transparency), nullptr, 0xFFFFFFFF);
    dc->OMSetDepthStencilState(rs->GetDepthStencilState(DepthState::NoTestNoWrite), 0);
    dc->RSSetState(rs->GetRasterizerState(RasterizerState::SolidCullNone));

    float t = 1.0f - std::clamp(m_bootTimer / BOOT_FADE_DURATION, 0.0f, 1.0f);
    float bootAlpha = t * t;

    if (bgSprite) bgSprite->Render(dc, camera.get(), 0, 0, 0, 1920.0f, 1080.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, bootAlpha);
    float screenW{ UI::s_baseWidth };
    float screenH{ UI::s_baseHeight };

    if (const auto* window{ Framework::Instance()->GetMainWindow() })
    {
        screenW = static_cast<float>(window->GetWidth());
        screenH = static_cast<float>(window->GetHeight());
    }

    const auto logo{ UI::GetScaled(461.5f, 200.0f, 957.0f, 547.0f, screenW, screenH) };
    if (logoSprite) logoSprite->Render(dc, logo.x, logo.y, 0.0f, logo.w, logo.h, 0.0f, 0.0f, 997.0f, 547.0f, 0.0f, 1.0f, 1.0f, 1.0f, bootAlpha);

    const auto copy{ UI::GetScaled(327.5f, 867.0f, 1245.0f, 105.0f, screenW, screenH) };
    float finalCopyrightAlpha = (m_bootTimer > 0.0f) ? bootAlpha : m_copyrightAlpha;
    if (copyrightSprite && finalCopyrightAlpha > 0.0f) {
        copyrightSprite->Render(dc, copy.x, copy.y, 0.0f, copy.w, copy.h, 0.0f, 0.0f, 1265.0f, 105.0f, 0.0f, 1.0f, 1.0f, 1.0f, finalCopyrightAlpha);
    }

	// Render Start 
    const auto start{ UI::GetScaled(679.5f, 906.5f, 545.0f, 34.0f, screenW, screenH) };
    if (startSprite && m_startAlpha > 0.0f) {
        startSprite->Render(dc, start.x, start.y, 0.0f, start.w, start.h, 0.0f, 0.0f, 545.0f, 34.0f, 0.0f, 1.0f, 1.0f, 1.0f, m_startAlpha);
    }

	// Render Menu Options and Cursor
    if (m_menuAlpha > 0.0f)
    {
        RenderMenuOptions(dc, screenW, screenH);

        if (m_primitive)
        {
            const float targetY = m_visualCursorY;
            const float curX{ MENU_START_X - CURSOR_OFFSET_X };

            // Calculate original unscaled points
            const float x1{ curX };
            const float y1{ targetY - (CURSOR_HEIGHT * 0.5f) };
            const float x2{ curX + CURSOR_WIDTH };
            const float y2{ targetY };
            const float x3{ curX };
            const float y3{ targetY + (CURSOR_HEIGHT * 0.5f) };

            // Scale using the same ratios applied in UI::GetScaled
            const float scaleX{ screenW / UI::s_baseWidth };
            const float scaleY{ screenH / UI::s_baseHeight };

            m_primitive->Triangle(
                x1 * scaleX, y1 * scaleY,
                x2 * scaleX, y2 * scaleY,
                x3 * scaleX, y3 * scaleY,
                1.0f, 1.0f, 1.0f, m_menuAlpha
            );
            m_primitive->Render(dc);
        }
    }

	// Render the Option Panel if active. This is a separate UI system that overlays on top of the main menu.
    if (m_isOptionPhase && m_uiOption) {
        m_uiOption->Render(dc, screenW, screenH, 1.0f);
    }

    const auto fade{ UI::GetScaled(0.0f, 0.0f, 1920.0f, 1080.0f, screenW, screenH) };
    if (m_fadeAlpha > 0.001f && m_fadeSprite) {
        m_fadeSprite->Render(dc, fade.x, fade.y, 0.0f, fade.w, fade.h, 0.0f, 0.0f, 1920.0f, 1080.0f, 0.0f, 0.0f, 0.0f, 0.0f, m_fadeAlpha);
    }

    if (postProcess->IsEnabled())
    {
        postProcess->EndCapture(dt);
    }
}

void SceneTitle::RenderMenuOptions(ID3D11DeviceContext* dc, float screenW, float screenH)
{
    struct MenuItem {
        Sprite* sprite{ nullptr };
        float yPos{ 0.0f };
        float width{ 0.0f };
        float height{ 0.0f };
        float colorWeight{ 0.0f };
    };

    struct ColorRGB { float r{ 0.0f }, g{ 0.0f }, b{ 0.0f }; };

    static constexpr ColorRGB unselectedColor{ 0.75f, 0.75f, 0.75f };
    static constexpr ColorRGB selectedColor{ 1.0f, 1.0f, 1.0f };

    const std::array<MenuItem, 3> items{ {
        { m_newGameSprite.get(), Y_NEW_GAME, 173.0f, 25.0f, m_optionWeights[0] },
        { m_optionSprite.get(),  Y_OPTION,   118.0f, 32.0f, m_optionWeights[1] },
        { m_exitSprite.get(),    Y_EXIT,     51.0f,  23.0f, m_optionWeights[2] }
    } };

    for (const auto& item : items)
    {
        if (item.sprite)
        {
            // Scale the destination coordinates dynamically
            const auto scaled{ UI::GetScaled(MENU_START_X, item.yPos, item.width, item.height, screenW, screenH) };

            const float r = CustomLerp(unselectedColor.r, selectedColor.r, item.colorWeight);
            const float g = CustomLerp(unselectedColor.g, selectedColor.g, item.colorWeight);
            const float b = CustomLerp(unselectedColor.b, selectedColor.b, item.colorWeight);

            item.sprite->Render(
                dc,
                scaled.x, scaled.y, 0.0f,      
                scaled.w, scaled.h,            
                0.0f, 0.0f,                    
                item.width, item.height,       
                0.0f,
                r, g, b,
                m_menuAlpha
            );
        }
    }
}

void SceneTitle::AnimateMenu(float elapsedTime)
{
    // Resolve exactly where the cursor is structurally headed
    float targetY{ 0.0f };
    switch (m_currentSelection)
    {
    case MenuOption::NewGame: targetY = Y_NEW_GAME + (25.0f * 0.5f); break;
    case MenuOption::Option:  targetY = Y_OPTION + (32.0f * 0.5f); break;
    case MenuOption::Exit:    targetY = Y_EXIT + (23.0f * 0.5f); break;
    default: return;
    }

    if (!m_isCursorInitialized)
    {
        m_visualCursorY = targetY;
        m_isCursorInitialized = true;
    }

    // Process Frame-Rate Independent Exponential Decay for the Cursor Position
    const float cursorAlpha = 1.0f - std::exp(-CURSOR_SMOOTH_SPEED * elapsedTime);
    m_visualCursorY = CustomLerp(m_visualCursorY, targetY, cursorAlpha);

    // Clamp boundary protection to block endless micro-tail updates
    if (std::abs(m_visualCursorY - targetY) < 0.05f)
    {
        m_visualCursorY = targetY;
    }

    // Smooth out highlighting colors across every text menu item
    const float colorAlpha = 1.0f - std::exp(-COLOR_SMOOTH_SPEED * elapsedTime);
    const auto currentSelectionIndex = static_cast<std::size_t>(m_currentSelection);

    for (std::size_t i = 0; i < m_optionWeights.size(); ++i)
    {
        const float targetWeight = (i == currentSelectionIndex) ? 1.0f : 0.0f;
        m_optionWeights[i] = CustomLerp(m_optionWeights[i], targetWeight, colorAlpha);

        if (std::abs(m_optionWeights[i] - targetWeight) < 0.01f)
        {
            m_optionWeights[i] = targetWeight;
        }
    }
}

void SceneTitle::ExecuteMenuSelection() noexcept
{
    switch (m_currentSelection)
    {
    case MenuOption::NewGame:
        // Bug Prevention: We check !m_isExiting to ensure that if a user 
        // mashes the Enter key, we don't continuously reset the timer to 0.0f
        if (!m_isExiting)
        {
            m_isExiting = true;
            m_exitTimer = 0.0f;

            // Optional: Play a confirmation sound effect here
            // AudioManager::Instance().PlaySFX("Data/Sound/SE_Confirm.wav", 0.5f);
        }
        break;

    case MenuOption::Option:
        if (!m_isExiting && !m_isOptionPhase)
        {
            m_isOptionPhase = true;
            // Optionally play a sound here
        }
        break;

    case MenuOption::Exit:
        if (!m_isExiting)
        {
            SDL_Event quitEvent{}; 
            quitEvent.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quitEvent);
        }
        break;

    default:
        break;
    }
}

void SceneTitle::OnResize(int width, int height)
{
    if (postProcess) postProcess->OnResize(width, height);
}