#pragma once

#include <string>
#include <vector>
#include <memory>
#include <d3d11.h>
#include <cstdint> 

#include "System/Sprite.h"
#include "FontTTF.h"

enum class InputDevice : std::uint8_t;

class UIDialogueBox
{
public:
    enum class State : std::uint8_t {
        Hidden = 0,
        Typing,
        WaitingForInput
    };

    UIDialogueBox();
    ~UIDialogueBox() = default;

    void Initialize();
    void StartDialogue(const std::vector<std::string>& dialogues);

    void Update(float dt);
    void Render(ID3D11DeviceContext* dc);
    void RenderToWindow(ID3D11DeviceContext* dc, float windowW, float windowH);

    [[nodiscard]] bool IsActive() const noexcept { return m_state != State::Hidden; }
    [[nodiscard]] int GetCurrentDialogueIndex() const noexcept { return m_currentIndex; }

    void SetShowBackground(bool show) noexcept { m_showBackground = show; }
    void SetPosition(float x, float y) noexcept { m_posX = x; m_posY = y; m_useCustomPos = true; }

    void SetWorldPosition(DirectX::XMFLOAT3 pos) noexcept { m_worldPos = pos; m_is3D = true; }
    void Render3D(ID3D11DeviceContext* dc, class Camera* camera);

    void SetAutoAdvance(bool enable, float delay = 1.5f, bool strict = false) noexcept {
        m_autoAdvance = enable;
        m_autoAdvanceDelay = delay;
        m_strictAutoAdvance = strict;
    }

private:
    void AdvanceDialogue();

    // --- AAA Inline Glyph Rendering Helpers ---
    // Parses a raw string containing {ATK} and {DASH} tags into a renderable string
    [[nodiscard]] std::string ParseDialogueTags(const std::string& rawLine);

    struct InlineSprite {
        Sprite* sprite{ nullptr };
        int triggerByteIndex{ 0 }; // The exact typewriter byte when this should appear
        float offsetX{ 0.0f };
        float offsetY{ 0.0f };
        float scaleW{ 1.0f };
        float scaleH{ 1.0f };
    };

private:
    // --- Sprites ---
    std::unique_ptr<Sprite> m_panelSpriteKB{}; // Keyboard/Mouse Sprite
    std::unique_ptr<Sprite> m_panelSpriteGP{}; // Gamepad Controller Sprite
    std::unique_ptr<FontTTF> m_font{};

    // --- Inline Button Sprites ---
    std::unique_ptr<Sprite> m_spriteRT{};
    std::unique_ptr<Sprite> m_spriteLB{};
    std::vector<InlineSprite> m_activeInlineSprites{};

    // --- State Tracking ---
    State m_state{ State::Hidden };

    InputDevice m_lockedDeviceForLine;

    std::vector<std::string> m_dialogues{};
    int m_currentIndex{ -1 };

    std::string m_currentLine{};
    std::string m_displayedText{};

    int   m_charIndex{ 0 };
    float m_typeTimer{ 0.0f };
    float m_typeDelay{ 0.05f };

    bool  m_showBackground{ true };
    bool  m_useCustomPos{ false };
    float m_posX{ 0.0f };
    float m_posY{ 0.0f };

    bool m_is3D{ false };
    DirectX::XMFLOAT3 m_worldPos{ 0.0f, 0.0f, 0.0f };

    // Auto-advance
    bool  m_autoAdvance{ false };
    float m_autoAdvanceTimer{ 0.0f };
    float m_autoAdvanceDelay{ 1.5f };
    bool  m_strictAutoAdvance{ false };
};