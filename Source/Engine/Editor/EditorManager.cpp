#include <filesystem>
#include <fstream>
#include <json.hpp>
#include "Camera.h"
#include "EditorManager.h"
#include "LightComponent.h"
#include "StaticMeshColliderComponent.h"

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
    enum class ToolbarIcon : std::uint8_t
    {
        Play = 0,
        Pause,
        Stop,
        Maximize
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
        }

        return isClicked;
    }
}

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

    CameraController::Instance().SetViewportHovered(ImGui::IsWindowHovered());

    constexpr ImVec2 buttonSize{ 34.0f, 22.0f };
    const float totalToolbarWidth{ (buttonSize.x * 3.0f) + (ImGui::GetStyle().ItemSpacing.x * 2.0f) };

    if (s_isSceneMaximized) ImGui::SetCursorPosY(8.0f);

    const float availWidth{ ImGui::GetContentRegionAvail().x };
    ImGui::SetCursorPosX((availWidth * 0.5f) - (totalToolbarWidth * 0.5f));

    if (DrawToolbarIconButton("##PlayBtn", ToolbarIcon::Play, m_editorMode == EditorMode::Play, ImVec4{ 0.2f, 0.7f, 0.2f, 1.0f }, buttonSize))
        SetEditorMode(EditorMode::Play);
    ImGui::SameLine();
    if (DrawToolbarIconButton("##PauseBtn", ToolbarIcon::Pause, m_editorMode == EditorMode::Pause, ImVec4{ 0.7f, 0.7f, 0.2f, 1.0f }, buttonSize))
        SetEditorMode(EditorMode::Pause);
    ImGui::SameLine();
    if (DrawToolbarIconButton("##StopBtn", ToolbarIcon::Stop, m_editorMode == EditorMode::Edit, ImVec4{ 0.7f, 0.2f, 0.2f, 1.0f }, buttonSize))
        SetEditorMode(EditorMode::Edit);

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

    if (m_sceneSRV)
    {
        ImGui::Image(reinterpret_cast<ImTextureID>(m_sceneSRV.Get()), renderSize);
    }

    if ((ImGui::IsWindowFocused() || ImGui::IsWindowHovered()) && !ImGui::IsMouseDown(ImGuiMouseButton_Right))
    {
        if (ImGui::IsKeyPressed(ImGuiKey_W)) m_gizmoOperation = ImGuizmo::TRANSLATE;
        if (ImGui::IsKeyPressed(ImGuiKey_E)) m_gizmoOperation = ImGuizmo::ROTATE;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) m_gizmoOperation = ImGuizmo::SCALE;
    }

    const bool isPlayMode{ m_editorMode == EditorMode::Play };
    const bool hasSelection{ m_selectedObject != nullptr };
    const bool isNotRoot{ currentScene && (m_selectedObject != currentScene->GetRootGameObject()) };

    if (activeCamera && hasSelection && isNotRoot && !isPlayMode)
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

            const DirectX::XMMATRIX locRot{ DirectX::XMMatrixRotationRollPitchYaw(
                DirectX::XMConvertToRadians(config.localRotation.x),
                DirectX::XMConvertToRadians(config.localRotation.y),
                DirectX::XMConvertToRadians(config.localRotation.z))
            };
            const DirectX::XMMATRIX locTrans{ DirectX::XMMatrixTranslation(config.localOffset.x, config.localOffset.y, config.localOffset.z) };

            DirectX::XMStoreFloat4x4(&targetMatrix, locRot * locTrans * objWorld);
        }

        ImGuizmo::SetOrthographic(false);
        ImGuizmo::Manipulate(&view._11, &proj._11, m_gizmoOperation, m_gizmoMode, &targetMatrix._11);

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

                    DirectX::XMFLOAT3 scaleDelta;
                    DirectX::XMStoreFloat3(&scaleDelta, vScale);
                    config.proxyExtents.x *= scaleDelta.x;
                    config.proxyExtents.y *= scaleDelta.y;
                    config.proxyExtents.z *= scaleDelta.z;

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

        if (ImGui::BeginMenu("Scene"))
        {
            if (ImGui::MenuItem("Title"))
            {
                Framework::Instance()->ChangeScene([]() { return std::make_unique<SceneTitle>(); });
            }
            if (ImGui::MenuItem("Game"))
            {
                Framework::Instance()->ChangeScene([]() { return std::make_unique<SceneGame>(); });
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Debug"))
        {
            ImGui::MenuItem("Profiler", nullptr, &m_showProfiler);
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

void EditorManager::DrawHierarchyNode(GameObject* node) noexcept
{
    if (!node) return;

    ImGuiTreeNodeFlags flags{ ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth };

    if (m_selectedObject == node)
    {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (node->GetChildren().empty())
    {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }

    const bool isOpen{ ImGui::TreeNodeEx(static_cast<void*>(node), flags, "%s", node->GetName().c_str()) };

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
    {
        m_selectedObject = node;
        m_selectedComponent = nullptr;
    }

    if (ImGui::BeginPopupContextItem())
    {
        m_selectedObject = node;

        if (ImGui::MenuItem("Create Empty Child"))
        {
            node->AddChild(std::make_unique<GameObject>("New_Child"));
        }
        if (ImGui::MenuItem("Create Socket Anchor"))
        {
            node->AddChild(std::make_unique<GameObject>("Socket_Anchor"));
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Delete GameObject"))
        {
            node->Destroy();
            if (m_selectedObject == node)
            {
                m_selectedObject = nullptr;
            }
        }
        ImGui::EndPopup();
    }

    if (isOpen)
    {
        for (const auto& child : node->GetChildren())
        {
            DrawHierarchyNode(child.get());
        }
        ImGui::TreePop();
    }
}

void EditorManager::DrawHierarchy(Scene* currentScene) noexcept
{
    if (m_selectedObject && m_selectedObject->IsDestroyed())
    {
        m_selectedObject = nullptr;
    }

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
                currentScene->GetRootGameObject()->AddChild(std::make_unique<GameObject>("Empty"));
            }

            if (auto* gameScene{ dynamic_cast<SceneGame*>(currentScene) })
            {
                ImGui::Separator();
                ImGui::TextDisabled("Lighting");

                if (ImGui::BeginMenu("Light"))
                {
                    if (ImGui::MenuItem("Directional Light"))
                    {
                        auto lightObj = std::make_unique<GameObject>("Directional_Light");
                        lightObj->transform.rotation = { 45.0f, -45.0f, 0.0f };
                        lightObj->AddComponent<DirectionalLightComponent>();
                        currentScene->GetRootGameObject()->AddChild(std::move(lightObj));
                    }
                    if (ImGui::MenuItem("Point Light"))
                    {
                        auto lightObj = std::make_unique<GameObject>("Point_Light");
                        lightObj->transform.position = { 0.0f, 3.0f, 0.0f };
                        lightObj->AddComponent<PointLightComponent>();
                        currentScene->GetRootGameObject()->AddChild(std::move(lightObj));
                    }
                    if (ImGui::MenuItem("Spot Light"))
                    {
                        auto lightObj = std::make_unique<GameObject>("Spot_Light");
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

        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4{ 0.15f, 0.15f, 0.15f, 1.0f });
        const bool isSceneNodeOpen{ ImGui::CollapsingHeader(currentScene->GetSceneName().data(), ImGuiTreeNodeFlags_DefaultOpen) };
        ImGui::PopStyleColor();

        if (isSceneNodeOpen)
        {
            for (const auto& child : currentScene->GetRootGameObject()->GetChildren())
            {
                DrawHierarchyNode(child.get());
            }
        }
    }
    else
    {
        ImGui::TextDisabled("No Scene Loaded");
    }

    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
    {
        m_selectedObject = nullptr;
    }

    ImGui::End();
}

void EditorManager::DrawInspector(Scene* currentScene) noexcept
{
    ImGui::Begin(s_windowInspector);

    if (m_selectedObject)
    {
        m_selectedObject->DrawInspector();
    }
    else if (currentScene)
    {
        ImGui::TextDisabled("SCENE PROPERTIES");
        ImGui::Separator();

        Graphics::Instance().GetLightManager().DrawEnvironmentGUI();
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
            if (ImGui::Button("Undo"))
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

void EditorManager::LoadUserPreferences(Scene* currentScene, Camera* activeCamera) const noexcept
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
    }
    catch (...)
    {
    }
}