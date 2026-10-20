#include <filesystem>
#include <fstream>
#include <json.hpp>
#include "Camera.h"
#include "CameraComponent.h"
#include "CapsuleColliderComponent.h"
#include "EditorManager.h"
#include "LightComponent.h"
#include "StaticMeshColliderComponent.h"
#include "VirtualCameraComponent.h"

namespace
{
    // Evaluate debug state at compile-time 
#ifdef _DEBUG
    inline constexpr bool s_isDebugMode{ true };
#else
    inline constexpr bool s_isDebugMode{ false };
#endif

    // Shared compile-time pointers for C-API compatibility
    inline constexpr const char* s_windowSceneView{ "Scene View" };
    inline constexpr const char* s_windowHierarchy{ "Hierarchy" };
    inline constexpr const char* s_windowInspector{ "Inspector" };
    inline constexpr const char* s_windowConsole{ "Console" };
    inline constexpr const char* s_windowProfiler{ "Profiler" };
    inline constexpr const char* s_windowPostProcess{ "Post-Processing" };

    // Tracks the fullscreen and docking restoration state of the Scene View tab
    static bool s_isSceneMaximized{ false };
    static ImGuiID s_previousDockId{ 0 };
    static bool s_restoreDock{ false };
}

namespace
{
    [[nodiscard]] nlohmann::json SnapshotGameObject(const GameObject* node)
    {
        if (!node) return nlohmann::json{};

        nlohmann::json j{};
        j["Name"] = node->GetName();
        j["IsActive"] = node->IsActive();
        j["PosX"] = node->transform.position.x;
        j["PosY"] = node->transform.position.y;
        j["PosZ"] = node->transform.position.z;
        j["RotX"] = node->transform.rotation.x;
        j["RotY"] = node->transform.rotation.y;
        j["RotZ"] = node->transform.rotation.z;
        j["ScaleX"] = node->transform.scale.x;
        j["ScaleY"] = node->transform.scale.y;
        j["ScaleZ"] = node->transform.scale.z;

        nlohmann::json compArray = nlohmann::json::array();
        for (const auto& comp : node->GetComponents())
        {
            if (!comp) continue;
            nlohmann::json compJson{};
            compJson["Type"] = comp->GetTypeName();
            comp->Serialize(compJson);
            compArray.push_back(std::move(compJson));
        }
        j["Components"] = std::move(compArray);

        return j;
    }

    void ApplyGameObjectSnapshot(GameObject* node, const nlohmann::json& j)
    {
        if (!node || !j.is_object() || j.empty()) return;

        node->SetName(j.value("Name", node->GetName()));
        node->SetActive(j.value("IsActive", node->IsActive()));
        node->transform.position = {
            j.value("PosX", node->transform.position.x),
            j.value("PosY", node->transform.position.y),
            j.value("PosZ", node->transform.position.z)
        };
        node->transform.rotation = {
            j.value("RotX", node->transform.rotation.x),
            j.value("RotY", node->transform.rotation.y),
            j.value("RotZ", node->transform.rotation.z)
        };
        node->transform.scale = {
            j.value("ScaleX", node->transform.scale.x),
            j.value("ScaleY", node->transform.scale.y),
            j.value("ScaleZ", node->transform.scale.z)
        };

        if (j.contains("Components"))
        {
            for (const auto& compJson : j["Components"])
            {
                const std::string type{ compJson.value("Type", "") };
                for (const auto& comp : node->GetComponents())
                {
                    if (comp && comp->GetTypeName() == type)
                    {
                        comp->Deserialize(compJson);
                        break;
                    }
                }
            }
        }
    }

    class TransformGizmoAction final : public IUndoAction
    {
    public:
        TransformGizmoAction(GameObject* target,
            const DirectX::XMFLOAT3& oldPos, const DirectX::XMFLOAT3& oldRot, const DirectX::XMFLOAT3& oldScale,
            const DirectX::XMFLOAT3& newPos, const DirectX::XMFLOAT3& newRot, const DirectX::XMFLOAT3& newScale)
            : m_target{ target }
            , m_oldPos{ oldPos }, m_oldRot{ oldRot }, m_oldScale{ oldScale }
            , m_newPos{ newPos }, m_newRot{ newRot }, m_newScale{ newScale }
        {}

        void Undo(Scene*) override
        {
            if (!m_target || m_target->IsDestroyed()) return;
            m_target->transform.position = m_oldPos;
            m_target->transform.rotation = m_oldRot;
            m_target->transform.scale = m_oldScale;
        }

        void Redo(Scene*) override
        {
            if (!m_target || m_target->IsDestroyed()) return;
            m_target->transform.position = m_newPos;
            m_target->transform.rotation = m_newRot;
            m_target->transform.scale = m_newScale;
        }

        [[nodiscard]] std::string_view GetName() const noexcept override { return "Transform Gizmo"; }

    private:
        GameObject* m_target{ nullptr };
        DirectX::XMFLOAT3 m_oldPos{};
        DirectX::XMFLOAT3 m_oldRot{};
        DirectX::XMFLOAT3 m_oldScale{};
        DirectX::XMFLOAT3 m_newPos{};
        DirectX::XMFLOAT3 m_newRot{};
        DirectX::XMFLOAT3 m_newScale{};
    };

    class InspectorEditAction final : public IUndoAction
    {
    public:
        InspectorEditAction(GameObject* target, nlohmann::json beforeState, nlohmann::json afterState)
            : m_target{ target }
        {
            // Direct assignment avoids nlohmann::json initializer_list wrapping the object into an array
            m_beforeState = std::move(beforeState);
            m_afterState = std::move(afterState);
        }

        void Undo(Scene*) override
        {
            if (!m_target || m_target->IsDestroyed()) return;
            ApplyGameObjectSnapshot(m_target, m_beforeState);
        }

        void Redo(Scene*) override
        {
            if (!m_target || m_target->IsDestroyed()) return;
            ApplyGameObjectSnapshot(m_target, m_afterState);
        }

        [[nodiscard]] std::string_view GetName() const noexcept override { return "Inspector Edit"; }

    private:
        GameObject* m_target{ nullptr };
        nlohmann::json m_beforeState{};
        nlohmann::json m_afterState{};
    };

    struct ReparentRecord
    {
        GameObject* node{ nullptr };
        GameObject* oldParent{ nullptr };
        DirectX::XMFLOAT3 oldPos{};
        DirectX::XMFLOAT3 oldRot{};
        DirectX::XMFLOAT3 oldScale{};
        GameObject* newParent{ nullptr };
        DirectX::XMFLOAT3 newPos{};
        DirectX::XMFLOAT3 newRot{};
        DirectX::XMFLOAT3 newScale{};
    };

    class ReparentAction final : public IUndoAction
    {
    public:
        explicit ReparentAction(std::vector<ReparentRecord> records, std::string_view name = "Reparent")
            : m_records{ std::move(records) }
            , m_name{ name }
        {}

        void Undo(Scene*) override
        {
            for (const auto& rec : m_records)
            {
                if (rec.node && rec.oldParent && !rec.node->IsDestroyed() && !rec.oldParent->IsDestroyed())
                {
                    rec.node->SetParent(rec.oldParent);
                    rec.node->transform.position = rec.oldPos;
                    rec.node->transform.rotation = rec.oldRot;
                    rec.node->transform.scale = rec.oldScale;
                }
            }
        }

        void Redo(Scene*) override
        {
            for (const auto& rec : m_records)
            {
                if (rec.node && rec.newParent && !rec.node->IsDestroyed() && !rec.newParent->IsDestroyed())
                {
                    rec.node->SetParent(rec.newParent);
                    rec.node->transform.position = rec.newPos;
                    rec.node->transform.rotation = rec.newRot;
                    rec.node->transform.scale = rec.newScale;
                }
            }
        }

        [[nodiscard]] std::string_view GetName() const noexcept override { return m_name; }

    private:
        std::vector<ReparentRecord> m_records{};
        std::string m_name{};
    };

    class CreateObjectAction final : public IUndoAction
    {
    public:
        CreateObjectAction(GameObject* parent, GameObject* rawNode)
            : m_parent{ parent }
            , m_rawNode{ rawNode }
        {}

        void Undo(Scene*) override
        {
            if (m_parent && m_rawNode && !m_ownedNode)
            {
                m_ownedNode = m_parent->RemoveChild(m_rawNode);
            }
        }

        void Redo(Scene*) override
        {
            if (m_parent && m_ownedNode)
            {
                m_rawNode = m_ownedNode.get();
                m_parent->AddChild(std::move(m_ownedNode));
            }
        }

        [[nodiscard]] std::string_view GetName() const noexcept override { return "Create GameObject"; }

    private:
        GameObject* m_parent{ nullptr };
        GameObject* m_rawNode{ nullptr };
        std::unique_ptr<GameObject> m_ownedNode{ nullptr };
    };

    struct DeletedRecord
    {
        GameObject* parent{ nullptr };
        GameObject* rawNode{ nullptr };
        std::unique_ptr<GameObject> storage{ nullptr };
    };

    class DeleteObjectsAction final : public IUndoAction
    {
    public:
        explicit DeleteObjectsAction(std::vector<DeletedRecord> records)
            : m_records{ std::move(records) }
        {}

        void Undo(Scene*) override
        {
            for (auto& rec : m_records)
            {
                if (rec.parent && rec.storage)
                {
                    rec.rawNode = rec.storage.get();
                    rec.parent->AddChild(std::move(rec.storage));
                }
            }
        }

        void Redo(Scene*) override
        {
            for (auto& rec : m_records)
            {
                if (rec.parent && rec.rawNode && !rec.storage)
                {
                    rec.storage = rec.parent->RemoveChild(rec.rawNode);
                }
            }
        }

        [[nodiscard]] std::string_view GetName() const noexcept override { return "Delete GameObject(s)"; }

    private:
        std::vector<DeletedRecord> m_records{};
    };
}

namespace
{
    enum class ToolbarIcon : std::uint8_t
    {
        Play = 0,
        Pause,
        Stop,
        Maximize,
        Undo,
        Redo
    };

