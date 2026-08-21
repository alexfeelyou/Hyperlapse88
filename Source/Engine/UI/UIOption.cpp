#include "UIOption.h"
#include "Primitive.h"
#include "FontTTF.h"
#include "System/Graphics.h"
#include "System/Input.h"
#include "System/AudioManager.h"
#include <algorithm>
#include <cmath>

void UIOption::Initialize(Primitive* primitive)
{
    m_primitive = primitive;

    m_font = std::make_unique<FontTTF>();
    m_font->Initialize("Data/Font/ElmsSans-Regular.ttf", FONT_SIZE);

    for (std::size_t i = 0; i <= 100; ++i)
    {
        m_volumeStrings[i] = std::to_string(i);
    }

    // Sync initial UI sliders with the core AudioManager settings 
    m_volumes[static_cast<std::size_t>(OptionRow::Music)] =
        static_cast<int>(std::round(AudioManager::Instance().GetGlobalMusicVolume() * 100.0f));

    m_volumes[static_cast<std::size_t>(OptionRow::SoundEffects)] =
        static_cast<int>(std::round(AudioManager::Instance().GetGlobalSFXVolume() * 100.0f));
}

int UIOption::GetVerticalInputTriggered() noexcept
{
    auto& input = Input::Instance();
    auto& kb = input.GetKeyboard();
    auto& pad = input.GetGamePad();

    bool upPress = kb.IsTriggered('W') || kb.IsTriggered(VK_UP) || (pad.GetButtonDown() & GamePad::BTN_UP);
    bool downPress = kb.IsTriggered('S') || kb.IsTriggered(VK_DOWN) || (pad.GetButtonDown() & GamePad::BTN_DOWN);

    const float padY = pad.GetAxisLY();
    const bool isAnalogPushedUp = (padY > 0.5f);
    const bool isAnalogPushedDown = (padY < -0.5f);

    bool analogUpTrigger = (isAnalogPushedUp && !m_analogUpWasPressed);
    bool analogDownTrigger = (isAnalogPushedDown && !m_analogDownWasPressed);

    m_analogUpWasPressed = isAnalogPushedUp;
    m_analogDownWasPressed = isAnalogPushedDown;

    if (upPress || analogUpTrigger) return -1;
    if (downPress || analogDownTrigger) return 1;

    return 0;
}

int UIOption::GetHorizontalInputTriggered() noexcept
{
    auto& input = Input::Instance();
    auto& kb = input.GetKeyboard();
    auto& pad = input.GetGamePad();

    bool leftPress = kb.IsTriggered('A') || kb.IsTriggered(VK_LEFT) || (pad.GetButtonDown() & GamePad::BTN_LEFT);
    bool rightPress = kb.IsTriggered('D') || kb.IsTriggered(VK_RIGHT) || (pad.GetButtonDown() & GamePad::BTN_RIGHT);

    const float padX = pad.GetAxisLX();
    const bool isAnalogPushedLeft = (padX < -0.5f);
    const bool isAnalogPushedRight = (padX > 0.5f);

    bool analogLeftTrigger = (isAnalogPushedLeft && !m_analogLeftWasPressed);
    bool analogRightTrigger = (isAnalogPushedRight && !m_analogRightWasPressed);

    m_analogLeftWasPressed = isAnalogPushedLeft;
    m_analogRightWasPressed = isAnalogPushedRight;

    if (leftPress || analogLeftTrigger) return -1;
    if (rightPress || analogRightTrigger) return 1;

    bool isHoldingLeft = (GetAsyncKeyState('A') & 0x8000) || (GetAsyncKeyState(VK_LEFT) & 0x8000) ||
        (pad.GetButton() & GamePad::BTN_LEFT) || isAnalogPushedLeft;

    bool isHoldingRight = (GetAsyncKeyState('D') & 0x8000) || (GetAsyncKeyState(VK_RIGHT) & 0x8000) ||
        (pad.GetButton() & GamePad::BTN_RIGHT) || isAnalogPushedRight;

    if (isHoldingLeft) return -2;
    if (isHoldingRight) return 2;

    return 0;
}

