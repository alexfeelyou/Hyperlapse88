#include "UIDialogueBox.h" 

UIDialogueBox::UIDialogueBox() : m_lockedDeviceForLine(InputDevice::Keyboard) {}

std::string UIDialogueBox::ParseDialogueTags(const std::string& rawLine)
{
    std::string processedLine;
    m_activeInlineSprites.clear();

    constexpr float FONT_SIZE = 32.0f;
    constexpr float FULL_WIDTH = FONT_SIZE;
    constexpr float HALF_WIDTH = FONT_SIZE * 0.5f;
    constexpr float LINE_HEIGHT = FONT_SIZE + 10.0f;

    float cursorX = 0.0f;
    float cursorY = 0.0f;

    size_t i = 0;
    while (i < rawLine.length())
    {
        // Process {ATK} Tag
        if (rawLine.compare(i, 5, "{ATK}") == 0)
        {
            if (m_lockedDeviceForLine == InputDevice::Gamepad) {
                constexpr float w = 40.0f;
                constexpr float h = 38.0f;
                constexpr float RT_Y_OFFSET = 7.0f;
				constexpr float RT_X_OFFSET = 1.0f;

                m_activeInlineSprites.push_back({
                    m_spriteRT.get(),
                    static_cast<int>(processedLine.length()),
                    cursorX + RT_X_OFFSET, cursorY + RT_Y_OFFSET, w, h
                    });

                // LEVER 1: Added an extra half-width space (1 Full, 2 Half) to push "で" to the right
                processedLine += u8"   ";

                // Keep the math tracker in sync with the new string length
                cursorX += (FULL_WIDTH + (HALF_WIDTH * 2.0f));
            }
            else {
                std::string kbText = u8"「左クリック」";
                processedLine += kbText;
                cursorX += FULL_WIDTH * 7;
            }
            i += 5;
            continue;
        }

        // Process {DASH} Tag
        if (rawLine.compare(i, 6, "{DASH}") == 0)
        {
            if (m_lockedDeviceForLine == InputDevice::Gamepad) {
                constexpr float w = 42.0f;
                constexpr float h = 37.0f;
                constexpr float LB_Y_OFFSET = 7.0f;
				constexpr float LB_X_OFFSET = 6.0f;

                m_activeInlineSprites.push_back({
                    m_spriteLB.get(),
                    static_cast<int>(processedLine.length()),
                    cursorX - LB_X_OFFSET, cursorY + LB_Y_OFFSET, w, h
                    });

                processedLine += u8"   ";
                cursorX += (FULL_WIDTH * 2.0f);
            }
            else {
                std::string kbText = u8"「Shift」";
                processedLine += kbText;
                cursorX += (FULL_WIDTH * 2.0f) + (HALF_WIDTH * 5.0f);
            }
            i += 6;
            continue;
        }

        // Process normal text & simulate cursor
        unsigned char c = rawLine[i];
        if (c == '\n') {
            cursorX = 0.0f;
            cursorY += LINE_HEIGHT;
            processedLine += rawLine[i++];
            continue;
        }

        int charLength = 1;
        bool isFullWidth = false;
        if ((c & 0xE0) == 0xC0) { charLength = 2; }
        else if ((c & 0xF0) == 0xE0) { charLength = 3; isFullWidth = true; }
        else if ((c & 0xF8) == 0xF0) { charLength = 4; isFullWidth = true; }

        for (int j = 0; j < charLength && i < rawLine.length(); ++j) {
            processedLine += rawLine[i++];
        }
        cursorX += (isFullWidth ? FULL_WIDTH : HALF_WIDTH);
    }

    return processedLine;
}

void UIDialogueBox::Initialize()
{
    auto device = Graphics::Instance().GetDevice();

    m_panelSpriteKB = std::make_unique<Sprite>(device, "Data/Sprite/UI/Sprite_DialogueBox.png");
    m_panelSpriteGP = std::make_unique<Sprite>(device, "Data/Sprite/UI/Sprite_DialogueBoxController.png");
    m_spriteRT = std::make_unique<Sprite>(device, "Data/Sprite/UI/Sprite_DialogueRT.png");
    m_spriteLB = std::make_unique<Sprite>(device, "Data/Sprite/UI/Sprite_DialogueLB.png");

    std::vector<uint32_t> requiredKanji = {
       0x76EE, 0x899A, 0x6226, 0x6642, 0x9593, 0x6765, 0x653B, 0x6483,
       0x9060, 0x6575, 0x629C, 0x8FD1, 0x5203, 0x65AC, 0x88C2, 0x8981,
       0x9B42, 0x523B, 0x8FBC, 0x6B21, 0x8A66, 0x98A8, 0x5F3E, 0x5E55,
       0x529B, 0x898B, 0x7259, 0x8FD4, 0x4ECA, 0x5927, 0x4EBA, 0x6C17,
       0x68EE, 0x5965, 0x85AC, 0x6DB2, 0x6C5A, 0x67D3, 0x51F6, 0x66B4,
       0x5316, 0x500B, 0x4F53, 0x5371, 0x4ED6, 0x9055, 0x72D9, 0x81EA,
       0x7206, 0x524D, 0x843D, 0x65E9, 0x4F55, 0x5F85, 0x69D8, 0x5B50,
       0x81A8, 0x6BD2, 0x5DE6, 0x7E4B, 0x9000, 0x5C48, 0x6ABB, 0x51FA,
       0x79C1, 0x4E2D, 0x8EAB, 0x5168, 0x90E8, 0x4E16, 0x754C, 0x58CA,
       0x6B66, 0x5668, 0x8DB3, 0x5143, 0x5730, 0x9762, 0x8272
    };

    m_font = std::make_unique<FontTTF>();
    m_font->Initialize("Data/Font/zpix.ttf", 28.0f, requiredKanji);
    m_font->Initialize("Data/Font/PixelMplus10-Regular.ttf", 32.0f, requiredKanji);
}

