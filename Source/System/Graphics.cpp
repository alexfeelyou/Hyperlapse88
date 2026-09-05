#include "Graphics.h"
#include "Misc.h"
#include "EffectManager.h"
#include "SkyboxRenderer.h"

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

    hr = device.As(&dxgiDevice);
    _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
    dxgiDevice->SetMaximumFrameLatency(1); 

    {
        Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        hr = dxgiDevice->GetAdapter(adapter.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

        hr = adapter->GetParent(__uuidof(IDXGIFactory2), (void**)dxgiFactory.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
    }

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

    renderState = std::make_unique<RenderState>(device.Get());
    primitiveRenderer = std::make_unique<PrimitiveRenderer>(device.Get());
    shapeRenderer = std::make_unique<ShapeRenderer>(device.Get());
    modelRenderer = std::make_unique<ModelRenderer>(device.Get());
    lightManager = std::make_unique<LightManager>();
    skyboxRenderer = std::make_unique<SkyboxRenderer>();

    EffectManager::Instance().Initialize(device.Get(), immediateContext.Get());
    skyboxRenderer->Initialize(device.Get());
}

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

    sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    if (m_tearingSupported)
        sd.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

    HRESULT hr = dxgiFactory->CreateSwapChainForHwnd(
        device.Get(), hWnd, &sd, nullptr, nullptr, outSwapChain);
    _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

    dxgiFactory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);

    Microsoft::WRL::ComPtr<IDXGISwapChain2> sc2;
    if (SUCCEEDED((*outSwapChain)->QueryInterface(IID_PPV_ARGS(&sc2))))
        sc2->SetMaximumFrameLatency(1);
}

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