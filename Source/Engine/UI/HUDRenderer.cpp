#include "HUDRenderer.h"
#include "System/Graphics.h"
#include <d3d11.h>
#include <algorithm>

HUDRenderer::HUDRenderer(ID3D11Device* device)
{
    m_prim = std::make_unique<Primitive>(device);
}

// ============================================================
// DrawBar  (internal helper)
//   Draws one bar: dark background → coloured fill → white border
// ============================================================
void HUDRenderer::DrawBar(float x, float y, float w, float h,
    float fill,
    float r, float g, float b,
    ID3D11DeviceContext* dc)
{
    fill = std::clamp(fill, 0.0f, 1.0f);

    // --- 1. Dark background ---
    m_prim->Rect(
        x - k_bgPad, y - k_bgPad,
        w + k_bgPad * 2.0f, h + k_bgPad * 2.0f,
        0.0f, 0.0f, 0.0f,
        0.12f, 0.12f, 0.12f, 0.85f);

    // --- 2. Filled portion ---
    if (fill > 0.0f)
    {
        // Colour shifts red as HP drops: full = (r,g,b), empty = (1, 0.1, 0.1)
        float lerp = fill;
        float fr = r * lerp + 0.85f * (1.0f - lerp);
        float fg = g * lerp + 0.10f * (1.0f - lerp);
        float fb = b * lerp + 0.10f * (1.0f - lerp);

        m_prim->Rect(x, y, w * fill, h,
            0.0f, 0.0f, 0.0f,
            fr, fg, fb, 1.0f);
    }

    // --- 3. Thin white border ---
    // Top
    m_prim->Rect(x - k_bgPad, y - k_bgPad,
        w + k_bgPad * 2.0f, k_borderThick,
        0.0f, 0.0f, 0.0f,
        1.0f, 1.0f, 1.0f, 0.6f);
    // Bottom
    m_prim->Rect(x - k_bgPad, y + h + k_bgPad - k_borderThick,
        w + k_bgPad * 2.0f, k_borderThick,
        0.0f, 0.0f, 0.0f,
        1.0f, 1.0f, 1.0f, 0.6f);
    // Left
    m_prim->Rect(x - k_bgPad, y - k_bgPad,
        k_borderThick, h + k_bgPad * 2.0f,
        0.0f, 0.0f, 0.0f,
        1.0f, 1.0f, 1.0f, 0.6f);
    // Right
    m_prim->Rect(x + w + k_bgPad - k_borderThick, y - k_bgPad,
        k_borderThick, h + k_bgPad * 2.0f,
        0.0f, 0.0f, 0.0f,
        1.0f, 1.0f, 1.0f, 0.6f);

    // Flush this bar immediately so borders don't bleed into next bar
    m_prim->Render(dc);
}

// ============================================================
// Render
// ============================================================
void HUDRenderer::Render(ID3D11DeviceContext* dc,
    int playerHP, int playerMaxHP,
    int bossHP, int bossMaxHP)
{
    if (!dc) return;

    // Get screen dimensions from current viewport
    D3D11_VIEWPORT vp;
    UINT numVP = 1;
    dc->RSGetViewports(&numVP, &vp);
    const float screenW = vp.Width;
    const float screenH = vp.Height;

    // ---- Player bar (bottom-left) ----
    if (playerMaxHP > 0)
    {
        float fill = (float)playerHP / (float)playerMaxHP;
        float barY = screenH - k_playerBarY - k_playerBarH;

        // Green → red fill
        DrawBar(k_playerBarX, barY,
            k_playerBarW, k_playerBarH,
            fill,
            0.15f, 0.85f, 0.25f,   // healthy green
            dc);
    }

    // ---- Boss bar (top-centre) ----
    if (bossMaxHP > 0)
    {
        float fill = (float)bossHP / (float)bossMaxHP;
        float barX = (screenW - k_bossBarW) * 0.5f;

        // Gold → red fill
        DrawBar(barX, k_bossBarYFromTop,
            k_bossBarW, k_bossBarH,
            fill,
            0.95f, 0.75f, 0.10f,   // boss gold
            dc);
    }
}