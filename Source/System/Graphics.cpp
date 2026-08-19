#include "Graphics.h"
#include "Misc.h"
#include "EffectManager.h"
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

void Graphics::Initialize()
{
    HRESULT hr = S_OK;

    UINT createDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(DEBUG) || defined(_DEBUG)
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL featureLevel;

    // 1. Buat D3D11 Device
    hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createDeviceFlags,
        featureLevels, ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        device.GetAddressOf(),
        &featureLevel,
        immediateContext.GetAddressOf()
    );
    _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

    // 2. Ambil IDXGIDevice1, simpan sebagai member (dipakai DComp nanti)
    hr = device.As(&dxgiDevice);
    _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
    dxgiDevice->SetMaximumFrameLatency(1); // Kurangi frame queuing

    // 3. Ambil IDXGIFactory2
    {
        Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        hr = dxgiDevice->GetAdapter(adapter.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

        hr = adapter->GetParent(__uuidof(IDXGIFactory2), (void**)dxgiFactory.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
    }

    // 4. Cek dukungan variable-refresh-rate (DXGI_FEATURE_PRESENT_ALLOW_TEARING)
    //    Diperlukan untuk G-Sync / FreeSync / borderless fullscreen tanpa vsync stall
    {
        Microsoft::WRL::ComPtr<IDXGIFactory5> factory5;
        if (SUCCEEDED(dxgiFactory.As(&factory5)))
        {
            BOOL tearing = FALSE;
            if (SUCCEEDED(factory5->CheckFeatureSupport(
                DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing))))
            {
                m_tearingSupported = (tearing == TRUE);
            }
        }
    }

    // 5. Inisialisasi renderer bersama
    renderState = std::make_unique<RenderState>(device.Get());
    primitiveRenderer = std::make_unique<PrimitiveRenderer>(device.Get());
    shapeRenderer = std::make_unique<ShapeRenderer>(device.Get());
    modelRenderer = std::make_unique<ModelRenderer>(device.Get());

    // 6. Initializes Effekseer once for the entire game loop
    EffectManager::Instance().Initialize(device.Get(), immediateContext.Get());
}

// ─────────────────────────────────────────────────────────────────────────────
// Swap chain untuk window normal (langsung ke HWND)
// ─────────────────────────────────────────────────────────────────────────────
void Graphics::CreateSwapChainForHwnd(HWND hWnd, int width, int height, IDXGISwapChain1** outSwapChain)
{
    if (!dxgiFactory) return;

    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = width;
    sd.Height = height;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

    // Frame-latency waitable object: CPU tunggu GPU siap sebelum render berikutnya
    // → mengurangi input latency secara signifikan
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    if (m_tearingSupported)
        sd.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

    HRESULT hr = dxgiFactory->CreateSwapChainForHwnd(
        device.Get(), hWnd, &sd, nullptr, nullptr, outSwapChain);
    _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

    // Matikan Alt+Enter fullscreen bawaan DXGI
    dxgiFactory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);

    // Set frame latency ke 1 pada swap chain juga
    Microsoft::WRL::ComPtr<IDXGISwapChain2> sc2;
    if (SUCCEEDED((*outSwapChain)->QueryInterface(IID_PPV_ARGS(&sc2))))
        sc2->SetMaximumFrameLatency(1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Swap chain untuk transparent window via DirectComposition
//
// PENTING: AlphaMode = PREMULTIPLIED
// Pixel shader harus output: float4(rgb * a, a)  ← bukan straight alpha!
// Kalau shader belum premultiply, tambahkan di PS output:
//   output.rgba = float4(color.rgb * color.a, color.a);
// ─────────────────────────────────────────────────────────────────────────────
void Graphics::CreateSwapChainForComposition(int width, int height, IDXGISwapChain1** outSwapChain)
{
    if (!dxgiFactory) return;

    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = width;
    sd.Height = height;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED; // ← kunci DComp transparency
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

    HRESULT hr = dxgiFactory->CreateSwapChainForComposition(
        device.Get(), &sd, nullptr, outSwapChain);
    _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

    Microsoft::WRL::ComPtr<IDXGISwapChain2> sc2;
    if (SUCCEEDED((*outSwapChain)->QueryInterface(IID_PPV_ARGS(&sc2))))
        sc2->SetMaximumFrameLatency(1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Alpha Blend State (straight alpha — untuk konten non-DComp)
// ─────────────────────────────────────────────────────────────────────────────
ID3D11BlendState* Graphics::GetAlphaBlendState()
{
    if (alphaBlendState)
        return alphaBlendState.Get();

    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    HRESULT hr = device->CreateBlendState(&blendDesc, alphaBlendState.GetAddressOf());
    if (FAILED(hr)) return nullptr;
    return alphaBlendState.Get();
}