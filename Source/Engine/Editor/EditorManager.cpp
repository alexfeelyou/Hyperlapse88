#include "EditorManager.h"
#include "LightComponent.h"

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
}

namespace
{
    enum class ToolbarIcon : std::uint8_t
    {
        Play = 0,
        Pause,
        Stop
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

            // Left bar
            drawList->AddRectFilled(
                ImVec2{ center.x - barGap - barWidth, center.y - barHalfHeight },
                ImVec2{ center.x - barGap,            center.y + barHalfHeight },
                iconColor,
                rounding
            );
            // Right bar
            drawList->AddRectFilled(
                ImVec2{ center.x + barGap,            center.y - barHalfHeight },
                ImVec2{ center.x + barGap + barWidth, center.y + barHalfHeight },
                iconColor,
                rounding
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
                iconColor,
                rounding
            );
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
    // EARLY OUT: The compiler optimizes this entire function away in Release mode
    if constexpr (!s_isDebugMode) return;

    ImGuiIO& io{ ImGui::GetIO() };
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ApplyStyle(); // Apply the custom theme
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
    // Retrieve the global ImGui style instance and its color array
    ImGuiStyle& style{ ImGui::GetStyle() };
    ImVec4* colors{ style.Colors };

    // Establish a baseline dark theme
    ImGui::StyleColorsDark();

    // Define the core color palette
    const ImVec4 deepNavyBg{ 0.0f, 0.043f, 0.118f, 1.00f };         // Base editor background
    const ImVec4 activeTabBg{ 0.929f, 0.094f, 0.541f, 1.00f };      // Active contexts
    const ImVec4 hoverTabBg{ 0.309f, 0.043f, 0.117f, 1.00f };       // Highlight
    const ImVec4 darkTitleBg{ 0.04f, 0.05f, 0.08f, 1.00f };         // Unfocused/header areas

    // Customize Window and Child backgrounds
    colors[ImGuiCol_WindowBg]   = deepNavyBg;
    colors[ImGuiCol_ChildBg]    = deepNavyBg;

    // Customize Tabs (Normal, Hovered, Active, and Unfocused)
    colors[ImGuiCol_Tab]                = hoverTabBg;
    colors[ImGuiCol_TabHovered]         = activeTabBg;
    colors[ImGuiCol_TabActive]          = activeTabBg;
    colors[ImGuiCol_TabUnfocused]       = darkTitleBg;
    colors[ImGuiCol_TabUnfocusedActive] = hoverTabBg;
    colors[ImGuiCol_TabSelectedOverline]= activeTabBg;

    // Customize Title Bars
    colors[ImGuiCol_TitleBg]            = deepNavyBg;
    colors[ImGuiCol_TitleBgActive]      = deepNavyBg;
    colors[ImGuiCol_TitleBgCollapsed]   = darkTitleBg;

    // Customize Menu Bar and Menu Item Header colors
    colors[ImGuiCol_MenuBarBg]          = darkTitleBg;
    colors[ImGuiCol_Header]             = hoverTabBg;
    colors[ImGuiCol_HeaderHovered]      = hoverTabBg;
    colors[ImGuiCol_HeaderActive]       = hoverTabBg;

    // Customize Dock splitters and window resize separators
    colors[ImGuiCol_Border]             = hoverTabBg;
    colors[ImGuiCol_ResizeGripHovered]  = activeTabBg;
    colors[ImGuiCol_ResizeGripActive]   = activeTabBg;

    // Docking Overlays
    colors[ImGuiCol_DockingPreview]     = ImVec4{ activeTabBg.x, activeTabBg.y, activeTabBg.z, 0.40f };
    colors[ImGuiCol_DockingEmptyBg]     = deepNavyBg;

    // Adjust rounding settings
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

    // Pass the scene into the Menu Bar
    DrawMenuBar(currentScene);

    const ImGuiID dockspaceId{ ImGui::GetID("MainDockSpace") };

    static bool s_firstTime{ true };
    if (s_firstTime)
    {
        s_firstTime = false;

        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_PassthruCentralNode);
        ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