void UIOption::Update(float elapsedTime) noexcept
{
    const int vDir{ GetVerticalInputTriggered() };
    if (vDir != 0)
    {
        const int count{ static_cast<int>(OptionRow::Count) };
        int current{ static_cast<int>(m_selectedRow) };

        current = (current + vDir + count) % count;
        m_selectedRow = static_cast<OptionRow>(current);
    }

    const int hDir{ GetHorizontalInputTriggered() };
    if (hDir == 0)
    {
        m_inputHoldTimer = 0.0f;
        return;
    }

    bool shouldChangeVolume{ false };

    if (hDir == 1 || hDir == -1)
    {
        shouldChangeVolume = true;
        m_inputHoldTimer = 0.0f;
    }
    else if (hDir == 2 || hDir == -2)
    {
        m_inputHoldTimer += elapsedTime;
        if (m_inputHoldTimer >= INPUT_INITIAL_DELAY)
        {
            shouldChangeVolume = true;
            m_inputHoldTimer -= INPUT_REPEAT_RATE;
        }
    }

    if (shouldChangeVolume)
    {
        const int step{ (hDir > 0) ? 1 : -1 };
        const std::size_t targetIndex{ static_cast<std::size_t>(m_selectedRow) };

        m_volumes[targetIndex] += step;
        m_volumes[targetIndex] = std::clamp(m_volumes[targetIndex], 0, 100);

		// Apply the volume to engine 
        const float normalizedVolume{ static_cast<float>(m_volumes[targetIndex]) / 100.0f };

        if (m_selectedRow == OptionRow::Music)
        {
            AudioManager::Instance().SetGlobalMusicVolume(normalizedVolume);
        }
        else if (m_selectedRow == OptionRow::SoundEffects)
        {
            AudioManager::Instance().SetGlobalSFXVolume(normalizedVolume);

            // Optional: Provide auditory feedback when clicking D-pad / A/D once
            // (Skipped for continuous hold so it doesn't spam the ear)
            if (hDir == 1 || hDir == -1)
            {
                // Un-comment this and use a valid UI click WAV file in your Data folder:
                // AudioManager::Instance().PlaySFX("Data/Sound/SE_Hit.wav", 0.5f);
            }
        }
    }
}

