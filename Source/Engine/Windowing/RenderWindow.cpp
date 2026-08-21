#include "RenderWindow.h"

namespace platform
{
    Window::Window() {}

    Window::~Window()
    {
        if (m_sdlWindow) {
            SDL_DestroyWindow(m_sdlWindow);
            m_sdlWindow = nullptr;
        }
    }

    SDL_HitTestResult SDLCALL WindowHitTestCallback(SDL_Window* win, const SDL_Point* area, void* data)
    {
        Window* pWindow = static_cast<Window*>(data);
        if (!pWindow) return SDL_HITTEST_NORMAL;

        if (pWindow->IsDraggable()) {
            return SDL_HITTEST_DRAGGABLE;
        }

        return SDL_HITTEST_NORMAL;
    }

    bool Window::Initialize(const char* title, int width, int height)
    {
        m_width = width;
        m_height = height;

        SDL_WindowFlags flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE;

        m_sdlWindow = SDL_CreateWindow(title, width, height, flags);
        if (!m_sdlWindow) return false;

        SDL_SetWindowHitTest(m_sdlWindow, WindowHitTestCallback, this);

        return SetupDirectX();
    }

    bool Window::SetupDirectX()
    {
        auto device = Graphics::Instance().GetDevice();

        HWND hwnd = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(m_sdlWindow), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
        if (!hwnd) return false;

        Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
        device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
        Microsoft::WRL::ComPtr<IDXGIAdapter> dxgiAdapter;
        dxgiDevice->GetAdapter(&dxgiAdapter);
        Microsoft::WRL::ComPtr<IDXGIFactory2> dxgiFactory;
        dxgiAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory));

        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
        swapChainDesc.Width = m_width;
        swapChainDesc.Height = m_height;
        swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.BufferCount = 2;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

        HRESULT hr;

        swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        hr = dxgiFactory->CreateSwapChainForHwnd(
            device, hwnd, &swapChainDesc, nullptr, nullptr, &m_swapChain
        );
        if (FAILED(hr)) return false;

        // Render Target View
        Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
        m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_renderTargetView);

        // Depth Stencil
        D3D11_TEXTURE2D_DESC depthDesc = {};
        depthDesc.Width = m_width;
        depthDesc.Height = m_height;
        depthDesc.MipLevels = 1;
        depthDesc.ArraySize = 1;
        depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.Usage = D3D11_USAGE_DEFAULT;
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> depthStencil;
        device->CreateTexture2D(&depthDesc, nullptr, &depthStencil);
        device->CreateDepthStencilView(depthStencil.Get(), nullptr, &m_depthStencilView);

        return true;
    }

    void Window::BeginRender(float r, float g, float b, float a)
    {
        auto context = Graphics::Instance().GetDeviceContext();

		// Clear the render target and depth stencil
        float clearColor[4] = { r, g, b, a };

        context->ClearRenderTargetView(m_renderTargetView.Get(), clearColor);

        if (m_depthStencilView) {
            context->ClearDepthStencilView(m_depthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
        }

        context->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), m_depthStencilView.Get());

        D3D11_VIEWPORT vp = {};
        vp.Width = (float)m_width;
        vp.Height = (float)m_height;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        context->RSSetViewports(1, &vp);
    }

    void Window::EndRender(int syncInterval)
    {
        if (m_swapChain) {
            m_swapChain->Present(syncInterval, 0);
        }
    }

    void Window::Resize(int width, int height)
    {
        if (width <= 0 || height <= 0 || (width == m_width && height == m_height)) return;

        m_width = width;
        m_height = height;

        if (!m_swapChain) return;

        auto context = Graphics::Instance().GetDeviceContext();
        context->OMSetRenderTargets(0, nullptr, nullptr);
        m_renderTargetView.Reset();
        m_depthStencilView.Reset();

        m_swapChain->ResizeBuffers(2, m_width, m_height, DXGI_FORMAT_B8G8R8A8_UNORM, 0);

        auto device = Graphics::Instance().GetDevice();
        Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
        m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_renderTargetView);

        D3D11_TEXTURE2D_DESC depthDesc = {};
        depthDesc.Width = m_width;
        depthDesc.Height = m_height;
        depthDesc.MipLevels = 1;
        depthDesc.ArraySize = 1;
        depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.Usage = D3D11_USAGE_DEFAULT;
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> depthStencil;
        device->CreateTexture2D(&depthDesc, nullptr, &depthStencil);
        device->CreateDepthStencilView(depthStencil.Get(), nullptr, &m_depthStencilView);
    }

    void Window::SetTitle(const char* title) {
        if (m_sdlWindow) SDL_SetWindowTitle(m_sdlWindow, title);
    }

    void Window::SetAlwaysOnTop(bool isTop) {
        if (m_sdlWindow) SDL_SetWindowAlwaysOnTop(m_sdlWindow, isTop);
    }
}