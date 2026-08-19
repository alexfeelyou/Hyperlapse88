#include "TUIBuilder.h"

TUIBuilder::TUIBuilder(ButtonManager* manager, Primitive* batcher)
    : m_manager(manager), m_batcher(batcher)
{
    // Default values agar tidak crash jika lupa set
    m_theme.textScale = 0.625f;
}

void TUIBuilder::SetStartPosition(float x, float y)
{
    m_startX = x;
    m_startY = y;
    m_currentY = y;
}

void TUIBuilder::SetSpacing(float spacing)
{
    m_spacing = spacing;
}

void TUIBuilder::SetButtonSize(float width, float height)
{
    m_btnWidth = width;
    m_btnHeight = height;
}

void TUIBuilder::SetTheme(const TUITheme& theme)
{
    m_theme = theme;
}

void TUIBuilder::ResetCursor()
{
    m_currentY = m_startY;
}

UIButtonPrimitive* TUIBuilder::AddButton(const std::string& text, std::function<void()> onClick)
{
    // 1. Buat Button Baru
    auto btn = std::make_unique<UIButtonPrimitive>(
        m_batcher,
        text,
        m_startX,
        m_currentY,
        m_btnWidth,
        m_btnHeight
    );

    // 2. Terapkan Style dari Theme
    btn->SetStyle(ButtonState::STANDBY, m_theme.styleStandby);
    btn->SetStyle(ButtonState::HOVER, m_theme.styleHover);
    btn->SetStyle(ButtonState::PRESSED, m_theme.stylePress);

    // 3. Terapkan Layout Properties
    btn->SetTextScale(m_theme.textScale);
    btn->SetPadding(m_theme.padding);
    btn->SetVerticalAdjustment(m_theme.verticalAdj);
    btn->SetAlignment(m_theme.align);

    // 4. Set Callback
    if (onClick) {
        btn->SetOnClick(onClick);
    }

    // 5. Simpan pointer mentah sebelum ownership dipindah ke Manager
    UIButtonPrimitive* rawPtr = btn.get();

    // 6. Masukkan ke Manager
    m_manager->AddButton(std::move(btn));

    // 7. Update posisi Y untuk tombol berikutnya (Auto-Layout)
    m_currentY += (m_btnHeight + m_spacing);

    return rawPtr;
}

UIButtonPrimitive* TUIBuilder::AddDecoration(const std::string& text)
{
    // Dekorasi mirip tombol tapi biasanya interaksinya beda
    // Untuk simplifikasi, kita anggap sama tapi tanpa callback onClick
    return AddButton(text, nullptr);
}