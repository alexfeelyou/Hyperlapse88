#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <memory>
#include "Primitive.h"

// Forward declaration untuk menghindari circular dependency
class Mouse;

class CursorBlock
{
public:
    CursorBlock(ID3D11Device* device);
    ~CursorBlock();

    // Setup ukuran kursor
    void Initialize(float width, float height);

    // [UPDATED] Menggunakan Reference (Mouse&) agar cocok dengan Input.h
    void Update(float dt, Mouse& mouse);
    void Update(float dt, float rawX, float rawY);

    // Render
    void Render(ID3D11DeviceContext* context, Primitive* primitiveBatcher);

    // Opsi Retro
    void SetGridSnap(bool enable, float gridW, float gridH);
    void SetBlink(bool enable, float visibleTime = 0.8f, float invisibleTime = 0.2f);
    void SetSize(float w, float h);

    void SetColor(float r, float g, float b, float a = 1.0f);

private:
    Microsoft::WRL::ComPtr<ID3D11BlendState> invertBlendState;
    Microsoft::WRL::ComPtr<ID3D11BlendState> oldBlendState;

    float width = 20.0f;
    float height = 20.0f;
    float posX = 0.0f;
    float posY = 0.0f;

    bool useGridSnap = false;
    float gridW = 20.0f;
    float gridH = 20.0f;

    bool useBlink = true;
    float blinkTimer = 0.0f;
    float blinkVisibleTime = 0.5f;   // Lama waktu terlihat
    float blinkInvisibleTime = 0.2f; // Lama waktu hilang
    bool isVisible = true;

    float m_color[4] = { 0.96f, 0.80f, 0.23f, 1.0f };
};