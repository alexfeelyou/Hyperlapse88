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
    // Fungsinya: Menimpa garis border kuning di tempat Judul berada

    BitmapFont* font = ResourceManager::Instance().GetFont("VGA_FONT");
    if (font)
    {
        DirectX::XMFLOAT2 titleSize = font->MeasureText(title, style.textScale);

        // Offset +25.0f disamakan dengan posisi teks judul di bawah
        float labelPad = 8.0f;
        float blockerX = (x + 25.0f) - labelPad;
        float blockerY = y - (th * 2.0f);
        float blockerW = titleSize.x + (labelPad * 2.0f);
        float blockerH = th * 4.0f; // Tinggi cukup untuk nutup garis

        // Gambar kotak hitam kecil
        primitive->Rect(blockerX, blockerY, blockerW, blockerH,
            0.0f, 0.0f, 0.0f,
            style.colorBg.x, style.colorBg.y, style.colorBg.z, style.colorBg.w);
    }

    // PENTING: Flush (Gambar) semua kotak sebelum teks
    primitive->Render(dc);

    // =========================================================
    // 3. RENDER TEKS
    // =========================================================

    if (font)
    {
        // A. Judul Panel (Kiri Atas + Offset 25px)
        font->Draw(title.c_str(), x + 25.0f, y - 8.0f, style.textScale,
            style.colorText.x, style.colorText.y, style.colorText.z, style.colorText.w);

        // B. Pesan Body (CENTER)
        float startY = y + 80.0f; // Jarak dari atas

        for (const auto& line : formattedLines)
        {
            DirectX::XMFLOAT2 lineSize = font->MeasureText(line, style.textScale);

            // RUMUS CENTER MURNI:
            // Posisi X = Awal Panel + (Setengah Sisa Ruang Kosong)
            float lineX = x + (width - lineSize.x) / 2.0f;

            font->Draw(line.c_str(), lineX, startY, style.textScale,
                style.colorText.x, style.colorText.y, style.colorText.z, style.colorText.w);

            startY += 25.0f; // Pindah baris
        }
    }

    // =========================================================
    // 4. RENDER TOMBOL
    // =========================================================
    for (auto& btn : buttons)
    {
        btn->Render(dc, nullptr);
    }
}