void UIOption::Render(ID3D11DeviceContext* dc, float alpha) const noexcept
{
    if (!m_primitive || alpha <= 0.001f) return;

    auto rs = Graphics::Instance().GetRenderState();
    dc->OMSetBlendState(rs->GetBlendState(BlendState::Transparency), nullptr, 0xFFFFFFFF);
    dc->OMSetDepthStencilState(rs->GetDepthStencilState(DepthState::NoTestNoWrite), 0);

    // --- 1. Background, Frame, & Header Separator ---
    constexpr float innerX{ PANEL_POS_X + BORDER_THICKNESS };
    constexpr float innerY{ PANEL_POS_Y + BORDER_THICKNESS };
    constexpr float innerW{ PANEL_WIDTH - (BORDER_THICKNESS * 2.0f) };
    constexpr float innerH{ PANEL_HEIGHT - (BORDER_THICKNESS * 2.0f) };

    // Inner Translucent Background Layer
    m_primitive->Rect(innerX, innerY, innerW, innerH, 0.0f, 0.0f, 0.0f, BG_R, BG_G, BG_B, alpha * BG_ALPHA_MULTIPLIER);

    // Outer Borders
    m_primitive->Rect(PANEL_POS_X, PANEL_POS_Y, PANEL_WIDTH, BORDER_THICKNESS, 0.0f, 0.0f, 0.0f, OUTLINE_R, OUTLINE_G, OUTLINE_B, alpha);
    m_primitive->Rect(PANEL_POS_X, PANEL_POS_Y + PANEL_HEIGHT - BORDER_THICKNESS, PANEL_WIDTH, BORDER_THICKNESS, 0.0f, 0.0f, 0.0f, OUTLINE_R, OUTLINE_G, OUTLINE_B, alpha);
    m_primitive->Rect(PANEL_POS_X, PANEL_POS_Y + BORDER_THICKNESS, BORDER_THICKNESS, PANEL_HEIGHT - (BORDER_THICKNESS * 2.0f), 0.0f, 0.0f, 0.0f, OUTLINE_R, OUTLINE_G, OUTLINE_B, alpha);
    m_primitive->Rect(PANEL_POS_X + PANEL_WIDTH - BORDER_THICKNESS, PANEL_POS_Y + BORDER_THICKNESS, BORDER_THICKNESS, PANEL_HEIGHT - (BORDER_THICKNESS * 2.0f), 0.0f, 0.0f, 0.0f, OUTLINE_R, OUTLINE_G, OUTLINE_B, alpha);

    // Header Separator Line
    m_primitive->Rect(
        innerX,
        PANEL_POS_Y + SEPARATOR_OFFSET_Y,
        innerW,
        SEPARATOR_THICKNESS,
        0.0f, 0.0f, 0.0f,
        OUTLINE_R, OUTLINE_G, OUTLINE_B,
        alpha
    );

    // --- 2. Queue All Primitives (Batch Rendering) ---
    for (std::size_t i = 0; i < static_cast<std::size_t>(OptionRow::Count); ++i)
    {
        const bool isSelected = (i == static_cast<std::size_t>(m_selectedRow));

        const float r = isSelected ? 1.0f : OUTLINE_R;
        const float g = isSelected ? 1.0f : OUTLINE_G;
        const float b = isSelected ? 1.0f : OUTLINE_B;

        const float labelPosY = PANEL_POS_Y + ROW_START_Y + (i * ROW_SPACING);
        const float trackPosX = PANEL_POS_X + ROW_START_X + SLIDER_INLINE_OFFSET_X;
        const float trackPosY = labelPosY + (FONT_SIZE * 0.5f) - (SLIDER_HEIGHT * 0.5f) + SLIDER_Y_OFFSET;

        // Background Slider Track
        m_primitive->Rect(trackPosX, trackPosY, SLIDER_WIDTH, SLIDER_HEIGHT, 0.0f, 0.0f, 0.0f, 0.3f, 0.3f, 0.3f, alpha);

        // Knob
        const float percent = static_cast<float>(m_volumes[i]) / 100.0f;
        const float knobPosX = trackPosX + (percent * (SLIDER_WIDTH - KNOB_WIDTH));
        const float knobPosY = trackPosY + (SLIDER_HEIGHT * 0.5f) - (KNOB_HEIGHT * 0.5f);

        m_primitive->Rect(knobPosX, knobPosY, KNOB_WIDTH, KNOB_HEIGHT, 0.0f, 0.0f, 0.0f, r, g, b, alpha);
    }

    // Flush batch to GPU
    m_primitive->Render(dc);

    // Render Font Iteration 
    if (m_font)
    {
        // Pure White Title
        m_font->Draw(m_titleText, PANEL_POS_X + TITLE_OFFSET_X, PANEL_POS_Y + TITLE_OFFSET_Y, 1.0f, { 1.0f, 1.0f, 1.0f, alpha });

        for (std::size_t i = 0; i < static_cast<std::size_t>(OptionRow::Count); ++i)
        {
            const bool isSelected = (i == static_cast<std::size_t>(m_selectedRow));

            const DirectX::XMFLOAT4 textColor = isSelected
                ? DirectX::XMFLOAT4{ 1.0f, 1.0f, 1.0f, alpha }
            : DirectX::XMFLOAT4{ OUTLINE_R, OUTLINE_G, OUTLINE_B, alpha };

            const float labelPosX = PANEL_POS_X + ROW_START_X;
            const float labelPosY = PANEL_POS_Y + ROW_START_Y + (i * ROW_SPACING);
            const float valuePosX = labelPosX + SLIDER_INLINE_OFFSET_X + SLIDER_WIDTH + VALUE_GAP_X;

            // Render Row Title
            m_font->Draw(m_rowTexts[i], labelPosX, labelPosY, 1.0f, textColor);

            // Render Row Value
            m_font->Draw(m_volumeStrings[m_volumes[i]], valuePosX, labelPosY, 1.0f, textColor);
        }
    }
}