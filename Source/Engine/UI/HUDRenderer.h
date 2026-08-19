#pragma once

#include <d3d11.h>
#include <memory>
#include "Primitive.h"

class Boss;
class INaviPhase;

// ============================================================
// HUDRenderer
//   Draws in-game health bars (player + boss) using Primitive.
//   - Call Render() once per frame, AFTER RenderScene, on the
//     main camera pass only (skip transparent sub-windows).
//   - Bars are drawn in screen-space (pixel coordinates).
// ============================================================
class HUDRenderer
{
public:
    explicit HUDRenderer(ID3D11Device* device);
    ~HUDRenderer() = default;

    // Call this every frame to submit + flush the bar geometry.
    // playerHP/MaxHP  : from Player::GetHP() / 100
    // bossHP/MaxHP    : from current phase GetHP() / GetMaxHP()
    //                   pass bossMaxHP = 0 to hide boss bar
    void Render(ID3D11DeviceContext* dc,
        int playerHP, int playerMaxHP,
        int bossHP, int bossMaxHP);

private:
    void DrawBar(float x, float y, float w, float h,
        float fill,            // 0..1
        float r, float g, float b,
        ID3D11DeviceContext* dc);

    std::unique_ptr<Primitive> m_prim;

    // ---- Layout constants (tweak freely) ----
    // Player bar — bottom-left
    static constexpr float k_playerBarX = 30.0f;
    static constexpr float k_playerBarY = 40.0f;   // from bottom
    static constexpr float k_playerBarW = 300.0f;
    static constexpr float k_playerBarH = 18.0f;

    // Boss bar — top-centre
    static constexpr float k_bossBarW = 500.0f;
    static constexpr float k_bossBarH = 18.0f;
    static constexpr float k_bossBarYFromTop = 30.0f;

    // Background / border thickness
    static constexpr float k_bgPad = 3.0f;
    static constexpr float k_borderThick = 1.5f;
};