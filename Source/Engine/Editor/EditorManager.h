#pragma once
#include <d3d11.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <utility>
#include <wrl/client.h>
#include "System/Graphics.h"
#include "System/Logger.h"
#include "Framework.h"
#include "Scene.h"
#include "SceneGame.h"
#include "SceneTitle.h"
#include "WindowManager.h"

// Manages out-of-scene debug UI (docking, menu bars, panels)
// Ensures debug tools persist across scene
class EditorManager
{
public:
    // Delete copy/move constructors to enforce strict singleton ownership
    EditorManager(const EditorManager&) = delete;
    EditorManager& operator=(const EditorManager&) = delete;
    EditorManager(EditorManager&&) = delete;
    EditorManager& operator=(EditorManager&&) = delete;

    // Returns a reference to the static local instance
    [[nodiscard]] static EditorManager& Instance() noexcept;

    // Configures ImGui context settings (e.g., docking)
    void Initialize() noexcept;

    // Dispatches UI drawing
    void Draw(Scene* currentScene) noexcept;

    void BeginSceneRender(ID3D11DeviceContext* context) noexcept;
    void EndSceneRender(ID3D11DeviceContext* context) noexcept;

private:
    EditorManager() = default;
    ~EditorManager() = default;

    void ApplyStyle() const noexcept;
    void DrawDockSpace() noexcept;
    void DrawSceneView() noexcept;
    void EnsureSceneRenderTarget(UINT width, UINT height) noexcept;
    void DrawMenuBar() noexcept;
    void DrawHierarchy() const noexcept;
    void DrawInspector(Scene* currentScene) const noexcept;
    void DrawConsole() const noexcept;
    void DrawProfiler() const noexcept;
    void DrawPostProcess(Scene* currentScene) noexcept;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_sceneTexture;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_sceneRTV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_sceneSRV;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_depthTexture;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_sceneDSV;

    float m_sceneWidth{ 1920.0f };
    float m_sceneHeight{ 1080.0f };

    bool m_showPostProcess{ false };
};