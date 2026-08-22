#include "UIPause.h"

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
    // if the user presses 'Up' (-1) at the top of the menu
    const int count = static_cast<int>(PauseOption::Count);
    int currentIndex = static_cast<int>(m_selectedOption);

    currentIndex = (currentIndex + direction + count) % count;

    m_selectedOption = static_cast<PauseOption>(currentIndex);
}

void UIPause::Render(ID3D11DeviceContext* dc, float screenW, float screenH, float alpha) const
{
    // BUG PREVENTION: Defensive guard clauses
    if (!m_pauseSprite || !m_fontTitle || !m_fontMenu) return;

    auto rs{ Graphics::Instance().GetRenderState() };

    dc->OMSetBlendState(rs->GetBlendState(BlendState::Transparency), nullptr, 0xFFFFFFFF);
    dc->OMSetDepthStencilState(rs->GetDepthStencilState(DepthState::TestOnly), 0);

    // Resolve resolution scaling
    const float scaleX{ screenW / UI::s_baseWidth };
    const float scaleY{ screenH / UI::s_baseHeight };
    const float fontScale{ (std::min)(scaleX, scaleY) };
    const auto panel = UI::GetScaled(PANEL_POS_X, PANEL_POS_Y, SPRITE_WIDTH, SPRITE_HEIGHT, screenW, screenH);

    // Render Background
    m_pauseSprite->Render(
        dc,
        panel.x, panel.y, 0.0f,
        panel.w, panel.h,
        0.0f, 0.0f,
        SPRITE_WIDTH, SPRITE_HEIGHT,
        0.0f,
        1.0f, 1.0f, 1.0f, alpha
    );

    // Render Title
    m_fontTitle->Draw(
        m_pauseText,
        TITLE_POS_X * scaleX,
        TITLE_POS_Y * scaleY,
        fontScale,
        { 1.0f, 1.0f, 1.0f, alpha }
    );

    // Render Menu Options
    const std::size_t itemCount{ m_menuText.size() };
    for (std::size_t i = 0; i < itemCount; ++i)
    {
        const bool isSelected{ (i == static_cast<std::size_t>(m_selectedOption)) };

        const DirectX::XMFLOAT4 color = isSelected
            ? DirectX::XMFLOAT4{ 1.0f, 1.0f, 0.0f, alpha }
        : DirectX::XMFLOAT4{ 0.7f, 0.7f, 0.7f, alpha };

        const float targetY{ (MENU_START_Y + (i * MENU_SPACING_Y)) * scaleY };
        const float targetX{ (MENU_POS_X + m_menuXOffsets[i]) * scaleX };

        m_fontMenu->Draw(m_menuText[i], targetX, targetY, fontScale, color);

        if (isSelected)
        {
            m_fontMenu->Draw(m_pointerText, (MENU_POS_X + POINTER_OFFSET_X) * scaleX, targetY, fontScale, color);
        }
    }
}