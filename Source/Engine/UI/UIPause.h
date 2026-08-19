#pragma once

#include <memory>
#include <string>
#include <array>
#include <cstdint>

// Forward declarations 
struct ID3D11DeviceContext;
class Sprite;
class FontTTF;

class UIPause
{
public:

    enum class PauseOption : std::uint8_t
    {
        Resume = 0,
        Exit,
        Count // Automatic counter for the number of options
    };

    UIPause() = default;
    ~UIPause() = default;

    // Explicitly delete copy constructor and assignment operator to prevent 
    // accidental copying of unique_ptrs or expensive UI objects.
    UIPause(const UIPause&) = delete;
    UIPause& operator=(const UIPause&) = delete;
    UIPause(UIPause&&) = default;
    UIPause& operator=(UIPause&&) = default;

    void Initialize();
    void Render(ID3D11DeviceContext* dc, float alpha = 1.0f) const;

    void MoveSelection(int direction) noexcept;

    [[nodiscard]] PauseOption GetSelectedOption() const noexcept { return m_selectedOption; }
    void ResetSelection() noexcept { m_selectedOption = PauseOption::Resume; }

private:
    // Core Components initialized to null via brace initialization
    std::unique_ptr<Sprite> m_pauseSprite{};
    std::unique_ptr<FontTTF> m_fontTitle{};
    std::unique_ptr<FontTTF> m_fontMenu{};

    // Cache the string to prevent `std::string` from allocating memory / utilizing SSO 
    // every single frame during the Render loop.
    const std::string m_pauseText{ "PAUSE" };

    const std::array<std::string, static_cast<std::size_t>(PauseOption::Count)> m_menuText{
        "Resume",
        "Exit"
    };

    const std::array<float, static_cast<std::size_t>(PauseOption::Count)> m_menuXOffsets{
        0.0f,   // "Resume" stays perfectly at MENU_POS_X
        22.0f   // "Exit" is shorter, so we align it to center.
    };

    // Track current state
    PauseOption m_selectedOption{ PauseOption::Resume };

    // ========================================================================
    // CONSTEXPR CONFIGURATION 
    // ========================================================================
    static constexpr float SCREEN_WIDTH{ 1920.0f };
    static constexpr float SCREEN_HEIGHT{ 1080.0f };

    static constexpr float SPRITE_WIDTH{ 339.0f };
    static constexpr float SPRITE_HEIGHT{ 456.0f };

    static constexpr float PANEL_POS_X{ (SCREEN_WIDTH - SPRITE_WIDTH) * 0.5f };
    static constexpr float PANEL_POS_Y{ (SCREEN_HEIGHT - SPRITE_HEIGHT) * 0.5f };

    // Title Positioning
    static constexpr float TITLE_POS_X{ PANEL_POS_X + 80.0f };
    static constexpr float TITLE_POS_Y{ PANEL_POS_Y + 86.0f };
    static constexpr float TITLE_FONT_SIZE{ 60.0f };

    // Menu Positioning
    const std::string m_pointerText{ ">> " };
    static constexpr float MENU_POS_X{ PANEL_POS_X + 110.0f };       
    static constexpr float MENU_START_Y{ TITLE_POS_Y + 140.0f };    
    static constexpr float MENU_SPACING_Y{ 60.0f };                 // Space between menu items
    static constexpr float MENU_FONT_SIZE{ 33.0f };
    static constexpr float POINTER_OFFSET_X{ -45.0f };
};