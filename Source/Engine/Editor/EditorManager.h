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
#include "AnimationGraphPanel.h"
#include "AnimationTimelinePanel.h"
#include "Framework.h"
#include "GameObject.h"
#include "IComponent.h"
#include "ProfilerManager.h"
#include "Scene.h"
#include "SceneGame.h"
#include "SceneSerializer.h"
#include "SceneTitle.h"
#include "WindowManager.h"
#include <string_view>

// Interface for polymorphic undo/redo command execution
class IUndoAction
{
public:
    virtual ~IUndoAction() = default;
    virtual void Undo(Scene* scene) = 0;
    virtual void Redo(Scene* scene) = 0;
    [[nodiscard]] virtual std::string_view GetName() const noexcept = 0;
};

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

    // Opens the Animation Graph Panel
    void OpenAnimationGraph(AnimationComponent* target) noexcept;

	// Opens the Animation Timeline Panel for a specific AnimationComponent
    void OpenAnimationTimeline(AnimationComponent* target, std::size_t stateIndex = 0) noexcept;

    // Safely clears the active inspector target to prevent dangling pointers
    void ClearSelection() noexcept {
        m_selectedObject = nullptr;
        m_selectedObjects.clear();
        m_selectionAnchor = nullptr;
        m_selectedComponent = nullptr;
        m_deferredDeselectNode = nullptr;
        m_timelinePanel.SetTarget(nullptr, 0);
        m_animGraphPanel.SetTarget(nullptr);
    }

    // Transactional Undo / Redo Pipeline
    void Undo(Scene* currentScene) noexcept;
    void Redo(Scene* currentScene) noexcept;
    [[nodiscard]] bool CanUndo() const noexcept;
    [[nodiscard]] bool CanRedo() const noexcept;
    [[nodiscard]] std::string_view GetUndoActionName() const noexcept;
    [[nodiscard]] std::string_view GetRedoActionName() const noexcept;
    void PushUndoAction(std::unique_ptr<class IUndoAction> action) noexcept;
    void ClearUndoHistory() noexcept;

    // Accessors for the currently selected GameObject and Component
    [[nodiscard]] GameObject* GetSelectedObject() const noexcept { return m_selectedObject; }
    [[nodiscard]] const std::vector<GameObject*>& GetSelectedObjects() const noexcept { return m_selectedObjects; }
    [[nodiscard]] bool IsObjectSelected(const GameObject* node) const noexcept;
    [[nodiscard]] IComponent* GetSelectedComponent() const noexcept { return m_selectedComponent; }
    void SetSelectedComponent(IComponent* comp) noexcept { m_selectedComponent = comp; }

    // Gizmo State Accessors
    [[nodiscard]] bool GetShowGizmos() const noexcept { return m_showGizmos; }
    [[nodiscard]] std::uint32_t GetGizmoMask() const noexcept { return m_gizmoMask; }

    // State accessors for the Game loop to query
    [[nodiscard]] EditorMode GetEditorMode() const noexcept { return m_editorMode; }
    void SetEditorMode(EditorMode mode) noexcept { m_editorMode = mode; }

    // User Workspace Preferences (Ignored by Git)
    void SaveUserPreferences(Scene* currentScene, Camera* activeCamera) const noexcept;
    void LoadUserPreferences(Scene* currentScene, Camera* activeCamera) noexcept;

private:
    static constexpr std::string_view s_editorPrefsPath{ "UserSettings/EditorPreferences.json" };

    EditorManager() = default;
    ~EditorManager();

    void ApplyStyle() const noexcept;
    void DrawDockSpace(Scene* currentScene) noexcept;
    void DrawSceneView(Scene* currentScene, Camera* activeCamera) noexcept;
    void EnsureSceneRenderTarget(UINT width, UINT height) noexcept;
    void DrawMenuBar(Scene* currentScene) noexcept;
    void DrawHierarchyNode(GameObject* node, Scene* currentScene) noexcept;
    void DuplicateGameObject(GameObject* target, Scene* currentScene) noexcept;
    void DuplicateSelected(Scene* currentScene) noexcept;
    void GroupSelected(Scene* currentScene) noexcept;
    void ParentSelectedToActive() noexcept;
    void UnparentSelected(Scene* currentScene) noexcept;
    void ToggleActiveSelected() noexcept;
    void DeleteSelected() noexcept;
    void ReparentNodes(const std::vector<GameObject*>& nodes, GameObject* newParent) noexcept;
    void SetSelectedObject(GameObject* target, bool isToggle = false) noexcept;
    void ApplySelection(GameObject* target, bool isCtrl, bool isShift) noexcept;
    GameObject* m_selectedObject{ nullptr }; // Primary selected GameObject (for Inspector/Gizmo)
    std::vector<GameObject*> m_selectedObjects{}; // Multi-selection collection
    GameObject* m_selectionAnchor{ nullptr }; // Range anchor for Shift-selection
    std::vector<GameObject*> m_linearHierarchyNodes{}; // Frame cache of visible hierarchy order
    GameObject* m_pendingClickNode{ nullptr };
    GameObject* m_deferredDeselectNode{ nullptr }; // Node to single-select on mouse up if no drag occurred
    bool m_pendingCtrl{ false };
    bool m_pendingShift{ false };
    IComponent* m_selectedComponent{ nullptr }; // Tracks what the user clicked on

    // Undo / Redo Transaction Buffers
    static constexpr std::size_t s_maxUndoHistory{ 64 };
    std::vector<std::unique_ptr<class IUndoAction>> m_undoStack{};
    std::vector<std::unique_ptr<class IUndoAction>> m_redoStack{};

    // Inspector Edit Memento State
    nlohmann::json m_inspectorSnapshotBefore{};
    GameObject* m_inspectorEditingObject{ nullptr };
    bool m_isInspectorEditing{ false };

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

	// Animation Graph Panel
    AnimationGraphPanel m_animGraphPanel{};
    bool m_showAnimGraph{ false };

	// Animation Timeline Panel
    AnimationTimelinePanel m_timelinePanel{};
    bool m_showAnimTimeline{ false };

    // Gizmo State
    ImGuizmo::OPERATION m_gizmoOperation{ ImGuizmo::TRANSLATE };
    ImGuizmo::MODE      m_gizmoMode{ ImGuizmo::WORLD };
    bool m_showGizmos{ true };
    std::uint32_t m_gizmoMask{ static_cast<std::uint32_t>(GizmoCategory::Cameras) };

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