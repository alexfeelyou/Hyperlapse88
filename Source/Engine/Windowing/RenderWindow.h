#pragma once

#include <SDL3/SDL.h>
#include <d3d11.h>
#include <dxgi1_3.h> 
#include <wrl.h>
#include <functional>
#include "System/Graphics.h"
#include <windows.h>

class Camera;

namespace platform
{
    class Window
    {
    public:
        Window();
        ~Window();

        bool Initialize(const char* title, int width, int height);

        void BeginRender(float r = 0.0f, float g = 0.0f, float b = 0.0f, float a = 1.0f);
        void EndRender(int syncInterval = 1);
        void Resize(int width, int height);

        SDL_Window* GetSDLWindow()    const { return m_sdlWindow; }
        int         GetWidth()        const { return m_width; }
        int         GetHeight()       const { return m_height; }

        void    SetCamera(Camera* cam) { m_targetCamera = cam; }
        Camera* GetCamera() const { return m_targetCamera; }

        void SetTitle(const char* title);
        void SetDraggable(bool isDraggable) { m_isDraggable = isDraggable; }
        bool IsDraggable() const { return m_isDraggable; }
        void SetTargetFPS(float fps) { m_targetFPS = fps; }
        void SetTickCallback(std::function<void()> callback) { m_tickCallback = callback; }
        void SetBorderVisible(bool visible) { m_borderVisible = visible; }
		void SetRenderScene(bool render) { m_shouldRenderScene = render; }
		bool ShouldRenderScene() const { return m_shouldRenderScene; }

    private:
        bool SetupDirectX();

        SDL_Window* m_sdlWindow = nullptr;

        int  m_width = 0;
        int  m_height = 0;
        bool m_isDraggable = true;
        bool m_borderVisible = false;

		bool m_shouldRenderScene = true;

        Camera* m_targetCamera = nullptr;
        std::function<void()> m_tickCallback;
        float m_targetFPS = 0.0f;

        // DirectX 11 SwapChain
        Microsoft::WRL::ComPtr<IDXGISwapChain1>        m_swapChain;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_renderTargetView;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_depthStencilView;
    };
}