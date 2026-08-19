#include "UIPanel.h"
#include "System/Input.h"
#include <sstream>

UIPanel::UIPanel(Primitive* prim, float x, float y, float w, float h, const std::string& title)
    : primitive(prim), x(x), y(y), width(w), height(h), title(title)
{
    // Default Style
    style = PanelStyle();
}

void UIPanel::Show() {
    isVisible = true;
}

void UIPanel::Hide() {
    isVisible = false;
    if (onClose) onClose();
}

void UIPanel::SetMessage(const std::string& msg)
{
    rawMessage = msg;
    RecalculateTextLayout();
}

void UIPanel::RecalculateTextLayout()
{
    formattedLines.clear();

    // Gunakan string stream untuk memecah teks berdasarkan karakter '\n'
    std::stringstream ss(rawMessage);
    std::string segment;

    while (std::getline(ss, segment, '\n'))
    {
        // Masukkan mentah-mentah sesuai potongan baris kamu
        formattedLines.push_back(segment);
    }
}

void UIPanel::AddButton(const std::string& label, float relX, float relY, float w, float h, std::function<void()> onClick)
{
    auto btn = std::make_unique<UIButtonPrimitive>(primitive, label, x + relX, y + relY, w, h);
    btn->SetOnClick(onClick);
    btn->SetAlignment(TextAlignment::Center);

    // Style Tombol (Sesuai request: Transparan + Border Kuning)
    DirectX::XMFLOAT4 transparent = { 0.0f, 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT4 white = { 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT4 blue = { 0.0f, 0.0f, 0.8f, 1.0f };

    // Standby: Transparan, Border Kuning (dari style)
    btn->SetStyle(ButtonState::STANDBY, { transparent, transparent, style.colorBorder });

    // Hover: Biru, Border Biru, Teks Putih (Sesuai file lama kamu)
    btn->SetStyle(ButtonState::HOVER,   { blue, blue, white });

    // Pressed: Kuning Solid
    btn->SetStyle(ButtonState::PRESSED, { style.colorBorder, style.colorBorder, {0.0f, 0.0f, 0.0f, 1.0f} });

    buttons.push_back(std::move(btn));
}

void UIPanel::Update()
{
    if (!isVisible) return;
    for (auto& btn : buttons) btn->Update();
}

void UIPanel::Render(ID3D11DeviceContext* dc)
{
    if (!isVisible) return;

    // =========================================================
    // 1. GAMBAR KOTAK & BORDER
    // =========================================================

    // A. Dimmer (Layar redup)
    primitive->Rect(0, 0, 1920, 1080, 0.0f, 0.0f, 0.0f,
        style.colorDimmer.x, style.colorDimmer.y, style.colorDimmer.z, style.colorDimmer.w);

    // B. Background Panel Hitam (Sedikit lebih besar dari border)
    primitive->Rect(x - 8.0f, y - 8.0f, width + 16.0f, height + 16.0f,
        0.0f, 0.0f, 0.0f,
        style.colorBg.x, style.colorBg.y, style.colorBg.z, style.colorBg.w);

    // C. Border Kuning
    primitive->Rect(x, y, width, height,
        0.0f, 0.0f, 0.0f,
        style.colorBorder.x, style.colorBorder.y, style.colorBorder.z, style.colorBorder.w);

    // D. Border Dalam Hitam (Efek garis tipis)
    float th = style.borderThickness;
    primitive->Rect(x + th, y + th, width - (th * 2), height - (th * 2),
        0.0f, 0.0f, 0.0f,
        style.colorBg.x, style.colorBg.y, style.colorBg.z, style.colorBg.w);

    // =========================================================
    // 2. HEADER BLOCKER (INI YANG HILANG KEMARIN)
    // =========================================================

    // PENTING: Flush (Gambar) semua kotak sebelum teks
    primitive->Render(dc);

    // =========================================================
    // 4. RENDER TOMBOL
    // =========================================================
    for (auto& btn : buttons)
    {
        btn->Render(dc, nullptr);
    }
}