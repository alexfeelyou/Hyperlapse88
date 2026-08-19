#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include "ButtonManager.h"
#include "UIButtonPrimitive.h"
#include "Primitive.h"

// Struct untuk menyimpan tema visual (Warna & Ukuran)
// Jadi kita bisa bikin tema: "Theme_DOS", "Theme_Hacker", "Theme_Error", dll.
struct TUITheme {
    ButtonStyle styleStandby;
    ButtonStyle styleHover;
    ButtonStyle stylePress;

    float textScale = 0.625f;
    float padding = 10.0f;
    float verticalAdj = 2.0f;
    TextAlignment align = TextAlignment::Left;
};

class TUIBuilder
{
public:
    // Builder butuh akses ke Manager (untuk simpan tombol) dan Batcher (untuk render)
    TUIBuilder(ButtonManager* manager, Primitive* batcher);
    ~TUIBuilder() = default;

    // 1. SETUP: Tentukan posisi awal dan gaya
    void SetStartPosition(float x, float y);
    void SetSpacing(float spacing);
    void SetButtonSize(float width, float height);
    void SetTheme(const TUITheme& theme);

    // 2. BUILD: Fungsi utama untuk membuat tombol
    // Mengembalikan pointer mentah agar Scene bisa menyimpan referensinya jika butuh logic khusus
    UIButtonPrimitive* AddButton(const std::string& text, std::function<void()> onClick);

    // Tambahan: Untuk label/dekorasi yang tidak bisa diklik
    UIButtonPrimitive* AddDecoration(const std::string& text);

    // Reset posisi kursor ke awal (berguna jika mau bikin kolom baru)
    void ResetCursor();

private:
    ButtonManager* m_manager;
    Primitive* m_batcher;

    // Layout State
    float m_startX = 0.0f;
    float m_startY = 0.0f;
    float m_currentY = 0.0f;

    float m_btnWidth = 200.0f;
    float m_btnHeight = 40.0f;
    float m_spacing = 5.0f;

    // Active Theme
    TUITheme m_theme;
};