    [[nodiscard]] bool DrawToolbarIconButton(
        const char* strId,
        ToolbarIcon icon,
        bool isActive,
        const ImVec4& activeColor,
        const ImVec2& buttonSize = ImVec2{ 34.0f, 22.0f }) noexcept
    {
        if (isActive)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ activeColor.x * 1.15f, activeColor.y * 1.15f, activeColor.z * 1.15f, 1.0f });
        }

        // Empty label with ## so ImGui only renders the button body
        const bool isClicked{ ImGui::Button(strId, buttonSize) };

        if (isActive)
        {
            ImGui::PopStyleColor(2);
        }

        ImDrawList* drawList{ ImGui::GetWindowDrawList() };
        const ImVec2 rectMin{ ImGui::GetItemRectMin() };
        const ImVec2 rectMax{ ImGui::GetItemRectMax() };

        // Floor the center to exact integer pixels to prevent subpixel edge-blurring
        const ImVec2 center{
            std::floor((rectMin.x + rectMax.x) * 0.5f),
            std::floor((rectMin.y + rectMax.y) * 0.5f)
        };

        constexpr ImU32 iconColor{ IM_COL32(235, 235, 235, 255) };

        switch (icon)
        {
        case ToolbarIcon::Play:
        {
            constexpr float halfHeight{ 3.5f };
            constexpr float halfWidth{ 4.5f };
            const ImVec2 p1{ center.x - halfWidth + 1.0f, center.y - halfHeight };
            const ImVec2 p2{ center.x - halfWidth + 1.0f, center.y + halfHeight };
            const ImVec2 p3{ center.x + halfWidth + 1.0f, center.y };
            drawList->AddTriangleFilled(p1, p2, p3, iconColor);
            break;
        }
        case ToolbarIcon::Pause:
        {
            constexpr float barHalfHeight{ 4.0f };
            constexpr float barWidth{ 2.5f };
            constexpr float barGap{ 1.5f };
            constexpr float rounding{ 0.5f };

            drawList->AddRectFilled(
                ImVec2{ center.x - barGap - barWidth, center.y - barHalfHeight },
                ImVec2{ center.x - barGap,            center.y + barHalfHeight },
                iconColor, rounding
            );
            drawList->AddRectFilled(
                ImVec2{ center.x + barGap,            center.y - barHalfHeight },
                ImVec2{ center.x + barGap + barWidth, center.y + barHalfHeight },
                iconColor, rounding
            );
            break;
        }
        case ToolbarIcon::Stop:
        {
            constexpr float halfSize{ 4.0f };
            constexpr float rounding{ 0.5f };
            drawList->AddRectFilled(
                ImVec2{ center.x - halfSize, center.y - halfSize },
                ImVec2{ center.x + halfSize, center.y + halfSize },
                iconColor, rounding
            );
            break;
        }
        case ToolbarIcon::Maximize:
        {
            constexpr float halfSize{ 4.5f };
            constexpr float rounding{ 0.5f };
            // Draw outer frame outline
            drawList->AddRect(
                ImVec2{ center.x - halfSize, center.y - halfSize },
                ImVec2{ center.x + halfSize, center.y + halfSize },
                iconColor, rounding, 0, 1.5f
            );
            // Draw inner square when in "Restore" state
            if (isActive)
            {
                drawList->AddRectFilled(
                    ImVec2{ center.x - 2.0f, center.y - 2.0f },
                    ImVec2{ center.x + 2.0f, center.y + 2.0f },
                    iconColor, rounding
                );
            }
            break;
        }
        case ToolbarIcon::Undo:
        {
            constexpr float r{ 4.0f };
            drawList->PathArcTo(ImVec2{ center.x + 1.0f, center.y + 1.0f }, r, DirectX::XM_PI * 1.15f, DirectX::XM_2PI * 0.95f, 6);
            drawList->PathStroke(iconColor, 0, 1.8f);

            const ImVec2 tip{ center.x - 3.5f, center.y + 0.5f };
            drawList->AddTriangleFilled(
                tip,
                ImVec2{ tip.x + 3.5f, tip.y - 3.5f },
                ImVec2{ tip.x + 4.0f, tip.y + 2.0f },
                iconColor
            );
            break;
        }
        case ToolbarIcon::Redo:
        {
            constexpr float r{ 4.0f };
            drawList->PathArcTo(ImVec2{ center.x - 1.0f, center.y + 1.0f }, r, -DirectX::XM_2PI * 0.45f, -DirectX::XM_PI * 0.15f, 6);
            drawList->PathStroke(iconColor, 0, 1.8f);

            const ImVec2 tip{ center.x + 3.5f, center.y + 0.5f };
            drawList->AddTriangleFilled(
                tip,
                ImVec2{ tip.x - 4.0f, tip.y + 2.0f },
                ImVec2{ tip.x - 3.5f, tip.y - 3.5f },
                iconColor
            );
            break;
        }
        }

        return isClicked;
    }
}

EditorManager::~EditorManager() = default;

EditorManager& EditorManager::Instance() noexcept
{
    static EditorManager s_instance{};
    return s_instance;
}

void EditorManager::Initialize() noexcept
{
    if constexpr (!s_isDebugMode) return;

    ImGuiIO& io{ ImGui::GetIO() };
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ApplyStyle();
}

void EditorManager::Draw(Scene* currentScene, Camera* activeCamera) noexcept
{
    if constexpr (!s_isDebugMode) return;

    DrawDockSpace(currentScene);

    DrawSceneView(currentScene, activeCamera);
    DrawHierarchy(currentScene);
    DrawInspector(currentScene);
    DrawProfiler();
    DrawPostProcess(currentScene);
    DrawConsole();

    if (m_showAnimGraph) m_animGraphPanel.Draw(&m_showAnimGraph);
    if (m_showAnimTimeline) m_timelinePanel.Draw(&m_showAnimTimeline);

    ImGui::End();
}

void EditorManager::ApplyStyle() const noexcept
{
    ImGuiStyle& style{ ImGui::GetStyle() };
    ImVec4* colors{ style.Colors };

    ImGui::StyleColorsDark();

    const ImVec4 deepNavyBg{ 0.0f, 0.043f, 0.118f, 1.00f };
    const ImVec4 activeTabBg{ 0.929f, 0.094f, 0.541f, 1.00f };
    const ImVec4 hoverTabBg{ 0.309f, 0.043f, 0.117f, 1.00f };
    const ImVec4 darkTitleBg{ 0.04f, 0.05f, 0.08f, 1.00f };

    colors[ImGuiCol_WindowBg] = deepNavyBg;
    colors[ImGuiCol_ChildBg] = deepNavyBg;
    colors[ImGuiCol_Tab] = hoverTabBg;
    colors[ImGuiCol_TabHovered] = activeTabBg;
    colors[ImGuiCol_TabActive] = activeTabBg;
    colors[ImGuiCol_TabUnfocused] = darkTitleBg;
    colors[ImGuiCol_TabUnfocusedActive] = hoverTabBg;
    colors[ImGuiCol_TabSelectedOverline] = activeTabBg;
    colors[ImGuiCol_TitleBg] = deepNavyBg;
    colors[ImGuiCol_TitleBgActive] = deepNavyBg;
    colors[ImGuiCol_TitleBgCollapsed] = darkTitleBg;
    colors[ImGuiCol_MenuBarBg] = darkTitleBg;
    colors[ImGuiCol_Header] = hoverTabBg;
    colors[ImGuiCol_HeaderHovered] = hoverTabBg;
    colors[ImGuiCol_HeaderActive] = hoverTabBg;
    colors[ImGuiCol_Border] = hoverTabBg;
    colors[ImGuiCol_ResizeGripHovered] = activeTabBg;
    colors[ImGuiCol_ResizeGripActive] = activeTabBg;
    colors[ImGuiCol_DockingPreview] = ImVec4{ activeTabBg.x, activeTabBg.y, activeTabBg.z, 0.40f };
    colors[ImGuiCol_DockingEmptyBg] = deepNavyBg;

    style.WindowRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.TabRounding = 2.0f;
}

void EditorManager::DrawDockSpace(Scene* currentScene) noexcept
{
    const ImGuiViewport* viewport{ ImGui::GetMainViewport() };
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags windowFlags{ ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBackground };
    windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0.0f, 0.0f });

    ImGui::Begin("EditorDockSpace", nullptr, windowFlags);
    ImGui::PopStyleVar(3);

    DrawMenuBar(currentScene);

    const ImGuiID dockspaceId{ ImGui::GetID("MainDockSpace") };

    static bool s_firstTime{ true };
    if (s_firstTime)
    {
        s_firstTime = false;
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_PassthruCentralNode);
        ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

        ImGuiID dockMain{ dockspaceId };
        const ImGuiID dockLeft{ ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.20f, nullptr, &dockMain) };
        const ImGuiID dockRight{ ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.25f, nullptr, &dockMain) };
        const ImGuiID dockBottom{ ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.29f, nullptr, &dockMain) };

        ImGui::DockBuilderDockWindow(s_windowSceneView, dockMain);
        ImGui::DockBuilderDockWindow(s_windowHierarchy, dockLeft);
        ImGui::DockBuilderDockWindow(s_windowInspector, dockRight);
        ImGui::DockBuilderDockWindow(s_windowConsole, dockBottom);
        ImGui::DockBuilderDockWindow(s_windowProfiler, dockBottom);
        ImGui::DockBuilderDockWindow(s_windowPostProcess, dockBottom);
        ImGui::DockBuilderDockWindow("Animation Graph", dockBottom);
        ImGui::DockBuilderDockWindow("AnimEvent Timeline", dockBottom);

        ImGui::DockBuilderFinish(dockspaceId);
    }

    ImGui::DockSpace(dockspaceId, ImVec2{ 0.0f, 0.0f }, ImGuiDockNodeFlags_PassthruCentralNode);
}

void EditorManager::EnsureSceneRenderTarget(UINT width, UINT height) noexcept
{
    if (width == 0 || height == 0) return;
    if (width == (UINT)m_sceneWidth && height == (UINT)m_sceneHeight && m_sceneRTV) return;

    m_sceneWidth = (float)width;
    m_sceneHeight = (float)height;

    auto device = Graphics::Instance().GetDevice();

    m_sceneRTV.Reset(); m_sceneSRV.Reset(); m_sceneTexture.Reset();
    m_sceneDSV.Reset(); m_depthTexture.Reset();

    D3D11_TEXTURE2D_DESC tex{};
    tex.Width = width; tex.Height = height;
    tex.MipLevels = 1; tex.ArraySize = 1;
    tex.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    tex.SampleDesc.Count = 1;
    tex.Usage = D3D11_USAGE_DEFAULT;
    tex.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    device->CreateTexture2D(&tex, nullptr, &m_sceneTexture);
    device->CreateRenderTargetView(m_sceneTexture.Get(), nullptr, &m_sceneRTV);
    device->CreateShaderResourceView(m_sceneTexture.Get(), nullptr, &m_sceneSRV);

    D3D11_TEXTURE2D_DESC depth{};
    depth.Width = width; depth.Height = height;
    depth.MipLevels = 1; depth.ArraySize = 1;
    depth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depth.SampleDesc.Count = 1;
    depth.Usage = D3D11_USAGE_DEFAULT;
    depth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    device->CreateTexture2D(&depth, nullptr, &m_depthTexture);
    device->CreateDepthStencilView(m_depthTexture.Get(), nullptr, &m_sceneDSV);
}

