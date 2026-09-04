#pragma once

#include <d3d11.h>
#include <dxgi1_5.h>   
#include <dxgi1_2.h>
#include <dcomp.h>     
#include <wrl.h>
#include <memory>
#include "Light.h"
#include "ModelRenderer.h"
#include "PrimitiveRenderer.h"
#include "RenderState.h"
#include "ShapeRenderer.h"

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

    void CreateSwapChainForHwnd(HWND hWnd, int width, int height, IDXGISwapChain1** outSwapChain);

    ID3D11Device* GetDevice() { return device.Get(); }
    ID3D11DeviceContext* GetDeviceContext() { return immediateContext.Get(); }

    RenderState* GetRenderState() { return renderState.get(); }
    PrimitiveRenderer* GetPrimitiveRenderer() const { return primitiveRenderer.get(); }
    ShapeRenderer* GetShapeRenderer()     const { return shapeRenderer.get(); }
    ModelRenderer* GetModelRenderer()     const { return modelRenderer.get(); }
    LightManager& GetLightManager() const { return *lightManager; }
    ID3D11BlendState* GetAlphaBlendState();

	// True if the system supports tearing (variable refresh rate), false otherwise
    bool IsTearingSupported() const { return m_tearingSupported; }

private:
    Microsoft::WRL::ComPtr<ID3D11Device>        device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> immediateContext;
    Microsoft::WRL::ComPtr<IDXGIFactory2>       dxgiFactory;
    Microsoft::WRL::ComPtr<IDXGIDevice1>        dxgiDevice;   
    Microsoft::WRL::ComPtr<ID3D11BlendState>    alphaBlendState;

    std::unique_ptr<RenderState>       renderState;
    std::unique_ptr<PrimitiveRenderer> primitiveRenderer;
    std::unique_ptr<ShapeRenderer>     shapeRenderer;
    std::unique_ptr<ModelRenderer>     modelRenderer;
    std::unique_ptr<LightManager>      lightManager;

    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    bool  m_tearingSupported = false;
};