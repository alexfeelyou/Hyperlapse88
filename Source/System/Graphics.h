#pragma once

#include <d3d11.h>
#include <dxgi1_5.h>   // IDXGIFactory5 untuk cek tearing support
#include <dxgi1_2.h>
#include <dcomp.h>     // DirectComposition
#include <wrl.h>
#include <memory>
#include "RenderState.h"
#include "PrimitiveRenderer.h"
#include "ShapeRenderer.h"
#include "ModelRenderer.h"

#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "dxgi.lib")

class Graphics
{
private:
    Graphics() = default;
    ~Graphics() = default;

public:
    void SetClearColor(float r, float g, float b) {
        clearColor[0] = r;
        clearColor[1] = g;
        clearColor[2] = b;
        clearColor[3] = 1.0f;
    }

    const float* GetClearColor() const { return clearColor; }

    static Graphics& Instance() { static Graphics i; return i; }

    void Initialize();

    // Normal window: swap chain langsung ke HWND
    void CreateSwapChainForHwnd(HWND hWnd, int width, int height, IDXGISwapChain1** outSwapChain);

    // Transparent window: swap chain untuk DirectComposition (tanpa HWND)
    // AlphaMode = PREMULTIPLIED — shader harus output rgb * a !
    void CreateSwapChainForComposition(int width, int height, IDXGISwapChain1** outSwapChain);

    // Legacy wrapper — sekarang delegate ke CreateSwapChainForHwnd
    void CreateSwapChain(HWND hWnd, int width, int height, bool /*isTransparent*/, IDXGISwapChain1** outSwapChain)
    {
        CreateSwapChainForHwnd(hWnd, width, height, outSwapChain);
    }

    ID3D11Device* GetDevice() { return device.Get(); }
    ID3D11DeviceContext* GetDeviceContext() { return immediateContext.Get(); }

    // Dipakai DCompositionCreateDevice() di Window::Initialize
    IDXGIDevice* GetDXGIDevice() { return dxgiDevice.Get(); }

    RenderState* GetRenderState() { return renderState.get(); }
    PrimitiveRenderer* GetPrimitiveRenderer() const { return primitiveRenderer.get(); }
    ShapeRenderer* GetShapeRenderer()     const { return shapeRenderer.get(); }
    ModelRenderer* GetModelRenderer()     const { return modelRenderer.get(); }
    ID3D11BlendState* GetAlphaBlendState();

    // True jika adapter mendukung variable-refresh / G-Sync / FreeSync
    bool IsTearingSupported() const { return m_tearingSupported; }

private:
    Microsoft::WRL::ComPtr<ID3D11Device>        device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> immediateContext;
    Microsoft::WRL::ComPtr<IDXGIFactory2>       dxgiFactory;
    Microsoft::WRL::ComPtr<IDXGIDevice1>        dxgiDevice;   // disimpan agar DComp bisa pakai
    Microsoft::WRL::ComPtr<ID3D11BlendState>    alphaBlendState;

    std::unique_ptr<RenderState>       renderState;
    std::unique_ptr<PrimitiveRenderer> primitiveRenderer;
    std::unique_ptr<ShapeRenderer>     shapeRenderer;
    std::unique_ptr<ModelRenderer>     modelRenderer;

    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    bool  m_tearingSupported = false;
};