void EditorManager::BeginSceneRender(ID3D11DeviceContext* context) noexcept
{
    if (!m_sceneRTV) return;
    float clear[4]{ 0.0f, 0.0f, 0.0f, 1.0f };
    context->ClearRenderTargetView(m_sceneRTV.Get(), clear);
    context->ClearDepthStencilView(m_sceneDSV.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    context->OMSetRenderTargets(1, m_sceneRTV.GetAddressOf(), m_sceneDSV.Get());

    D3D11_VIEWPORT vp{};
    vp.Width = m_sceneWidth; vp.Height = m_sceneHeight; vp.MaxDepth = 1.0f;
    context->RSSetViewports(1, &vp);
}

void EditorManager::EndSceneRender(ID3D11DeviceContext* context) noexcept
{
    context->OMSetRenderTargets(0, nullptr, nullptr);
}

void EditorManager::DrawSceneView(Scene* currentScene, Camera* activeCamera) noexcept
{
    // Apply previously cached Docking ID to elegantly snap the window back to its place
    if (s_restoreDock)
    {
        ImGui::SetNextWindowDockID(s_previousDockId, ImGuiCond_Always);
        s_restoreDock = false;
    }

    // NoNavInputs blocks arrow keys from triggering ImGui focus highlighting while playing
    ImGuiWindowFlags windowFlags{ ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoNavInputs };

    if (s_isSceneMaximized)
    {
        const ImGuiViewport* viewport{ ImGui::GetMainViewport() };
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);

        windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0.0f, 0.0f });
    }

    ImGui::Begin(s_windowSceneView, nullptr, windowFlags);

    if (s_isSceneMaximized)
    {
        ImGui::PopStyleVar(3);
    }

    constexpr ImVec2 buttonSize{ 34.0f, 22.0f };
    const float totalToolbarWidth{ (buttonSize.x * 5.0f) + (ImGui::GetStyle().ItemSpacing.x * 4.0f) };

    if (s_isSceneMaximized) ImGui::SetCursorPosY(8.0f);

    const float availWidth{ ImGui::GetContentRegionAvail().x };
    ImGui::SetCursorPosX((availWidth * 0.5f) - (totalToolbarWidth * 0.5f));

    // Toolbar Undo Button
    ImGui::BeginDisabled(!CanUndo());
    if (DrawToolbarIconButton("##ToolbarUndoBtn", ToolbarIcon::Undo, false, ImVec4{ 0.3f, 0.3f, 0.3f, 1.0f }, buttonSize))
    {
        Undo(currentScene);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("Undo (Ctrl+Z)");
    }

    ImGui::SameLine();

    // Toolbar Redo Button
    ImGui::BeginDisabled(!CanRedo());
    if (DrawToolbarIconButton("##ToolbarRedoBtn", ToolbarIcon::Redo, false, ImVec4{ 0.3f, 0.3f, 0.3f, 1.0f }, buttonSize))
    {
        Redo(currentScene);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("Redo (Ctrl+Y)");
    }

    ImGui::SameLine();

    static bool s_skipGizmoThisFrame{ false };

    if (DrawToolbarIconButton("##PlayBtn", ToolbarIcon::Play, m_editorMode == EditorMode::Play, ImVec4{ 0.2f, 0.7f, 0.2f, 1.0f }, buttonSize))
    {
        // Undo any leftover timeline scrub (pose + position + capsule) before SceneGame snapshots the scene
        if (m_editorMode == EditorMode::Edit)
        {
            m_timelinePanel.EndPreview();
        }

        SetEditorMode(EditorMode::Play);
        m_showGizmos = false;
        s_skipGizmoThisFrame = true; 
    }
    ImGui::SameLine();
    if (DrawToolbarIconButton("##PauseBtn", ToolbarIcon::Pause, m_editorMode == EditorMode::Pause, ImVec4{ 0.7f, 0.7f, 0.2f, 1.0f }, buttonSize))
    {
        SetEditorMode(EditorMode::Pause);
        m_showGizmos = true;
        s_skipGizmoThisFrame = true; 
    }
    ImGui::SameLine();
    if (DrawToolbarIconButton("##StopBtn", ToolbarIcon::Stop, m_editorMode == EditorMode::Edit, ImVec4{ 0.7f, 0.2f, 0.2f, 1.0f }, buttonSize))
    {
        SetEditorMode(EditorMode::Edit);
        m_showGizmos = true;
        s_skipGizmoThisFrame = true; 
    }

    // Gizmo Category Dropdown aligned to the right
    ImGui::SameLine(availWidth - 140.0f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
    ImGui::Checkbox("Gizmos", &m_showGizmos);

    ImGui::SameLine();
    ImGui::BeginDisabled(!m_showGizmos);
    if (ImGui::Button("▼##GizmoDrop"))
    {
        ImGui::OpenPopup("GizmoSettingsPopup");
    }

    if (ImGui::BeginPopup("GizmoSettingsPopup"))
    {
        ImGui::TextDisabled("GIZMO VISIBILITY");
        ImGui::Separator();

        ImGui::CheckboxFlags("Cameras", &m_gizmoMask, static_cast<std::uint32_t>(GizmoCategory::Cameras));
        ImGui::CheckboxFlags("Static Physics", &m_gizmoMask, static_cast<std::uint32_t>(GizmoCategory::StaticPhysics));
        ImGui::CheckboxFlags("Character Physics", &m_gizmoMask, static_cast<std::uint32_t>(GizmoCategory::DynamicPhysics));
        ImGui::CheckboxFlags("Combat Hitboxes", &m_gizmoMask, static_cast<std::uint32_t>(GizmoCategory::Hitboxes));

        ImGui::EndPopup();
    }
    ImGui::EndDisabled();

    const auto* mainWindow{ WindowManager::Instance().GetWindowByIndex(0) };
    const float gameWidth{ mainWindow ? static_cast<float>(mainWindow->GetWidth()) : 1920.0f };
    const float gameHeight{ mainWindow ? static_cast<float>(mainWindow->GetHeight()) : 1080.0f };

    const ImVec2 availSize{ ImGui::GetContentRegionAvail() };
    if (availSize.x <= 0.0f || availSize.y <= 0.0f)
    {
        ImGui::End();
        return;
    }

    if (m_sceneWidth != gameWidth || m_sceneHeight != gameHeight)
    {
        m_sceneWidth = gameWidth;
        m_sceneHeight = gameHeight;
        EnsureSceneRenderTarget(static_cast<UINT>(m_sceneWidth), static_cast<UINT>(m_sceneHeight));
    }

    const float targetAspect{ gameWidth / gameHeight };
    const float windowAspect{ availSize.x / availSize.y };

    ImVec2 renderSize{ availSize };
    ImVec2 cursorOffset{ 0.0f, 0.0f };

    if (windowAspect > targetAspect)
    {
        renderSize.x = availSize.y * targetAspect;
        cursorOffset.x = (availSize.x - renderSize.x) * 0.5f;
    }
    else
    {
        renderSize.y = availSize.x / targetAspect;
        cursorOffset.y = (availSize.y - renderSize.y) * 0.5f;
    }

    renderSize.x = (std::max)(1.0f, std::floor(renderSize.x));
    renderSize.y = (std::max)(1.0f, std::floor(renderSize.y));

    const ImVec2 screenCursorPos{ ImGui::GetCursorScreenPos() };
    const ImVec2 centeredScreenPos{ screenCursorPos.x + cursorOffset.x, screenCursorPos.y + cursorOffset.y };

    const ImVec2 originalCursorPos{ ImGui::GetCursorPos() };
    ImGui::SetCursorPos(ImVec2{ originalCursorPos.x + cursorOffset.x, originalCursorPos.y + cursorOffset.y });

    bool isImageHovered = false;
    if (m_sceneSRV)
    {
        ImGui::Image(reinterpret_cast<ImTextureID>(m_sceneSRV.Get()), renderSize);
        isImageHovered = ImGui::IsItemHovered();
    }

    const bool isPlayMode{ m_editorMode == EditorMode::Play };

    // Only allow W/E/R gizmo hotkeys if we are not playing the game
    if (!isPlayMode && (ImGui::IsWindowFocused() || ImGui::IsWindowHovered()) && !ImGui::IsMouseDown(ImGuiMouseButton_Right))
    {
        if (ImGui::IsKeyPressed(ImGuiKey_W)) m_gizmoOperation = ImGuizmo::TRANSLATE;
        if (ImGui::IsKeyPressed(ImGuiKey_E)) m_gizmoOperation = ImGuizmo::ROTATE;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) m_gizmoOperation = ImGuizmo::SCALE;
    }

    const bool hasSelection{ m_selectedObject != nullptr };
    const bool isNotRoot{ currentScene && (m_selectedObject != currentScene->GetRootGameObject()) };

    // Consume the latch. If true, we skip drawing ImGuizmo entirely this frame.
    if (s_skipGizmoThisFrame)
    {
        s_skipGizmoThisFrame = false;
    }
    else if (activeCamera && hasSelection && isNotRoot && !isPlayMode)
    {
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(centeredScreenPos.x, centeredScreenPos.y, renderSize.x, renderSize.y);

        DirectX::XMFLOAT4X4 view{ activeCamera->GetView() };
        DirectX::XMFLOAT4X4 proj{ activeCamera->GetProjection() };
        DirectX::XMFLOAT4X4 targetMatrix{ m_selectedObject->transform.GetWorldMatrix() };

        auto* staticCollider{ dynamic_cast<StaticMeshColliderComponent*>(m_selectedComponent) };

        if (staticCollider && m_selectedObject == staticCollider->GetOwner())
        {
            auto& config{ staticCollider->GetConfig() };
            const DirectX::XMMATRIX objWorld{ DirectX::XMLoadFloat4x4(&targetMatrix) };

            // Inject the proxyExtents so ImGuizmo knows the absolute size
            const DirectX::XMMATRIX locScale{ DirectX::XMMatrixScaling(config.proxyExtents.x, config.proxyExtents.y, config.proxyExtents.z) };
            const DirectX::XMMATRIX locRot{ DirectX::XMMatrixRotationRollPitchYaw(
                DirectX::XMConvertToRadians(config.localRotation.x),
                DirectX::XMConvertToRadians(config.localRotation.y),
                DirectX::XMConvertToRadians(config.localRotation.z))
            };
            const DirectX::XMMATRIX locTrans{ DirectX::XMMatrixTranslation(config.localOffset.x, config.localOffset.y, config.localOffset.z) };

            DirectX::XMStoreFloat4x4(&targetMatrix, locScale * locRot * locTrans * objWorld);
        }

        ImGuizmo::SetOrthographic(false);
        ImGuizmo::Manipulate(&view._11, &proj._11, m_gizmoOperation, m_gizmoMode, &targetMatrix._11);

        static bool s_wasGizmoUsing{ false };
        static DirectX::XMFLOAT3 s_gizmoPrePos{};
        static DirectX::XMFLOAT3 s_gizmoPreRot{};
        static DirectX::XMFLOAT3 s_gizmoPreScale{};
        static GameObject* s_gizmoTarget{ nullptr };

        if (!s_wasGizmoUsing && ImGuizmo::IsUsing())
        {
            s_wasGizmoUsing = true;
            s_gizmoTarget = m_selectedObject;
            s_gizmoPrePos = m_selectedObject->transform.position;
            s_gizmoPreRot = m_selectedObject->transform.rotation;
            s_gizmoPreScale = m_selectedObject->transform.scale;
        }

        if (ImGuizmo::IsUsing())
        {
            DirectX::XMMATRIX matNewWorld{ DirectX::XMLoadFloat4x4(&targetMatrix) };

            if (staticCollider && m_selectedObject == staticCollider->GetOwner())
            {
                auto& config{ staticCollider->GetConfig() };

                DirectX::XMFLOAT4X4 objFloat4x4{ m_selectedObject->transform.GetWorldMatrix() };
                DirectX::XMMATRIX objWorld{ DirectX::XMLoadFloat4x4(&objFloat4x4) };

                DirectX::XMVECTOR det;
                DirectX::XMMATRIX matLocalNew{ DirectX::XMMatrixMultiply(matNewWorld, DirectX::XMMatrixInverse(&det, objWorld)) };

                DirectX::XMVECTOR vScale, vRotQuat, vTrans;
                if (DirectX::XMMatrixDecompose(&vScale, &vRotQuat, &vTrans, matLocalNew))
                {
                    DirectX::XMStoreFloat3(&config.localOffset, vTrans);

                    // Assign absolute scale directly to prevent exponential blowup
                    DirectX::XMStoreFloat3(&config.proxyExtents, vScale);

                    // Clamp to prevent collapsing the physics engine
                    config.proxyExtents.x = (std::max)(0.001f, config.proxyExtents.x);
                    config.proxyExtents.y = (std::max)(0.001f, config.proxyExtents.y);
                    config.proxyExtents.z = (std::max)(0.001f, config.proxyExtents.z);

                    const DirectX::XMFLOAT4X4 mRot{ [&]() {
                        DirectX::XMFLOAT4X4 temp;
                        DirectX::XMStoreFloat4x4(&temp, DirectX::XMMatrixRotationQuaternion(vRotQuat));
                        return temp;
                    }() };

                    float pitch{ asinf(std::clamp(-mRot._32, -1.0f, 1.0f)) };
                    float yaw, roll;
                    if (cosf(pitch) > 0.0001f) {
                        yaw = atan2f(mRot._31, mRot._33);
                        roll = atan2f(mRot._12, mRot._22);
                    }
                    else {
                        yaw = atan2f(-mRot._13, mRot._11);
                        roll = 0.0f;
                    }

                    config.localRotation = {
                        DirectX::XMConvertToDegrees(pitch),
                        DirectX::XMConvertToDegrees(yaw),
                        DirectX::XMConvertToDegrees(roll)
                    };

                    staticCollider->MarkDirty();
                }
            }
            else
            {
                // Base GameObject Transform Math (Untouched)
                DirectX::XMMATRIX matLocal{ matNewWorld };
                if (m_selectedObject->transform.parent)
                {
                    DirectX::XMFLOAT4X4 parentWorld{ m_selectedObject->transform.parent->GetWorldMatrix() };
                    DirectX::XMMATRIX pWorld{ DirectX::XMLoadFloat4x4(&parentWorld) };
                    DirectX::XMVECTOR det;
                    matLocal = DirectX::XMMatrixMultiply(matNewWorld, DirectX::XMMatrixInverse(&det, pWorld));
                }

                DirectX::XMVECTOR vScale, vRotQuat, vTrans;
                if (DirectX::XMMatrixDecompose(&vScale, &vRotQuat, &vTrans, matLocal))
                {
                    DirectX::XMStoreFloat3(&m_selectedObject->transform.position, vTrans);
                    DirectX::XMStoreFloat3(&m_selectedObject->transform.scale, vScale);

                    const DirectX::XMFLOAT4X4 mRot{ [&]() {
                        DirectX::XMFLOAT4X4 temp;
                        DirectX::XMStoreFloat4x4(&temp, DirectX::XMMatrixRotationQuaternion(vRotQuat));
                        return temp;
                    }() };

                    float pitch{ asinf(std::clamp(-mRot._32, -1.0f, 1.0f)) };
                    float yaw, roll;
                    if (cosf(pitch) > 0.0001f) {
                        yaw = atan2f(mRot._31, mRot._33);
                        roll = atan2f(mRot._12, mRot._22);
                    }
                    else {
                        yaw = atan2f(-mRot._13, mRot._11);
                        roll = 0.0f;
                    }

                    m_selectedObject->transform.rotation = {
                        DirectX::XMConvertToDegrees(pitch),
                        DirectX::XMConvertToDegrees(yaw),
                        DirectX::XMConvertToDegrees(roll)
                    };
                }
            }
        }
        else if (s_wasGizmoUsing)
        {
            s_wasGizmoUsing = false;
            if (s_gizmoTarget && s_gizmoTarget == m_selectedObject)
            {
                const auto& curPos = m_selectedObject->transform.position;
                const auto& curRot = m_selectedObject->transform.rotation;
                const auto& curScale = m_selectedObject->transform.scale;

                if (curPos.x != s_gizmoPrePos.x || curPos.y != s_gizmoPrePos.y || curPos.z != s_gizmoPrePos.z ||
                    curRot.x != s_gizmoPreRot.x || curRot.y != s_gizmoPreRot.y || curRot.z != s_gizmoPreRot.z ||
                    curScale.x != s_gizmoPreScale.x || curScale.y != s_gizmoPreScale.y || curScale.z != s_gizmoPreScale.z)
                {
                    PushUndoAction(std::make_unique<TransformGizmoAction>(
                        m_selectedObject,
                        s_gizmoPrePos, s_gizmoPreRot, s_gizmoPreScale,
                        curPos, curRot, curScale));
                }
            }
            s_gizmoTarget = nullptr;
        }
    }

    const ImVec2 windowSize{ ImGui::GetWindowSize() };
    ImGui::SetCursorPos(ImVec2{ windowSize.x - buttonSize.x - 8.0f, windowSize.y - buttonSize.y - 8.0f });

    if (DrawToolbarIconButton("##MaximizeBtn", ToolbarIcon::Maximize, s_isSceneMaximized, ImVec4{ 0.4f, 0.4f, 0.4f, 1.0f }, buttonSize))
    {
        s_isSceneMaximized = !s_isSceneMaximized;
        if (s_isSceneMaximized)
        {
            // Cache the active dock node ID right before we detach
            s_previousDockId = ImGui::GetWindowDockID();
        }
        else
        {
            // Flag to snap back into the cached dock node next frame
            s_restoreDock = true;
        }
    }

    // Resolve overlapping UI states mathematically at the end of the scope.
    // Subtract the overlapping Maximize button hover state so it doesn't trigger the camera capture.
    bool isMaximizeHovered = ImGui::IsItemHovered();
    CameraController::Instance().SetViewportHovered(isImageHovered && !isMaximizeHovered);

    ImGui::End();
}