void UIDialogueBox::StartDialogue(const std::vector<std::string>& dialogues)
{
    m_dialogues = dialogues;
    m_currentIndex = -1;

    if (!m_dialogues.empty()) {
        AdvanceDialogue();
    }
}

void UIDialogueBox::AdvanceDialogue()
{
    m_currentIndex++;
    m_autoAdvanceTimer = 0.0f;

    if (m_currentIndex >= static_cast<int>(m_dialogues.size())) {
        m_state = State::Hidden;
        return;
    }

    m_lockedDeviceForLine = Input::Instance().GetLastUsedDevice();
    m_currentLine = ParseDialogueTags(m_dialogues[m_currentIndex]);
    m_displayedText.clear();
    m_charIndex = 0;
    m_typeTimer = 0.0f;
    m_state = State::Typing;
}

void UIDialogueBox::Update(float dt)
{
    if (m_state == State::Hidden) return;

    // Zero-overhead reference binding
    auto& input = Input::Instance();
    auto& keyboard = input.GetKeyboard();
    auto& gamepad = input.GetGamePad();

    if (gamepad.GetButtonDown() != 0 ||
        std::abs(gamepad.GetAxisLX()) > 0.3f || std::abs(gamepad.GetAxisLY()) > 0.3f)
    {
        input.SetLastUsedDevice(InputDevice::Gamepad);
    }
    else if (keyboard.IsTriggered(VK_SPACE))
    {
        input.SetLastUsedDevice(InputDevice::Keyboard);
    }

    const bool isConfirmPressed = keyboard.IsTriggered(VK_SPACE) ||
        ((gamepad.GetButtonDown() & GamePad::BTN_A) != 0);

    if (m_state == State::Typing)
    {
        m_typeTimer += dt;
        if (m_typeTimer >= m_typeDelay) {
            m_typeTimer = 0.0f;
            if (m_charIndex < m_currentLine.length()) {

                unsigned char c = m_currentLine[m_charIndex];
                int charLength = 1;
                if ((c & 0xE0) == 0xC0) charLength = 2;
                else if ((c & 0xF0) == 0xE0) charLength = 3;
                else if ((c & 0xF8) == 0xF0) charLength = 4;

                for (int i = 0; i < charLength && m_charIndex < m_currentLine.length(); ++i) {
                    m_displayedText += m_currentLine[m_charIndex];
                    m_charIndex++;
                }
            }
            else {
                m_state = State::WaitingForInput;
            }
        }

        if (isConfirmPressed) {
            m_displayedText = m_currentLine;
            m_charIndex = static_cast<int>(m_currentLine.length());
            m_state = State::WaitingForInput;
        }
    }
    else if (m_state == State::WaitingForInput)
    {
        if (m_autoAdvance) {
            m_autoAdvanceTimer += dt;
            if (m_autoAdvanceTimer >= m_autoAdvanceDelay) {
                m_autoAdvanceTimer = 0.0f;
                AdvanceDialogue();
            }
        }
        else {
            if (isConfirmPressed) {
                AdvanceDialogue();
            }
        }
    }
}