        // Split the nodes based on screen percentages
        ImGuiID dockMain{ dockspaceId };
        const ImGuiID dockLeft{ ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.20f, nullptr, &dockMain) };
        const ImGuiID dockRight{ ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.25f, nullptr, &dockMain) };
        const ImGuiID dockBottom{ ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.29f, nullptr, &dockMain) };

        // Snap the permanent windows to their assigned docks
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

    // Release before recreating so DX frees the old memory first
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
    constexpr ImGuiWindowFlags windowFlags{
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse
    };

    ImGui::Begin(s_windowSceneView, nullptr, windowFlags);

    // Tell the camera if the mouse is currently inside this specific Scene View window
    CameraController::Instance().SetViewportHovered(ImGui::IsWindowHovered());

    // Embedded scene toolbar
    constexpr ImVec2 buttonSize{ 34.0f, 22.0f };
    const float totalToolbarWidth{ (buttonSize.x * 3.0f) + (ImGui::GetStyle().ItemSpacing.x * 2.0f) };

    // Center the buttons horizontally based on the Scene View's current width
    const float availWidth{ ImGui::GetContentRegionAvail().x };
    ImGui::SetCursorPosX((availWidth * 0.5f) - (totalToolbarWidth * 0.5f));

    // Play Button
    if (DrawToolbarIconButton("##PlayBtn", ToolbarIcon::Play, m_editorMode == EditorMode::Play, ImVec4{ 0.2f, 0.7f, 0.2f, 1.0f }, buttonSize))
    {
        SetEditorMode(EditorMode::Play);
    }

    ImGui::SameLine();

    // Pause Button
    if (DrawToolbarIconButton("##PauseBtn", ToolbarIcon::Pause, m_editorMode == EditorMode::Pause, ImVec4{ 0.7f, 0.7f, 0.2f, 1.0f }, buttonSize))
    {
        SetEditorMode(EditorMode::Pause);
    }

    ImGui::SameLine();

    // Stop Button
    if (DrawToolbarIconButton("##StopBtn", ToolbarIcon::Stop, m_editorMode == EditorMode::Edit, ImVec4{ 0.7f, 0.2f, 0.2f, 1.0f }, buttonSize))
    {
        SetEditorMode(EditorMode::Edit);
    }

	// Scene texture rendering 
    const auto* mainWindow{ WindowManager::Instance().GetWindowByIndex(0) };
    const float gameWidth{ mainWindow ? static_cast<float>(mainWindow->GetWidth()) : 1920.0f };
    const float gameHeight{ mainWindow ? static_cast<float>(mainWindow->GetHeight()) : 1080.0f };

    if (m_sceneWidth != gameWidth || m_sceneHeight != gameHeight)
    {
        m_sceneWidth = gameWidth;
        m_sceneHeight = gameHeight;
        EnsureSceneRenderTarget(static_cast<UINT>(gameWidth), static_cast<UINT>(gameHeight));
    }

    const ImVec2 availSize{ ImGui::GetContentRegionAvail() };

    if (availSize.x <= 0.0f || availSize.y <= 0.0f)
    {
        ImGui::End();
        return;
    }

    const float targetAspect{ m_sceneWidth / m_sceneHeight };
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

    // Cache the exact screen coordinates before we shift the cursor to draw the image
    ImVec2 screenCursorPos{ ImGui::GetCursorScreenPos() };
    screenCursorPos.x += cursorOffset.x;
    screenCursorPos.y += cursorOffset.y;

    const ImVec2 originalCursorPos{ ImGui::GetCursorPos() };
    ImGui::SetCursorPos(ImVec2{ originalCursorPos.x + cursorOffset.x, originalCursorPos.y + cursorOffset.y });

    if (m_sceneSRV)
    {
        ImGui::Image(reinterpret_cast<ImTextureID>(m_sceneSRV.Get()), renderSize);
    }


	// Gizmo hotkeys (W, E, R) only work when the Scene View is focused or hovered
    if ((ImGui::IsWindowFocused() || ImGui::IsWindowHovered()) && !ImGui::IsMouseDown(ImGuiMouseButton_Right))
    {
        // Standard Maya/Unity hotkeys
        if (ImGui::IsKeyPressed(ImGuiKey_W)) m_gizmoOperation = ImGuizmo::TRANSLATE;
        if (ImGui::IsKeyPressed(ImGuiKey_E)) m_gizmoOperation = ImGuizmo::ROTATE;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) m_gizmoOperation = ImGuizmo::SCALE;
    }

    // Gizmo Gating Pipeline
    const bool isPlayMode{ m_editorMode == EditorMode::Play };
    const bool hasSelection{ m_selectedObject != nullptr };
    const bool isNotRoot{ currentScene && (m_selectedObject != currentScene->GetRootGameObject()) };

    // Only draw the manipulator if all architectural conditions are met
    if (activeCamera && hasSelection && isNotRoot && !isPlayMode)
    {
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(screenCursorPos.x, screenCursorPos.y, renderSize.x, renderSize.y);

        DirectX::XMFLOAT4X4 view{ activeCamera->GetView() };
        DirectX::XMFLOAT4X4 proj{ activeCamera->GetProjection() };
        DirectX::XMFLOAT4X4 objectMatrix{};
        {
            // Build the matrix 
            const DirectX::XMFLOAT3 pos{ m_selectedObject->transform.position };
            const DirectX::XMFLOAT3 rot{ m_selectedObject->transform.rotation };
            const DirectX::XMFLOAT3 scl{ m_selectedObject->transform.scale };

            const DirectX::XMMATRIX S{ DirectX::XMMatrixScaling(scl.x, scl.y, scl.z) };
            const DirectX::XMMATRIX R{ DirectX::XMMatrixRotationRollPitchYaw(
                DirectX::XMConvertToRadians(rot.x),
                DirectX::XMConvertToRadians(rot.y),
                DirectX::XMConvertToRadians(rot.z))
            };
            const DirectX::XMMATRIX T{ DirectX::XMMatrixTranslation(pos.x, pos.y, pos.z) };

            DirectX::XMStoreFloat4x4(&objectMatrix, S * R * T);
        }

        // Required to prevent axes from becoming unclickable at steep camera angles
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::Manipulate(&view._11, &proj._11, m_gizmoOperation, m_gizmoMode, &objectMatrix._11);

        if (ImGuizmo::IsUsing())
        {
            // Decompose natively using DirectXMath 
            DirectX::XMMATRIX mat = DirectX::XMLoadFloat4x4(&objectMatrix);
            DirectX::XMVECTOR vScale, vRotQuat, vTrans;

            if (DirectX::XMMatrixDecompose(&vScale, &vRotQuat, &vTrans, mat))
            {
                DirectX::XMFLOAT3 newPos, newScale;
                DirectX::XMStoreFloat3(&newPos, vTrans);
                DirectX::XMStoreFloat3(&newScale, vScale);

                // Convert Quaternion to a clean 3x3 Rotation Matrix to eliminate scale bias
                DirectX::XMMATRIX rotMat = DirectX::XMMatrixRotationQuaternion(vRotQuat);
                DirectX::XMFLOAT4X4 mRot;
                DirectX::XMStoreFloat4x4(&mRot, rotMat);

                // Extract Pitch (X), Yaw (Y), and Roll (Z) manually based on DXMath's order
                float pitch = asinf(std::clamp(-mRot._32, -1.0f, 1.0f));
                float yaw, roll;

                // Gimbal Lock Protection (If Pitch is exactly 90 or -90 degrees)
                if (cosf(pitch) > 0.0001f)
                {
                    yaw = atan2f(mRot._31, mRot._33);
                    roll = atan2f(mRot._12, mRot._22);
                }
                else
                {
                    // At extreme pitch, Roll and Yaw align. Force Roll to 0 and give all rotation to Yaw.
                    yaw = atan2f(-mRot._13, mRot._11);
                    roll = 0.0f;
                }

                // Write directly to the Transform struct
                m_selectedObject->transform.position = newPos;
                m_selectedObject->transform.rotation = {
                    DirectX::XMConvertToDegrees(pitch),
                    DirectX::XMConvertToDegrees(yaw),
                    DirectX::XMConvertToDegrees(roll)
                };
                m_selectedObject->transform.scale = newScale;
            }
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
                    // Only save the root objects, stripping out all enemy/item manager saves
                    SceneSerializer::Save(
                        currentScene->GetSceneSavePath(),
                        currentScene->GetRootGameObject()
                    );
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

    // Configure ImGui visual flags
    ImGuiTreeNodeFlags flags{ ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth };

    if (m_selectedObject == node)
    {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    if (node->GetChildren().empty())
    {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }

    // Draw the node
    const bool isOpen{ ImGui::TreeNodeEx(static_cast<void*>(node), flags, "%s", node->GetName().c_str()) };

    // Left-Click: Select entity for the Inspector
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
    {
        m_selectedObject = node;
    }

    // Right-Click Context Menu: Unity-style child creation and deletion
    if (ImGui::BeginPopupContextItem())
    {
        m_selectedObject = node; // Auto-select on right click

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
            node->Destroy(); // Triggers deferred deletion on next frame
            if (m_selectedObject == node)
            {
                m_selectedObject = nullptr;
            }
        }

        ImGui::EndPopup();
    }

    // If expanded, recurse through children
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
    // If the object selected in the Inspector was destroyed this frame, clear it immediately
    if (m_selectedObject && m_selectedObject->IsDestroyed())
    {
        m_selectedObject = nullptr;
    }

    ImGui::Begin(s_windowHierarchy);

    if (currentScene && currentScene->GetRootGameObject())
    {
        // "+ CREATE" Drop Down Menu
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

           // Only show Gameplay Entities if we are currently editing the Game Scene
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
                        lightObj->transform.rotation = { 90.0f, 0.0f, 0.0f }; // Aim straight down
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
            // Iterate directly over top-level children, bypassing the SceneRoot node entirely
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

    // Deselect if clicking on empty space in the hierarchy window
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
    {
        m_selectedObject = nullptr;
    }

    ImGui::End();
}

void EditorManager::DrawInspector(Scene* currentScene) noexcept
{
    ImGui::Begin(s_windowInspector);

    // If an object is selected in the Hierarchy, draw its GameObject Inspector
    if (m_selectedObject)
    {
        m_selectedObject->DrawInspector();
    }
    // Fallback: Deselected State (Scene Metadata & Level Settings View)
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
        
        // Render custom per-scene debug settings without file operation buttons
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
            ImVec4 color{ 1.0f, 1.0f, 1.0f, 1.0f }; // Info = White

            switch (entry.level)
            {
            case LogLevel::Success: color = ImVec4{ 0.2f, 0.9f, 0.2f, 1.0f }; break; // Green
            case LogLevel::Warning: color = ImVec4{ 1.0f, 0.8f, 0.0f, 1.0f }; break; // Yellow
            case LogLevel::Error:   color = ImVec4{ 1.0f, 0.2f, 0.2f, 1.0f }; break; // Red
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
    // Early-out if the user closed the window via the menu or the 'X' button
    if (!m_showProfiler) return;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0.0f, 0.0f });

    // Passing &m_showProfiler adds the 'X' close button to the tab
    if (ImGui::Begin(s_windowProfiler, const_cast<bool*>(&m_showProfiler)))
    {
        ImGui::PopStyleVar(); // Pop immediately so internal elements pad normally

        const auto& cpuData{ ProfilerManager::Instance().GetCpuData() };
        const auto& metrics{ ProfilerManager::Instance().GetMetrics() };
        const std::size_t currentIndex{ ProfilerManager::Instance().GetCurrentFrameIndex() };

        const int maxFrames{ static_cast<int>(MAX_PROFILE_FRAMES) };
        const int offset{ static_cast<int>(currentIndex) };

        // TOP PANEL: Frame Time Graph
        // Chosen over per-scope CPU timings as the headline visual: it answers
        // "does this run well?" at a glance for any viewer, technical or not
        if (ImPlot::BeginPlot("##FrameTime", ImVec2{ -1.0f, -90.0f }))
        {
            ImPlot::SetupAxes(nullptr, "Frame Time (ms)", ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_AutoFit);
            ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, static_cast<double>(maxFrames), ImPlotCond_Always);

            // The 16.6ms line marks the 60fps budget — any spike above it is a dropped frame
            static double targetFrameTimeMs{ 16.666 };
            ImPlot::DragLineY(0, &targetFrameTimeMs, ImVec4{ 0.9f, 0.1f, 0.1f, 0.8f }, 1.0f, ImPlotDragToolFlags_NoInputs);

            const auto& frameTimeHistory{ ProfilerManager::Instance().GetFrameTimeHistory() };

            ImPlotSpec spec{};
            spec.Offset = offset;
            spec.LineWeight = 1.5f;

            ImPlot::PlotLine("Frame Time", frameTimeHistory.data(), maxFrames, 1.0, 0.0, spec);
            ImPlot::EndPlot();
        }

        // BOTTOM PANEL: Metrics Dashboard
        ImGui::Separator();

        // Add subtle padding around the table
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);

        // A 4-column table for organizing readouts
        if (ImGui::BeginTable("MetricsDashboard", 4, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableNextRow();

            // Column 1: Core Performance
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("PERFORMANCE");
            ImGui::Text("FPS: %.1f", metrics.fps);
            ImGui::Text("Frame: %.2f ms", (1000.0f / (std::max)(metrics.fps, 1.0f)));

            // Column 2: Memory
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("MEMORY");
            ImGui::Text("Sys RAM: %.1f MB", metrics.ramUsageMB);
            ImGui::Text("GPU VRAM: %.1f MB", metrics.vramUsageMB);

            // Column 3: CPU Scopes
            ImGui::TableSetColumnIndex(2);
            ImGui::TextDisabled("CPU TIMINGS");

            for (const auto& [name, data] : cpuData)
            {
                // Draw Calls lives in its own dedicated column below, not mixed into
                // the CPU-scope ms readouts
                if (name == std::string_view{ "Draw Calls (3D)" }) continue;

                ImGui::Text("%s: %.2f ms", name, data.lastFrameTime);
            }

            // Column 4: Draw Call and Triangle counts, read straight from the dedicated
            // accessors rather than pulled out of the generic CPU-timer map
            ImGui::TableSetColumnIndex(3);
            ImGui::TextDisabled("DRAW CALLS (3D)");
            ImGui::Text("Calls: %zu", ProfilerManager::Instance().GetLastFrameDrawCallCount());
            ImGui::Text("Triangles: %zu", ProfilerManager::Instance().GetLastFrameTriangleCount());

            ImGui::EndTable();
        }
    }
    else
    {
        ImGui::PopStyleVar(); // Safety pop if window is collapsed
    }
    ImGui::End();
}

void EditorManager::DrawPostProcess(Scene* currentScene) noexcept
{
    // Avoid processing ImGui logic if the user hasn't toggled the window open
    if (!m_showPostProcess) return;

    // Pass the boolean pointer so ImGui renders an 'X' close button in the title bar
    if (ImGui::Begin(s_windowPostProcess, &m_showPostProcess))
    {
        if (currentScene && currentScene->GetPostProcessManager())
        {
            auto* ppm = currentScene->GetPostProcessManager();

            // TOOLBAR: Save / Undo / Reset
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

			// Master toggle for the entire post-processing graph
            bool masterEnabled = ppm->IsEnabled();
            if (ImGui::Checkbox("Master Post-Process Enabled", &masterEnabled))
            {
                ppm->SetEnabled(masterEnabled);
            }
            ImGui::Separator();

            // Automatically renders ImGui controls for every discrete effect pass
            ImGui::BeginDisabled(!masterEnabled);
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