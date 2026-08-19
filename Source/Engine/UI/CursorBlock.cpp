#include "CursorBlock.h"
#include "System/Mouse.h" // Include full definition di sini
#include <cmath>

CursorBlock::CursorBlock(ID3D11Device* device)
{
    // [PERBAIKAN] Ganti dari INVERT ke STANDARD ALPHA BLEND
    // Ini menjamin warna kursor tetap AMBER TERANG di atas background apapun.

    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;

    blendDesc.RenderTarget[0].BlendEnable = TRUE;

    // SRC_ALPHA = Pakai alpha dari warna kursor
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    // INV_SRC_ALPHA = Background terlihat sesuai sisa alpha kursor
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;

    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;

    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    device->CreateBlendState(&blendDesc, invertBlendState.GetAddressOf());
}

CursorBlock::~CursorBlock() {}

void CursorBlock::Initialize(float w, float h)
{
    this->width = w;
    this->height = h;
}

void CursorBlock::SetGridSnap(bool enable, float gw, float gh)
{
    useGridSnap = enable;
    gridW = gw;
    gridH = gh;
}

void CursorBlock::SetBlink(bool enable, float visibleTime, float invisibleTime)
{
    useBlink = enable;
    blinkVisibleTime = visibleTime;
    blinkInvisibleTime = invisibleTime;

    // Reset state agar saat diaktifkan, kursor mulai dari kondisi terlihat (fresh start)
    isVisible = true;
    blinkTimer = 0.0f;
}

void CursorBlock::SetSize(float w, float h)
{
    this->width = w;
    this->height = h;
}

void CursorBlock::SetColor(float r, float g, float b, float a)
{
    m_color[0] = r; m_color[1] = g; m_color[2] = b; m_color[3] = a;
}

// [BARU] Logika utama dipindah ke sini
void CursorBlock::Update(float dt, float rawX, float rawY)
{
    // 1. Logic Snapping
    if (useGridSnap)
    {
        posX = floorf(rawX / gridW) * gridW;
        posY = floorf(rawY / gridH) * gridH;
    }
    else
    {
        posX = rawX;
        posY = rawY;
    }

    // 2. Logic Blinking
    if (useBlink)
    {
        blinkTimer += dt;
        float currentLimit = isVisible ? blinkVisibleTime : blinkInvisibleTime;

        if (blinkTimer >= currentLimit)
        {
            blinkTimer -= currentLimit;
            isVisible = !isVisible;
        }
    }
    else
    {
        isVisible = true;
    }
}

// [MODIFIED] Fungsi lama sekarang cuma jadi wrapper
void CursorBlock::Update(float dt, Mouse& mouse)
{
    float mx = static_cast<float>(mouse.GetPositionX());
    float my = static_cast<float>(mouse.GetPositionY());

    // Panggil fungsi generic di atas
    Update(dt, mx, my);
}

void CursorBlock::Render(ID3D11DeviceContext* context, Primitive* primitiveBatcher)
{
    if (!isVisible || !primitiveBatcher) return;

    // 1. Simpan State Lama
    float blendFactor[4];
    UINT sampleMask;
    context->OMGetBlendState(oldBlendState.GetAddressOf(), blendFactor, &sampleMask);

    // 2. Pasang Invert Blend State
    context->OMSetBlendState(invertBlendState.Get(), nullptr, 0xFFFFFFFF);

    // 3. Gambar Kotak (Flush via Primitive)
    // Warna Amber: 0.96, 0.80, 0.23
    primitiveBatcher->Rect(posX, posY, width, height, 0.0f, 0.0f, 0.0f, m_color[0], m_color[1], m_color[2], m_color[3]);
    primitiveBatcher->Render(context);

    // 4. Kembalikan State Lama
    context->OMSetBlendState(oldBlendState.Get(), blendFactor, sampleMask);
    oldBlendState.Reset();
}