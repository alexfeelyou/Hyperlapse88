#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <array>

struct ID3D11DeviceContext;
class Primitive;
class FontTTF;

class UIOption
{
public:
    enum class OptionRow : std::uint8_t {
        Music = 0,
        SoundEffects,
        Count
    };

    UIOption() = default;
    ~UIOption() = default;

    UIOption(const UIOption&) = delete;
    UIOption& operator=(const UIOption&) = delete;
    UIOption(UIOption&&) = default;
    UIOption& operator=(UIOption&&) = default;

    void Initialize(Primitive* primitive);
    void Update(float elapsedTime) noexcept;
    void Render(ID3D11DeviceContext* dc, float alpha = 1.0f) const noexcept;

private:
    Primitive* m_primitive{ nullptr };
    std::unique_ptr<FontTTF> m_font{};

    OptionRow m_selectedRow{ OptionRow::Music };

    std::array<int, static_cast<std::size_t>(OptionRow::Count)> m_volumes{ 50, 50 };

    const std::array<std::string, static_cast<std::size_t>(OptionRow::Count)> m_rowTexts{
        "Music",
        "Sound Effects"
    };

    const std::string m_titleText{ "Options" };
    std::array<std::string, 101> m_volumeStrings{};

    float m_inputHoldTimer{ 0.0f };
    bool m_analogLeftWasPressed{ false };
    bool m_analogRightWasPressed{ false };
    bool m_analogUpWasPressed{ false };
    bool m_analogDownWasPressed{ false };

    [[nodiscard]] int GetHorizontalInputTriggered() noexcept;
    [[nodiscard]] int GetVerticalInputTriggered() noexcept;

    static constexpr float SCREEN_WIDTH{ 1920.0f };
    static constexpr float SCREEN_HEIGHT{ 1080.0f };

    static constexpr float PANEL_WIDTH{ 800.0f };
    static constexpr float PANEL_HEIGHT{ 400.0f };
    static constexpr float PANEL_POS_X{ (SCREEN_WIDTH - PANEL_WIDTH) * 0.5f };
    static constexpr float PANEL_POS_Y{ (SCREEN_HEIGHT - PANEL_HEIGHT) * 0.5f };
    static constexpr float BORDER_THICKNESS{ 4.0f };

    static constexpr float FONT_SIZE{ 40.0f };
    static constexpr float TITLE_OFFSET_X{ 40.0f };
    static constexpr float TITLE_OFFSET_Y{ 45.0f };

    // Divider Line 
    static constexpr float SEPARATOR_OFFSET_Y{ 60.0f }; 
    static constexpr float SEPARATOR_THICKNESS{ BORDER_THICKNESS };

    // Grid System 
    static constexpr float ROW_START_X{ 40.0f };
    static constexpr float ROW_START_Y{ 140.0f };
    static constexpr float ROW_SPACING{ 80.0f };

    // Slider Metrics 
    static constexpr float SLIDER_INLINE_OFFSET_X{ 240.0f };
    static constexpr float SLIDER_Y_OFFSET{ -28.0f };
    static constexpr float SLIDER_WIDTH{ 430.0f };
    static constexpr float SLIDER_HEIGHT{ 4.0f };
    static constexpr float KNOB_WIDTH{ 12.0f };
    static constexpr float KNOB_HEIGHT{ 24.0f };
    static constexpr float VALUE_GAP_X{ 20.0f };

    static constexpr float INPUT_INITIAL_DELAY{ 0.3f };
    static constexpr float INPUT_REPEAT_RATE{ 0.04f };

    static constexpr float OUTLINE_R{ 0.75f };
    static constexpr float OUTLINE_G{ 0.75f };
    static constexpr float OUTLINE_B{ 0.75f };

    static constexpr float BG_R{ 0.05f };
    static constexpr float BG_G{ 0.05f };
    static constexpr float BG_B{ 0.05f };
    static constexpr float BG_ALPHA_MULTIPLIER{ 0.35f };
};