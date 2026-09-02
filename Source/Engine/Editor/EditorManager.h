#pragma once

#include <d3d11.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>
#include <implot.h>
#include <utility>
#include <wrl/client.h>
#include "System/Graphics.h"
#include "System/Logger.h"
#include "Framework.h"
#include "GameObject.h"
#include "LightComponent.h"
#include "ProfilerManager.h"
#include "Scene.h"
#include "SceneGame.h"
#include "SceneSerializer.h"
#include "SceneTitle.h"
#include "WindowManager.h"

// Scoped state machine for the engine's runtime
enum class EditorMode : std::uint8_t
{
    Edit = 0,
    Play,
    Pause
};

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

    void Draw(Scene* currentScene, Camera* activeCamera) noexcept;

    void BeginSceneRender(ID3D11DeviceContext* context) noexcept;
    void EndSceneRender(ID3D11DeviceContext* context) noexcept;

    // Safely clears the active inspector target to prevent dangling pointers
    void ClearSelection() noexcept { m_selectedObject = nullptr; }

    // State accessors for the Game loop to query
    [[nodiscard]] EditorMode GetEditorMode() const noexcept { return m_editorMode; }
    void SetEditorMode(EditorMode mode) noexcept { m_editorMode = mode; }

private:
    EditorManager() = default;
    ~EditorManager() = default;

    void ApplyStyle() const noexcept;
    void DrawDockSpace(Scene* currentScene) noexcept;
    void DrawSceneView(Scene* currentScene, Camera* activeCamera) noexcept;
    void EnsureSceneRenderTarget(UINT width, UINT height) noexcept;
    void DrawMenuBar(Scene* currentScene) noexcept;
    void DrawHierarchyNode(GameObject* node) noexcept;
    GameObject* m_selectedObject{ nullptr }; // Tracks what the user clicked on
    void DrawHierarchy(Scene* currentScene) noexcept;
    void DrawInspector(Scene* currentScene) noexcept;
    void DrawConsole() const noexcept;
    void DrawProfiler() const noexcept;
    void DrawPostProcess(Scene* currentScene) noexcept;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_sceneTexture;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_sceneRTV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_sceneSRV;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_depthTexture;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_sceneDSV;

    // Gizmo State
    ImGuizmo::OPERATION m_gizmoOperation{ ImGuizmo::TRANSLATE };
    ImGuizmo::MODE      m_gizmoMode{ ImGuizmo::WORLD };

    // Release vs Editor Boot State
#ifdef _DEBUG
    EditorMode m_editorMode{ EditorMode::Edit };
#else
    EditorMode m_editorMode{ EditorMode::Play }; // Release builds strictly bypass the Editor
#endif

    float m_sceneWidth{ 1920.0f };
    float m_sceneHeight{ 1080.0f };

    bool m_showPostProcess{ false };
    bool m_showProfiler{ false };
};