void UIDialogueBox::Render(ID3D11DeviceContext* dc, float screenW, float screenH)
{
    if (m_state == State::Hidden || !m_font) return;

    auto rs{ Graphics::Instance().GetRenderState() };
    dc->OMSetBlendState(rs->GetBlendState(BlendState::Transparency), nullptr, 0xFFFFFFFF);
    dc->OMSetDepthStencilState(rs->GetDepthStencilState(DepthState::TestOnly), 0);

    // Get the base dimensions
    constexpr float PANEL_W{ 847.0f };
    constexpr float PANEL_H{ 198.0f };

    const float startX{ m_useCustomPos ? m_posX : ((UI::s_baseWidth - PANEL_W) * 0.5f) };
    const float startY{ m_useCustomPos ? m_posY : (UI::s_baseHeight - PANEL_H - 60.0f) };

    const auto scaledPanel = UI::GetScaled(startX, startY, PANEL_W, PANEL_H, screenW, screenH);

    // Font scaling needs a uniform multiplier to prevent ugly stretching
    const float fontScale{ (std::min)(screenW / UI::s_baseWidth, screenH / UI::s_baseHeight) };

    const InputDevice currentDevice{ Input::Instance().GetLastUsedDevice() };
    Sprite* activePanel{ (currentDevice == InputDevice::Gamepad) ? m_panelSpriteGP.get() : m_panelSpriteKB.get() };

    if (m_showBackground && activePanel) {
        activePanel->Render(
            dc,
            scaledPanel.x, scaledPanel.y, 0.0f,
            scaledPanel.w, scaledPanel.h,
            0.0f, 1.0f, 1.0f, 1.0f, 1.0f
        );
    }

    const float textMarginX{ 40.0f * fontScale };
    const float textMarginY{ 40.0f * fontScale };

    m_font->Draw(
        m_displayedText,
        scaledPanel.x + textMarginX,
        scaledPanel.y + textMarginY,
        fontScale,
        { 1.0f, 1.0f, 1.0f, 1.0f }
    );

    constexpr float FONT_BASELINE_SHIFT{ -32.0f };
    constexpr float OPTICAL_Y_TWEAK{ -2.0f };

    for (const auto& inlineIcon : m_activeInlineSprites)
    {
        if (m_charIndex >= inlineIcon.triggerByteIndex && inlineIcon.sprite)
        {
            inlineIcon.sprite->Render(
                dc,
                scaledPanel.x + textMarginX + (inlineIcon.offsetX * fontScale),
                scaledPanel.y + textMarginY + ((inlineIcon.offsetY + FONT_BASELINE_SHIFT + OPTICAL_Y_TWEAK) * fontScale),
                0.0f,
                inlineIcon.scaleW * fontScale,
                inlineIcon.scaleH * fontScale,
                0.0f, 1.0f, 1.0f, 1.0f, 1.0f
            );
        }
    }
}

void UIDialogueBox::Render3D(ID3D11DeviceContext* dc, class Camera* camera)
{
    if (m_state == State::Hidden || !m_font || !camera) return;

    auto rs = Graphics::Instance().GetRenderState();
    dc->OMSetBlendState(rs->GetBlendState(BlendState::Transparency), nullptr, 0xFFFFFFFF);
    dc->OMSetDepthStencilState(rs->GetDepthStencilState(DepthState::TestOnly), 0);

    m_font->Draw3D(m_displayedText, camera, m_worldPos, 0.05f, { 1.0f, 1.0f, 1.0f, 1.0f });
}

void UIDialogueBox::RenderToWindow(ID3D11DeviceContext* dc, float windowW, float windowH)
{
    if (m_state == State::Hidden || !m_font) return;

    auto rs = Graphics::Instance().GetRenderState();
    dc->OMSetBlendState(rs->GetBlendState(BlendState::Transparency), nullptr, 0xFFFFFFFF);
    dc->OMSetDepthStencilState(rs->GetDepthStencilState(DepthState::TestOnly), 0);

    InputDevice currentDevice = Input::Instance().GetLastUsedDevice();
    Sprite* activePanel = (currentDevice == InputDevice::Gamepad) ? m_panelSpriteGP.get() : m_panelSpriteKB.get();

    if (m_showBackground && activePanel) {
        activePanel->Render(dc,
            0.0f, 0.0f, 0.0f,
            windowW, windowH,
            0.0f,
            1.0f, 1.0f, 1.0f, 1.0f
        );
    }

    const float marginX = 14.0f;
    const float marginY = 30.0f;

    // Draw the Base Text (Which safely contains the invisible padding spaces)
    m_font->Draw(m_displayedText, marginX, marginY, 1.0f, { 1.0f, 1.0f, 1.0f, 1.0f });

    constexpr float FONT_BASELINE_SHIFT = -32.0f;
    constexpr float OPTICAL_Y_TWEAK = -2.0f;

    for (const auto& inlineIcon : m_activeInlineSprites)
    {
        if (m_charIndex >= inlineIcon.triggerByteIndex && inlineIcon.sprite)
        {
            inlineIcon.sprite->Render(dc,
                marginX + inlineIcon.offsetX,
                marginY + inlineIcon.offsetY + FONT_BASELINE_SHIFT + OPTICAL_Y_TWEAK,
                0.0f,
                inlineIcon.scaleW, inlineIcon.scaleH,
                0.0f, 1.0f, 1.0f, 1.0f, 1.0f
            );
        }
    }
}