void EditorManager::DrawMenuBar(Scene* currentScene) noexcept
{
    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Save Scene"))
            {
                if (currentScene)
                {
                    // Undo any leftover timeline scrub so the preview offset is never written to disk
                    if (m_editorMode == EditorMode::Edit)
                    {
                        m_timelinePanel.EndPreview();
                    }

                    SceneSerializer::Save(
                        currentScene->GetSceneSavePath(),
                        currentScene->GetRootGameObject()
                    );
                    SaveUserPreferences(currentScene, CameraController::Instance().GetActiveCamera().get());
                }
                else
                {
                    Log::Error("Cannot save: No active scene loaded.");
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit"))
        {
            const std::string undoLabel = CanUndo()
                ? "Undo " + std::string(GetUndoActionName())
                : "Undo";
            const std::string redoLabel = CanRedo()
                ? "Redo " + std::string(GetRedoActionName())
                : "Redo";

            if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, CanUndo()))
            {
                Undo(currentScene);
            }
            if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y", false, CanRedo()))
            {
                Redo(currentScene);
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, m_selectedObject != nullptr))
            {
                DuplicateSelected(currentScene);
            }
            if (ImGui::MenuItem("Group Selected", "Ctrl+G", false, !m_selectedObjects.empty()))
            {
                GroupSelected(currentScene);
            }
            if (ImGui::MenuItem("Delete", "Del", false, !m_selectedObjects.empty() || m_selectedObject != nullptr))
            {
                DeleteSelected();
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Scene"))
        {
            if (ImGui::MenuItem("Title"))
            {
                ClearSelection();
                ClearUndoHistory();
                Framework::Instance()->ChangeScene([]() { return std::make_unique<SceneTitle>(); });
            }
            if (ImGui::MenuItem("Game"))
            {
                ClearSelection();
                ClearUndoHistory();
                Framework::Instance()->ChangeScene([]() { return std::make_unique<SceneGame>(); });
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Debug"))
        {
            ImGui::MenuItem("Profiler", nullptr, &m_showProfiler);
            ImGui::MenuItem("Animation Timeline", nullptr, &m_showAnimTimeline);
            ImGui::MenuItem("Animation Graph", nullptr, &m_showAnimGraph);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Graphics"))
        {
            ImGui::MenuItem("Post-Processing", nullptr, &m_showPostProcess);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Time")) { ImGui::EndMenu(); }
        if (ImGui::BeginMenu("Curve Manager")) { ImGui::EndMenu(); }

        ImGui::EndMenuBar();
    }
}

void EditorManager::Undo(Scene* currentScene) noexcept
{
    if (m_undoStack.empty() || !currentScene) return;

    // Abort any open inspector session so the undo itself is not recorded as a new edit
    m_isInspectorEditing = false;

    auto action{ std::move(m_undoStack.back()) };
    m_undoStack.pop_back();

    action->Undo(currentScene);
    m_redoStack.push_back(std::move(action));
}

void EditorManager::Redo(Scene* currentScene) noexcept
{
    if (m_redoStack.empty() || !currentScene) return;

    // Abort any open inspector session so the redo itself is not recorded as a new edit
    m_isInspectorEditing = false;

    auto action{ std::move(m_redoStack.back()) };
    m_redoStack.pop_back();

    action->Redo(currentScene);
    m_undoStack.push_back(std::move(action));
}

[[nodiscard]] bool EditorManager::CanUndo() const noexcept
{
    return !m_undoStack.empty();
}

[[nodiscard]] bool EditorManager::CanRedo() const noexcept
{
    return !m_redoStack.empty();
}

[[nodiscard]] std::string_view EditorManager::GetUndoActionName() const noexcept
{
    return m_undoStack.empty() ? "" : m_undoStack.back()->GetName();
}

[[nodiscard]] std::string_view EditorManager::GetRedoActionName() const noexcept
{
    return m_redoStack.empty() ? "" : m_redoStack.back()->GetName();
}

void EditorManager::PushUndoAction(std::unique_ptr<IUndoAction> action) noexcept
{
    if (!action) return;

    if (m_undoStack.size() >= s_maxUndoHistory)
    {
        m_undoStack.erase(m_undoStack.begin());
    }

    m_undoStack.push_back(std::move(action));
    m_redoStack.clear();
}

void EditorManager::ClearUndoHistory() noexcept
{
    m_undoStack.clear();
    m_redoStack.clear();
}

bool EditorManager::IsObjectSelected(const GameObject* node) const noexcept
{
    if (!node) return false;
    return std::find(m_selectedObjects.begin(), m_selectedObjects.end(), node) != m_selectedObjects.end();
}

void EditorManager::SetSelectedObject(GameObject* target, bool isToggle) noexcept
{
    ApplySelection(target, isToggle, false);
}

void EditorManager::ApplySelection(GameObject* target, bool isCtrl, bool isShift) noexcept
{
    if (!target) return;

    // Shift range selection: selects contiguous block between anchor and target
    if (isShift && m_selectionAnchor)
    {
        const auto anchorIt = std::find(m_linearHierarchyNodes.begin(), m_linearHierarchyNodes.end(), m_selectionAnchor);
        const auto targetIt = std::find(m_linearHierarchyNodes.begin(), m_linearHierarchyNodes.end(), target);

        if (anchorIt != m_linearHierarchyNodes.end() && targetIt != m_linearHierarchyNodes.end())
        {
            const auto first = (std::min)(anchorIt, targetIt);
            const auto last = (std::max)(anchorIt, targetIt);

            if (!isCtrl)
            {
                m_selectedObjects.clear();
            }

            for (auto it = first; it <= last; ++it)
            {
                if (!IsObjectSelected(*it))
                {
                    m_selectedObjects.push_back(*it);
                }
            }
            m_selectedObject = target;
            m_selectedComponent = nullptr;
            return;
        }
    }

    // Ctrl click: toggle selection
    if (isCtrl)
    {
        const auto it = std::find(m_selectedObjects.begin(), m_selectedObjects.end(), target);
        if (it != m_selectedObjects.end())
        {
            m_selectedObjects.erase(it);
            if (m_selectedObject == target)
            {
                m_selectedObject = m_selectedObjects.empty() ? nullptr : m_selectedObjects.back();
            }
        }
        else
        {
            m_selectedObjects.push_back(target);
            m_selectedObject = target;
        }
        m_selectionAnchor = target;
    }
    else
    {
        // Normal single click: replace selection
        m_selectedObjects.clear();
        m_selectedObjects.push_back(target);
        m_selectedObject = target;
        m_selectionAnchor = target;
    }

    m_selectedComponent = nullptr;
}

void EditorManager::DuplicateGameObject(GameObject* target, Scene* currentScene) noexcept
{
    if (!target || !currentScene || target == currentScene->GetRootGameObject()) return;

    auto clone{ target->Clone() };
    GameObject* clonePtr{ clone.get() };

    GameObject* parent{ target->GetParent() ? target->GetParent() : currentScene->GetRootGameObject() };
    parent->AddChild(std::move(clone));

    m_selectedObjects.clear();
    m_selectedObjects.push_back(clonePtr);
    m_selectedObject = clonePtr;
    m_selectionAnchor = clonePtr;
    m_selectedComponent = nullptr;
}

void EditorManager::DuplicateSelected(Scene* currentScene) noexcept
{
    if (!currentScene) return;

    if (m_selectedObjects.empty())
    {
        if (m_selectedObject && m_selectedObject != currentScene->GetRootGameObject())
        {
            DuplicateGameObject(m_selectedObject, currentScene);
        }
        return;
    }

    // Clone selected objects into temporary array to prevent iteration invalidation
    const std::vector<GameObject*> targetsToDuplicate{ m_selectedObjects };
    m_selectedObjects.clear();

    for (auto* target : targetsToDuplicate)
    {
        if (!target || target == currentScene->GetRootGameObject() || target->IsDestroyed()) continue;

        auto clone{ target->Clone() };
        GameObject* clonePtr{ clone.get() };

        GameObject* parent{ target->GetParent() ? target->GetParent() : currentScene->GetRootGameObject() };
        parent->AddChild(std::move(clone));

        m_selectedObjects.push_back(clonePtr);
    }

    if (!m_selectedObjects.empty())
    {
        for (auto* cloneObj : m_selectedObjects)
        {
            if (cloneObj && cloneObj->GetParent())
            {
                PushUndoAction(std::make_unique<CreateObjectAction>(cloneObj->GetParent(), cloneObj));
            }
        }
        m_selectedObject = m_selectedObjects.back();
        m_selectionAnchor = m_selectedObjects.front();
    }
    else
    {
        m_selectedObject = nullptr;
        m_selectionAnchor = nullptr;
    }
    m_selectedComponent = nullptr;
}

void EditorManager::GroupSelected(Scene* currentScene) noexcept
{
    if (!currentScene || !currentScene->GetRootGameObject()) return;

    // Filter valid nodes from selection (exclude null, root, or destroyed)
    std::vector<GameObject*> validObjects{};
    validObjects.reserve(m_selectedObjects.size());
    for (auto* obj : m_selectedObjects)
    {
        if (obj && obj != currentScene->GetRootGameObject() && !obj->IsDestroyed())
        {
            validObjects.push_back(obj);
        }
    }

    if (validObjects.empty())
    {
        if (m_selectedObject && m_selectedObject != currentScene->GetRootGameObject() && !m_selectedObject->IsDestroyed())
        {
            validObjects.push_back(m_selectedObject);
        }
        else
        {
            return;
        }
    }

    // Isolate top-level nodes within selection to prevent pulling children out of selected parents
    std::vector<GameObject*> topLevelSelection{};
    topLevelSelection.reserve(validObjects.size());
    for (auto* obj : validObjects)
    {
        bool hasSelectedAncestor{ false };
        for (const auto* curr{ obj->GetParent() }; curr != nullptr; curr = curr->GetParent())
        {
            if (std::find(validObjects.begin(), validObjects.end(), curr) != validObjects.end())
            {
                hasSelectedAncestor = true;
                break;
            }
        }
        if (!hasSelectedAncestor)
        {
            topLevelSelection.push_back(obj);
        }
    }

    if (topLevelSelection.empty()) return;

    // Compute the arithmetic mean of world positions for the new group pivot
    DirectX::XMFLOAT3 centerPos{ 0.0f, 0.0f, 0.0f };
    for (const auto* obj : topLevelSelection)
    {
        const DirectX::XMFLOAT4X4 wMat{ obj->transform.GetWorldMatrix() };
        centerPos.x += wMat._41;
        centerPos.y += wMat._42;
        centerPos.z += wMat._43;
    }
    const float invCount{ 1.0f / static_cast<float>(topLevelSelection.size()) };
    centerPos.x *= invCount;
    centerPos.y *= invCount;
    centerPos.z *= invCount;

    // If all top-level objects share the same parent, place group under it; otherwise scene root
    GameObject* commonParent{ topLevelSelection.front()->GetParent() };
    for (std::size_t i{ 1 }; i < topLevelSelection.size(); ++i)
    {
        if (topLevelSelection[i]->GetParent() != commonParent)
        {
            commonParent = currentScene->GetRootGameObject();
            break;
        }
    }
    if (!commonParent)
    {
        commonParent = currentScene->GetRootGameObject();
    }

    // Determine clean incremental name (Maya/Blender convention: Group_01, Group_02, ...)
    int maxIndex{ 0 };
    for (const auto& sibling : commonParent->GetChildren())
    {
        if (!sibling) continue;
        const std::string& sibName{ sibling->GetName() };
        if (sibName.rfind("Group_", 0) == 0 && sibName.length() > 6)
        {
            char* endPtr{ nullptr };
            const long val{ std::strtol(sibName.c_str() + 6, &endPtr, 10) };
            if (endPtr != sibName.c_str() + 6 && *endPtr == '\0')
            {
                if (val > maxIndex) maxIndex = static_cast<int>(val);
            }
        }
    }

    char groupName[64];
    snprintf(groupName, sizeof(groupName), "Group_%02d", maxIndex + 1);

    auto groupObj{ std::make_unique<GameObject>(groupName) };
    auto* groupPtr{ groupObj.get() };

    // Convert world center position to common parent's local space
    if (commonParent != currentScene->GetRootGameObject())
    {
        const DirectX::XMFLOAT4X4 parentWorldF{ commonParent->transform.GetWorldMatrix() };
        const DirectX::XMMATRIX pWorld{ DirectX::XMLoadFloat4x4(&parentWorldF) };
        DirectX::XMVECTOR det;
        const DirectX::XMMATRIX invPWorld{ DirectX::XMMatrixInverse(&det, pWorld) };
        const DirectX::XMVECTOR vWorldPos{ DirectX::XMLoadFloat3(&centerPos) };
        const DirectX::XMVECTOR vLocalPos{ DirectX::XMVector3TransformCoord(vWorldPos, invPWorld) };
        DirectX::XMStoreFloat3(&groupObj->transform.position, vLocalPos);
    }
    else
    {
        groupObj->transform.position = centerPos;
    }

    commonParent->AddChild(std::move(groupObj));

    // Reparent each selected top-level object under the new group
    for (auto* obj : topLevelSelection)
    {
        if (obj != groupPtr && obj->GetParent() != groupPtr)
        {
            obj->SetParent(groupPtr);
        }
    }

    // Focus selection onto the new group
    m_selectedObjects.clear();
    m_selectedObjects.push_back(groupPtr);
    m_selectedObject = groupPtr;
    m_selectionAnchor = groupPtr;
    m_selectedComponent = nullptr;
}

void EditorManager::ReparentNodes(const std::vector<GameObject*>& nodes, GameObject* newParent) noexcept
{
    if (!newParent) return;

    // Gather valid candidates (non-null, not destroyed, not already parented, not self)
    std::vector<GameObject*> validNodes{};
    validNodes.reserve(nodes.size());
    for (auto* node : nodes)
    {
        if (!node || node->IsDestroyed() || node == newParent || node->GetParent() == newParent)
        {
            continue;
        }

        // Prevent cyclic parenting: newParent cannot be a descendant of node
        bool isAncestorOfTarget{ false };
        for (const auto* curr{ newParent }; curr != nullptr; curr = curr->GetParent())
        {
            if (curr == node)
            {
                isAncestorOfTarget = true;
                break;
            }
        }
        if (!isAncestorOfTarget)
        {
            validNodes.push_back(node);
        }
    }

    // Isolate top-level nodes in selection (if parent and child are both selected, only reparent parent)
    std::vector<GameObject*> topLevelNodes{};
    topLevelNodes.reserve(validNodes.size());
    for (auto* node : validNodes)
    {
        bool hasSelectedAncestor{ false };
        for (const auto* curr{ node->GetParent() }; curr != nullptr; curr = curr->GetParent())
        {
            if (std::find(validNodes.begin(), validNodes.end(), curr) != validNodes.end())
            {
                hasSelectedAncestor = true;
                break;
            }
        }
        if (!hasSelectedAncestor)
        {
            topLevelNodes.push_back(node);
        }
    }

    std::vector<ReparentRecord> records{};
    records.reserve(topLevelNodes.size());

    for (auto* node : topLevelNodes)
    {
        ReparentRecord rec{};
        rec.node = node;
        rec.oldParent = node->GetParent();
        rec.oldPos = node->transform.position;
        rec.oldRot = node->transform.rotation;
        rec.oldScale = node->transform.scale;
        rec.newParent = newParent;
        records.push_back(rec);
    }

    // Apply reparenting with coordinate preservation
    for (std::size_t i{ 0 }; i < topLevelNodes.size(); ++i)
    {
        topLevelNodes[i]->SetParent(newParent);
        records[i].newPos = topLevelNodes[i]->transform.position;
        records[i].newRot = topLevelNodes[i]->transform.rotation;
        records[i].newScale = topLevelNodes[i]->transform.scale;
    }

    if (!records.empty())
    {
        PushUndoAction(std::make_unique<ReparentAction>(std::move(records)));
    }
}

void EditorManager::ParentSelectedToActive() noexcept
{
    if (!m_selectedObject || m_selectedObjects.size() < 2) return;

    ReparentNodes(m_selectedObjects, m_selectedObject);
}

void EditorManager::UnparentSelected(Scene* currentScene) noexcept
{
    if (!currentScene || !currentScene->GetRootGameObject()) return;

    GameObject* root{ currentScene->GetRootGameObject() };
    if (m_selectedObjects.empty())
    {
        if (m_selectedObject && m_selectedObject->GetParent() != root)
        {
            m_selectedObject->SetParent(root);
        }
        return;
    }

    ReparentNodes(m_selectedObjects, root);
}

void EditorManager::ToggleActiveSelected() noexcept
{
    if (m_selectedObjects.empty())
    {
        if (m_selectedObject)
        {
            m_selectedObject->SetActive(!m_selectedObject->IsActive());
        }
        return;
    }

    const bool newState{ m_selectedObject ? !m_selectedObject->IsActive() : true };
    for (auto* obj : m_selectedObjects)
    {
        if (obj)
        {
            obj->SetActive(newState);
        }
    }
}

void EditorManager::DeleteSelected() noexcept
{
    std::vector<GameObject*> targets{};
    if (m_selectedObjects.empty())
    {
        if (m_selectedObject) targets.push_back(m_selectedObject);
    }
    else
    {
        targets = m_selectedObjects;
    }

    if (targets.empty()) return;

    std::vector<DeletedRecord> records{};
    records.reserve(targets.size());

    for (auto* obj : targets)
    {
        if (!obj || obj->IsDestroyed() || !obj->GetParent()) continue;

        GameObject* parent = obj->GetParent();
        auto extracted = parent->RemoveChild(obj);
        if (extracted)
        {
            DeletedRecord rec{};
            rec.parent = parent;
            rec.rawNode = obj;
            rec.storage = std::move(extracted);
            records.push_back(std::move(rec));
        }
    }

    ClearSelection();

    if (!records.empty())
    {
        PushUndoAction(std::make_unique<DeleteObjectsAction>(std::move(records)));
    }
}

void EditorManager::DrawHierarchyNode(GameObject* node, Scene* currentScene) noexcept
{
    if (!node) return;

    // Record visible display order for contiguous Shift-click calculations
    m_linearHierarchyNodes.push_back(node);

    ImGuiTreeNodeFlags flags{ ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth };

    if (IsObjectSelected(node))
    {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (node->GetChildren().empty())
    {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }

    const bool isOpen{ ImGui::TreeNodeEx(static_cast<void*>(node), flags, "%s", node->GetName().c_str()) };

    // Hierarchy Drag Source (Multi-item support)
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
    {
        m_deferredDeselectNode = nullptr; // Drag started; preserve multi-selection!
        ImGui::SetDragDropPayload("HIERARCHY_GAMEOBJECT", &node, sizeof(GameObject*));
        if (m_selectedObjects.size() > 1 && IsObjectSelected(node))
        {
            ImGui::Text("Move %zu Objects", m_selectedObjects.size());
        }
        else
        {
            ImGui::Text("Move %s", node->GetName().c_str());
        }
        ImGui::EndDragDropSource();
    }

    // Hierarchy Drop Target (Reparents all selected objects if part of group)
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_GAMEOBJECT"))
        {
            GameObject* droppedNode{ *static_cast<GameObject**>(payload->Data) };
            if (droppedNode && droppedNode != node)
            {
                if (IsObjectSelected(droppedNode) && m_selectedObjects.size() > 1)
                {
                    ReparentNodes(m_selectedObjects, node);
                }
                else
                {
                    droppedNode->SetParent(node);
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Selection Input Capture: Distinguishes between multi-drag initiation vs. single selection
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
    {
        const ImGuiIO& io{ ImGui::GetIO() };
        if (io.KeyCtrl || io.KeyShift)
        {
            m_pendingClickNode = node;
            m_pendingCtrl = io.KeyCtrl;
            m_pendingShift = io.KeyShift;
            m_deferredDeselectNode = nullptr;
        }
        else if (!IsObjectSelected(node) || m_selectedObjects.size() <= 1)
        {
            m_pendingClickNode = node;
            m_pendingCtrl = false;
            m_pendingShift = false;
            m_deferredDeselectNode = nullptr;
        }
        else
        {
            // Node is already selected in a multi-selection.
            // Defer single-selection until mouse release so drag-and-drop can move all selected nodes!
            m_deferredDeselectNode = node;
            m_pendingClickNode = nullptr;
        }
    }

    if (ImGui::BeginPopupContextItem())
    {
        // If right-clicking an unselected object, switch selection to it
        if (!IsObjectSelected(node))
        {
            m_selectedObjects.clear();
            m_selectedObjects.push_back(node);
            m_selectedObject = node;
            m_selectionAnchor = node;
            m_selectedComponent = nullptr;
        }

        if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
        {
            DuplicateSelected(currentScene);
        }
        if (ImGui::MenuItem("Group Selected", "Ctrl+G"))
        {
            GroupSelected(currentScene);
        }
        if (ImGui::MenuItem("Parent to Last Selected", "P", false, m_selectedObjects.size() >= 2 && m_selectedObject != nullptr))
        {
            ParentSelectedToActive();
        }
        if (ImGui::MenuItem("Unparent to Root", "Shift+P", false, node->GetParent() != currentScene->GetRootGameObject() || m_selectedObjects.size() > 1))
        {
            UnparentSelected(currentScene);
        }
        if (ImGui::MenuItem("Toggle Active", "H"))
        {
            ToggleActiveSelected();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Create Empty Child"))
        {
            auto child{ std::make_unique<GameObject>("Empty_Child") };
            auto* rawPtr{ child.get() };
            node->AddChild(std::move(child));
            m_selectedObjects.clear();
            m_selectedObjects.push_back(rawPtr);
            m_selectedObject = rawPtr;
            m_selectionAnchor = rawPtr;
            m_selectedComponent = nullptr;
        }
        if (ImGui::MenuItem("Create Socket Anchor"))
        {
            node->AddChild(std::make_unique<GameObject>("Socket_Anchor"));
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Delete", "Del"))
        {
            DeleteSelected();
        }
        ImGui::EndPopup();
    }

    if (isOpen)
    {
        for (const auto& child : node->GetChildren())
        {
            DrawHierarchyNode(child.get(), currentScene);
        }
        ImGui::TreePop();
    }
}

void EditorManager::DrawHierarchy(Scene* currentScene) noexcept
{
    // Sanitize destroyed entities from multi-selection list
    m_selectedObjects.erase(
        std::remove_if(m_selectedObjects.begin(), m_selectedObjects.end(),
            [](const GameObject* obj) noexcept {
                return !obj || obj->IsDestroyed();
            }),
        m_selectedObjects.end()
    );

    if (m_selectedObject && m_selectedObject->IsDestroyed())
    {
        m_selectedObject = m_selectedObjects.empty() ? nullptr : m_selectedObjects.back();
    }
    if (m_selectionAnchor && m_selectionAnchor->IsDestroyed())
    {
        m_selectionAnchor = nullptr;
    }
    if (m_deferredDeselectNode && m_deferredDeselectNode->IsDestroyed())
    {
        m_deferredDeselectNode = nullptr;
    }

    // Reset frame-order collection and click-event latches
    m_linearHierarchyNodes.clear();
    m_pendingClickNode = nullptr;

    ImGui::Begin(s_windowHierarchy);

    if (currentScene && currentScene->GetRootGameObject())
    {
        if (ImGui::Button("+ Create"))
        {
            ImGui::OpenPopup("CreateMenuPopup");
        }

        if (ImGui::BeginPopup("CreateMenuPopup"))
        {
            if (ImGui::MenuItem("Create Empty"))
            {
                auto emptyObj = std::make_unique<GameObject>("Empty");
                auto* rawPtr = emptyObj.get();
                auto* rootObj = currentScene->GetRootGameObject();
                rootObj->AddChild(std::move(emptyObj));
                PushUndoAction(std::make_unique<CreateObjectAction>(rootObj, rawPtr));
                m_selectedObjects.clear();
                m_selectedObjects.push_back(rawPtr);
                m_selectedObject = rawPtr;
                m_selectionAnchor = rawPtr;
                m_selectedComponent = nullptr;
            }

            if (auto* gameScene{ dynamic_cast<SceneGame*>(currentScene) })
            {
                ImGui::Separator();
                ImGui::TextDisabled("Rendering");

                if (ImGui::BeginMenu("Camera"))
                {
                    if (ImGui::MenuItem("Main Camera"))
                    {
                        auto camObj = std::make_unique<GameObject>("Main Camera");
                        camObj->AddComponent<CameraComponent>();
                        currentScene->GetRootGameObject()->AddChild(std::move(camObj));
                    }
                    if (ImGui::MenuItem("Virtual Camera"))
                    {
                        auto vcamObj = std::make_unique<GameObject>("Virtual Camera");
                        vcamObj->AddComponent<VirtualCameraComponent>();
                        currentScene->GetRootGameObject()->AddChild(std::move(vcamObj));
                    }
                    ImGui::EndMenu();
                }

                ImGui::Separator();
                ImGui::TextDisabled("Lighting");

                if (ImGui::BeginMenu("Light"))
                {
                    if (ImGui::MenuItem("Directional Light"))
                    {
                        auto lightObj = std::make_unique<GameObject>("Directional Light");
                        lightObj->transform.rotation = { 45.0f, -45.0f, 0.0f };
                        lightObj->AddComponent<DirectionalLightComponent>();
                        currentScene->GetRootGameObject()->AddChild(std::move(lightObj));
                    }
                    if (ImGui::MenuItem("Point Light"))
                    {
                        auto lightObj = std::make_unique<GameObject>("Point Light");
                        lightObj->transform.position = { 0.0f, 3.0f, 0.0f };
                        lightObj->AddComponent<PointLightComponent>();
                        currentScene->GetRootGameObject()->AddChild(std::move(lightObj));
                    }
                    if (ImGui::MenuItem("Spot Light"))
                    {
                        auto lightObj = std::make_unique<GameObject>("Spot Light");
                        lightObj->transform.position = { 0.0f, 5.0f, 0.0f };
                        lightObj->transform.rotation = { 90.0f, 0.0f, 0.0f };
                        lightObj->AddComponent<SpotLightComponent>();
                        currentScene->GetRootGameObject()->AddChild(std::move(lightObj));
                    }
                    ImGui::EndMenu();
                }
            }

            ImGui::EndPopup();
        }

        // Outliner Keyboard Shortcuts (Maya/Unity DCC conventions)
        const ImGuiIO& io{ ImGui::GetIO() };
        const bool canHandleShortcuts{ !io.WantTextInput && m_editorMode != EditorMode::Play };
        if (canHandleShortcuts)
        {
            if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false))
            {
                Undo(currentScene);
            }
            else if ((io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) ||
                (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)))
            {
                Redo(currentScene);
            }
            else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false))
            {
                DuplicateSelected(currentScene);
            }
            else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_G, false))
            {
                GroupSelected(currentScene);
            }
            else if (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_P, false))
            {
                UnparentSelected(currentScene);
            }
            else if (!io.KeyCtrl && !io.KeyShift && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_P, false))
            {
                ParentSelectedToActive();
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
            {
                DeleteSelected();
            }
            else if (!io.KeyCtrl && !io.KeyShift && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_H, false))
            {
                ToggleActiveSelected();
            }
        }

        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4{ 0.15f, 0.15f, 0.15f, 1.0f });
        const bool isSceneNodeOpen{ ImGui::CollapsingHeader(currentScene->GetSceneName().data(), ImGuiTreeNodeFlags_DefaultOpen) };
        ImGui::PopStyleColor();

        // Drag-and-drop onto Scene Header unparents all active dragged objects to Scene Root
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_GAMEOBJECT"))
            {
                GameObject* droppedNode{ *static_cast<GameObject**>(payload->Data) };
                if (droppedNode)
                {
                    GameObject* root{ currentScene->GetRootGameObject() };
                    if (IsObjectSelected(droppedNode) && m_selectedObjects.size() > 1)
                    {
                        ReparentNodes(m_selectedObjects, root);
                    }
                    else if (droppedNode->GetParent() != root)
                    {
                        droppedNode->SetParent(root);
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (isSceneNodeOpen)
        {
            for (const auto& child : currentScene->GetRootGameObject()->GetChildren())
            {
                DrawHierarchyNode(child.get(), currentScene);
            }
        }

        // Evaluate deferred click events after all visible nodes have been registered
        if (m_pendingClickNode)
        {
            ApplySelection(m_pendingClickNode, m_pendingCtrl, m_pendingShift);
            m_pendingClickNode = nullptr;
        }

        // Deferred deselect for multi-selection when clicking without dragging
        if (m_deferredDeselectNode && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            if (!ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f))
            {
                ApplySelection(m_deferredDeselectNode, false, false);
            }
            m_deferredDeselectNode = nullptr;
        }

        // Blank space context menu for scene root actions
        if (ImGui::BeginPopupContextWindow("HierarchyBlankContextMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            if (ImGui::MenuItem("Create Empty"))
            {
                auto emptyObj{ std::make_unique<GameObject>("Empty") };
                auto* rawPtr{ emptyObj.get() };
                auto* rootObj = currentScene->GetRootGameObject();
                rootObj->AddChild(std::move(emptyObj));
                PushUndoAction(std::make_unique<CreateObjectAction>(rootObj, rawPtr));
                m_selectedObjects.clear();
                m_selectedObjects.push_back(rawPtr);
                m_selectedObject = rawPtr;
                m_selectionAnchor = rawPtr;
                m_selectedComponent = nullptr;
            }
            if (!m_selectedObjects.empty())
            {
                ImGui::Separator();
                if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
                {
                    DuplicateSelected(currentScene);
                }
                if (ImGui::MenuItem("Group Selected", "Ctrl+G"))
                {
                    GroupSelected(currentScene);
                }
                if (ImGui::MenuItem("Unparent to Root", "Shift+P"))
                {
                    UnparentSelected(currentScene);
                }
                if (ImGui::MenuItem("Toggle Active", "H"))
                {
                    ToggleActiveSelected();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Delete", "Del"))
                {
                    DeleteSelected();
                }
            }
            ImGui::EndPopup();
        }

        // Blank space drop target: Unparents any dropped node back to the Scene Root
        const ImVec2 availSize{ ImGui::GetContentRegionAvail() };
        if (availSize.y > 0.0f)
        {
            ImGui::InvisibleButton("##HierarchyBlankDropZone", ImVec2{ availSize.x, availSize.y });
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            {
                ClearSelection();
            }
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_GAMEOBJECT"))
                {
                    GameObject* droppedNode{ *static_cast<GameObject**>(payload->Data) };
                    if (droppedNode)
                    {
                        GameObject* root{ currentScene->GetRootGameObject() };
                        if (IsObjectSelected(droppedNode) && m_selectedObjects.size() > 1)
                        {
                            ReparentNodes(m_selectedObjects, root);
                        }
                        else if (droppedNode->GetParent() != root)
                        {
                            droppedNode->SetParent(root);
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }
    }
    else
    {
        ImGui::TextDisabled("No Scene Loaded");
    }

    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
    {
        ClearSelection();
    }

    ImGui::End();
}

void EditorManager::DrawInspector(Scene* currentScene) noexcept
{
    ImGui::Begin(s_windowInspector);

    if (m_selectedObject)
    {
        if (m_selectedObjects.size() > 1)
        {
            ImGui::TextDisabled("MULTI-SELECTION (%zu OBJECTS)", m_selectedObjects.size());
            ImGui::Separator();
        }

        // Interaction-gated memento: serialize only when an edit begins or ends, never per frame
        const ImGuiContext& imguiCtx{ *GImGui };
        const ImGuiWindow* inspectorWindow{ ImGui::GetCurrentWindow() };

        // A selection switch invalidates any session that was recorded for the previous object
        if (m_inspectorEditingObject != m_selectedObject)
        {
            m_isInspectorEditing = false;
            m_inspectorEditingObject = m_selectedObject;
            m_inspectorSnapshotBefore = nullptr;
        }

        // Open a session on the press frame (before widgets consume the click) or when a widget is already active
        const bool pressedOnInspector{
            ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)
            && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) };
        const bool widgetAlreadyActive{
            imguiCtx.ActiveId != 0 && imguiCtx.ActiveIdWindow && inspectorWindow
            && imguiCtx.ActiveIdWindow->RootWindow == inspectorWindow->RootWindow };

        if (!m_isInspectorEditing && (pressedOnInspector || widgetAlreadyActive))
        {
            m_inspectorSnapshotBefore = SnapshotGameObject(m_selectedObject);
            m_isInspectorEditing = true;
        }

        m_selectedObject->DrawInspector();

        // The session ends once no widget is active, no popup is open and the mouse is released
        const bool interactionFinished{
            imguiCtx.ActiveId == 0 && imguiCtx.OpenPopupStack.Size == 0
            && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right) };

        if (m_isInspectorEditing && interactionFinished)
        {
            m_isInspectorEditing = false;

            // Copy-init so nlohmann does not wrap the snapshot in a one-element array
            nlohmann::json afterState = SnapshotGameObject(m_selectedObject);
            if (afterState != m_inspectorSnapshotBefore)
            {
                PushUndoAction(std::make_unique<InspectorEditAction>(
                    m_selectedObject,
                    std::move(m_inspectorSnapshotBefore),
                    std::move(afterState)));
            }
            m_inspectorSnapshotBefore = nullptr;
        }
        else if (m_isInspectorEditing)
        {
            const ImGuiContext& g{ *GImGui };
            const ImGuiWindow* curWindow{ ImGui::GetCurrentWindow() };
            const bool isActivelyDragging{ g.ActiveId != 0 && g.ActiveIdWindow == curWindow && ImGui::IsMouseDown(ImGuiMouseButton_Left) };
            if (!isActivelyDragging)
            {
                m_isInspectorEditing = false;
            }
        }
    }
    else if (currentScene)
    {
        ImGui::TextDisabled("SCENE PROPERTIES");
        ImGui::Separator();

        Graphics::Instance().GetLightManager().DrawEnvironmentGUI();

        ImGui::Spacing();
        if (ImGui::Button("Open Post-Processing & Color Grading..."))
        {
            m_showPostProcess = true;
            ImGui::SetWindowFocus(s_windowPostProcess);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Active Scene: %s", currentScene->GetSceneName().data());
        ImGui::TextDisabled("Save Path: %s", currentScene->GetSceneSavePath().data());
        ImGui::TextDisabled("PostProcess: %s", currentScene->GetPostProcessProfilePath().data());

        ImGui::Spacing();
        ImGui::Separator();

        currentScene->DrawGUI();
    }

    ImGui::End();
}

void EditorManager::DrawConsole() const noexcept
{
    if (ImGui::Begin(s_windowConsole))
    {
        if (ImGui::Button("Clear"))
        {
            Logger::Instance().Clear();
        }
        ImGui::Separator();

        ImGui::BeginChild("ConsoleScrollRegion", ImVec2{ 0, 0 }, false, ImGuiWindowFlags_HorizontalScrollbar);

        for (const auto& entry : Logger::Instance().GetEntries())
        {
            ImVec4 color{ 1.0f, 1.0f, 1.0f, 1.0f };

            switch (entry.level)
            {
            case LogLevel::Success: color = ImVec4{ 0.2f, 0.9f, 0.2f, 1.0f }; break;
            case LogLevel::Warning: color = ImVec4{ 1.0f, 0.8f, 0.0f, 1.0f }; break;
            case LogLevel::Error:   color = ImVec4{ 1.0f, 0.2f, 0.2f, 1.0f }; break;
            default: break;
            }

            ImGui::TextDisabled("[%s]", entry.timestamp.c_str());
            ImGui::SameLine();
            ImGui::TextColored(color, "%s", entry.message.c_str());
        }

        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        {
            ImGui::SetScrollHereY(1.0f);
        }

        ImGui::EndChild();
    }
    ImGui::End();
}

void EditorManager::DrawProfiler() const noexcept
{
    if (!m_showProfiler) return;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0.0f, 0.0f });

    if (ImGui::Begin(s_windowProfiler, const_cast<bool*>(&m_showProfiler)))
    {
        ImGui::PopStyleVar();

        const auto& cpuData{ ProfilerManager::Instance().GetCpuData() };
        const auto& metrics{ ProfilerManager::Instance().GetMetrics() };
        const std::size_t currentIndex{ ProfilerManager::Instance().GetCurrentFrameIndex() };

        const int maxFrames{ static_cast<int>(MAX_PROFILE_FRAMES) };
        const int offset{ static_cast<int>(currentIndex) };

        if (ImPlot::BeginPlot("##FrameTime", ImVec2{ -1.0f, -90.0f }))
        {
            ImPlot::SetupAxes(nullptr, "Frame Time (ms)", ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_AutoFit);
            ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, static_cast<double>(maxFrames), ImPlotCond_Always);

            static double targetFrameTimeMs{ 16.666 };
            ImPlot::DragLineY(0, &targetFrameTimeMs, ImVec4{ 0.9f, 0.1f, 0.1f, 0.8f }, 1.0f, ImPlotDragToolFlags_NoInputs);

            const auto& frameTimeHistory{ ProfilerManager::Instance().GetFrameTimeHistory() };

            ImPlotSpec spec{};
            spec.Offset = offset;
            spec.LineWeight = 1.5f;

            ImPlot::PlotLine("Frame Time", frameTimeHistory.data(), maxFrames, 1.0, 0.0, spec);
            ImPlot::EndPlot();
        }

        ImGui::Separator();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);

        if (ImGui::BeginTable("MetricsDashboard", 4, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("PERFORMANCE");
            ImGui::Text("FPS: %.1f", metrics.fps);
            ImGui::Text("Frame: %.2f ms", (1000.0f / (std::max)(metrics.fps, 1.0f)));

            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("MEMORY");
            ImGui::Text("Sys RAM: %.1f MB", metrics.ramUsageMB);
            ImGui::Text("GPU VRAM: %.1f MB", metrics.vramUsageMB);

            ImGui::TableSetColumnIndex(2);
            ImGui::TextDisabled("CPU TIMINGS");

            for (const auto& [name, data] : cpuData)
            {
                if (name == std::string_view{ "Draw Calls (3D)" }) continue;
                ImGui::Text("%s: %.2f ms", name, data.lastFrameTime);
            }

            ImGui::TableSetColumnIndex(3);
            ImGui::TextDisabled("DRAW CALLS (3D)");
            ImGui::Text("Calls: %zu", ProfilerManager::Instance().GetLastFrameDrawCallCount());
            ImGui::Text("Triangles: %zu", ProfilerManager::Instance().GetLastFrameTriangleCount());

            ImGui::EndTable();
        }
    }
    else
    {
        ImGui::PopStyleVar();
    }
    ImGui::End();
}

void EditorManager::DrawPostProcess(Scene* currentScene) noexcept
{
    if (!m_showPostProcess) return;

    if (ImGui::Begin(s_windowPostProcess, &m_showPostProcess))
    {
        if (currentScene && currentScene->GetPostProcessManager())
        {
            auto* ppm = currentScene->GetPostProcessManager();

            const std::string_view configPath{ currentScene->GetPostProcessProfilePath() };

            if (ImGui::Button("Save"))
            {
                ppm->SaveConfig(configPath);
            }
            ImGui::SameLine();
            if (ImGui::Button("Reload Saved"))
            {
                ppm->LoadConfig(configPath);
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset Defaults"))
            {
                ppm->ResetToDefaults();
            }

            ImGui::Separator();

            bool masterEnabled = ppm->IsEnabled();
            if (ImGui::Checkbox("Master Post-Process Enabled", &masterEnabled))
            {
                ppm->SetEnabled(masterEnabled);
            }
            ImGui::Separator();

            ImGui::BeginDisabled(!masterEnabled);

            if (ImGui::CollapsingHeader("Tonemapping & Color Grading", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ppm->GetColorGrading().DrawGUI();
            }
            ImGui::Spacing();

            if (ImGui::CollapsingHeader("Temporal AA"))
            {
                ppm->GetTemporalAA().DrawGUI();
            }
            ImGui::Spacing();

            for (const auto& effect : ppm->GetEffects())
            {
                ImGui::PushID(effect.get());
                if (ImGui::CollapsingHeader(effect->GetName().data()))
                {
                    effect->DrawGUI();
                }
                ImGui::PopID();
                ImGui::Spacing();
            }
            ImGui::EndDisabled();
        }
        else
        {
            ImGui::TextDisabled("No Post-Processing active in the current scene.");
        }
    }
    ImGui::End();
}

void EditorManager::OpenAnimationGraph(AnimationComponent* target) noexcept
{
    m_animGraphPanel.SetTarget(target);
    m_showAnimGraph = true;
    ImGui::SetWindowFocus("Animation Graph");
}

void EditorManager::OpenAnimationTimeline(AnimationComponent* target, std::size_t stateIndex) noexcept
{
    m_timelinePanel.SetTarget(target, stateIndex);
    m_showAnimTimeline = true;
    ImGui::SetWindowFocus("AnimEvent Timeline");
}

void EditorManager::SaveUserPreferences(Scene* currentScene, Camera* activeCamera) const noexcept
{
    if (!currentScene || !activeCamera) return;

    nlohmann::json root{};

    if (std::filesystem::exists(s_editorPrefsPath))
    {
        std::ifstream inFile{ std::string{ s_editorPrefsPath } };
        if (inFile.is_open())
        {
            try { inFile >> root; }
            catch (...) { root = nlohmann::json::object(); }
        }
    }

    const std::string sceneKey{ currentScene->GetSceneName() };
    const DirectX::XMFLOAT3 pos{ activeCamera->GetPosition() };
    const DirectX::XMFLOAT3 rot{ activeCamera->GetRotation() };

    root[sceneKey]["CamPosX"] = pos.x;
    root[sceneKey]["CamPosY"] = pos.y;
    root[sceneKey]["CamPosZ"] = pos.z;

    root[sceneKey]["CamRotX"] = rot.x;
    root[sceneKey]["CamRotY"] = rot.y;
    root[sceneKey]["CamRotZ"] = rot.z;

    root["EditorUI"]["ShowProfiler"] = m_showProfiler;
    root["EditorUI"]["ShowPostProcess"] = m_showPostProcess;
    root["EditorUI"]["ShowAnimTimeline"] = m_showAnimTimeline;
    root["EditorUI"]["ShowAnimGraph"] = m_showAnimGraph;

    const std::filesystem::path pathObj{ s_editorPrefsPath };
    if (!std::filesystem::exists(pathObj.parent_path()))
    {
        std::filesystem::create_directories(pathObj.parent_path());
    }

    std::ofstream outFile{ std::string{ s_editorPrefsPath } };
    if (outFile.is_open())
    {
        outFile << root.dump(4);
    }
}

void EditorManager::LoadUserPreferences(Scene* currentScene, Camera* activeCamera) noexcept
{
    if (!currentScene || !activeCamera || !std::filesystem::exists(s_editorPrefsPath)) return;

    std::ifstream inFile{ std::string{ s_editorPrefsPath } };
    if (!inFile.is_open()) return;

    try
    {
        nlohmann::json root{};
        inFile >> root;

        const std::string sceneKey{ currentScene->GetSceneName() };
        if (root.contains(sceneKey))
        {
            const auto& data = root[sceneKey];

            const DirectX::XMFLOAT3 pos{
                data.value("CamPosX", 0.001f),
                data.value("CamPosY", 18.0f),
                data.value("CamPosZ", -14.0f)
            };

            const DirectX::XMFLOAT3 rot{
                data.value("CamRotX", 0.0f),
                data.value("CamRotY", 0.0f),
                data.value("CamRotZ", 0.0f)
            };

            activeCamera->SetPosition(pos);
            activeCamera->SetRotation(rot);
        }

        if (root.contains("EditorUI"))
        {
            const auto& uiData = root["EditorUI"];
            m_showProfiler = uiData.value("ShowProfiler", false);
            m_showPostProcess = uiData.value("ShowPostProcess", false);
            m_showAnimTimeline = uiData.value("ShowAnimTimeline", false);
            m_showAnimGraph = uiData.value("ShowAnimGraph", false);
        }
    }
    catch (...)
    {
    }
}