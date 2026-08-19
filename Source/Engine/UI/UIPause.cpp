#include "UIPause.h"
#include "System/Graphics.h"
#include "System/Sprite.h"
#include "FontTTF.h"
#include <DirectXMath.h>

void UIPause::Initialize()
{
    auto device{ Graphics::Instance().GetDevice() };

    m_pauseSprite = std::make_unique<Sprite>(device, "Data/Sprite/UI/Sprite_Pause.png");
    m_fontTitle = std::make_unique<FontTTF>();
    m_fontTitle->Initialize("Data/Font/PixelifySans-Bold.ttf", TITLE_FONT_SIZE);

    m_fontMenu = std::make_unique<FontTTF>();
    m_fontMenu->Initialize("Data/Font/PixelifySans-Bold.ttf", MENU_FONT_SIZE);
}

void UIPause::MoveSelection(int direction) noexcept
{
    // BUG PREVENTION: Safe wrapping logic.
    // By adding the Count before using modulo, we prevent negative results 
    // if the user presses 'Up' (-1) at the top of the menu.
    const int count = static_cast<int>(PauseOption::Count);
    int currentIndex = static_cast<int>(m_selectedOption);

    currentIndex = (currentIndex + direction + count) % count;

    m_selectedOption = static_cast<PauseOption>(currentIndex);
}

void UIPause::Render(ID3D11DeviceContext* dc, float alpha) const
{
    // BUG PREVENTION: Defensive guard clauses.
    if (!m_pauseSprite || !m_fontTitle || !m_fontMenu) return;

    auto rs{ Graphics::Instance().GetRenderState() };

    dc->OMSetBlendState(rs->GetBlendState(BlendState::Transparency), nullptr, 0xFFFFFFFF);
    dc->OMSetDepthStencilState(rs->GetDepthStencilState(DepthState::TestOnly), 0);

    // Render Background
    m_pauseSprite->Render(
        dc,
        PANEL_POS_X, PANEL_POS_Y, 0.0f,
        SPRITE_WIDTH, SPRITE_HEIGHT,
        0.0f, 0.0f,
        SPRITE_WIDTH, SPRITE_HEIGHT,
        0.0f,
        1.0f, 1.0f, 1.0f, alpha   
    );

    // Render Title
    m_fontTitle->Draw(
        m_pauseText,
        TITLE_POS_X,
        TITLE_POS_Y,
        1.0f,
        { 1.0f, 1.0f, 1.0f, alpha }
    );

    // Render Menu Options
    const std::size_t itemCount = m_menuText.size();
    for (std::size_t i = 0; i < itemCount; ++i)
    {
        const bool isSelected = (i == static_cast<std::size_t>(m_selectedOption));

        // Inject the dynamic alpha into our color selection
        DirectX::XMFLOAT4 color = isSelected
            ? DirectX::XMFLOAT4{ 1.0f, 1.0f, 0.0f, alpha }
        : DirectX::XMFLOAT4{ 0.7f, 0.7f, 0.7f, alpha };

        const float targetY = MENU_START_Y + (i * MENU_SPACING_Y);
        const float targetX = MENU_POS_X + m_menuXOffsets[i];

        // Draw the menu text (Resume / Exit)
        m_fontMenu->Draw(
            m_menuText[i],
            targetX,
            targetY,
            1.0f,
            color
        );

        // Draw the pointer ONLY for the selected item.
        // It always uses MENU_POS_X as its baseline so it never shifts horizontally.
        if (isSelected)
        {
            m_fontMenu->Draw(
                m_pointerText,
                MENU_POS_X + POINTER_OFFSET_X,
                targetY,
                1.0f,
                color
            );
        }
    }
}