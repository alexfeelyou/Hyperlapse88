#include "EditorManager.h"

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
    inline constexpr const char* s_windowPostProcess{ "Post Processing" };
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

void EditorManager::Draw(Scene* currentScene) noexcept
{
    if constexpr (!s_isDebugMode) return;

    DrawDockSpace();

    DrawSceneView();
    DrawHierarchy();
    DrawInspector(currentScene);
    DrawConsole();
    DrawProfiler();
    DrawPostProcess(currentScene);

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

void EditorManager::DrawDockSpace() noexcept
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

    DrawMenuBar();

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
        const ImGuiID dockBottom{ ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.25f, nullptr, &dockMain) };

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

void EditorManager::DrawSceneView() noexcept
{
    // Prevent the user from scrolling or collapsing this critical editor tab
    constexpr ImGuiWindowFlags windowFlags{
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoCollapse
    };

    ImGui::Begin(s_windowSceneView, nullptr, windowFlags);

    // Sync Render Target to Logical Game Resolution
    // By locking the texture to the game's actual window size instead of the dock size, 
    // UI matrices and viewports align 1:1
    const auto* mainWindow{ WindowManager::Instance().GetWindowByIndex(0) };
    const float gameWidth{ mainWindow ? static_cast<float>(mainWindow->GetWidth()) : 1920.0f };
    const float gameHeight{ mainWindow ? static_cast<float>(mainWindow->GetHeight()) : 1080.0f };

    // Reallocate the DirectX render target ONLY if the game's logical resolution changed
    if (m_sceneWidth != gameWidth || m_sceneHeight != gameHeight)
    {
        m_sceneWidth = gameWidth;
        m_sceneHeight = gameHeight;
        EnsureSceneRenderTarget(static_cast<UINT>(gameWidth), static_cast<UINT>(gameHeight));
    }

    // Calculate Letterbox/Pillarbox for ImGui Display
    const ImVec2 availSize{ ImGui::GetContentRegionAvail() };

    // EARLY OUT: Protect against minimized windows causing division-by-zero
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
        // Pillarbox: Window is too wide. Constrain width based on height.
        renderSize.x = availSize.y * targetAspect;
        cursorOffset.x = (availSize.x - renderSize.x) * 0.5f;
    }
    else
    {
        // Letterbox: Window is too tall. Constrain height based on width.
        renderSize.y = availSize.x / targetAspect;
        cursorOffset.y = (availSize.y - renderSize.y) * 0.5f;
    }

    // Shift the ImGui cursor to perfectly center the texture in the dock node
    const ImVec2 cursorPos{ ImGui::GetCursorPos() };
    ImGui::SetCursorPos(ImVec2{ cursorPos.x + cursorOffset.x, cursorPos.y + cursorOffset.y });

    // Render the Texture
    // ImGui automatically handles sampling and downscaling the 1080p SRV to the calculated renderSize
    if (m_sceneSRV)
    {
        ImGui::Image(reinterpret_cast<ImTextureID>(m_sceneSRV.Get()), renderSize);
    }

    ImGui::End();
}

void EditorManager::DrawMenuBar() noexcept
{
    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("Scene"))
        {
            // Swap to the Title when clicked
            if (ImGui::MenuItem("Title"))
            {
                Framework::Instance()->ChangeScene(std::make_unique<SceneTitle>());
            }

            // Swap to the Game when clicked
            if (ImGui::MenuItem("Game"))
            {
                Framework::Instance()->ChangeScene(std::make_unique<SceneGame>());
            }

            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Debug")) { ImGui::EndMenu(); }
        if (ImGui::BeginMenu("Graphics"))
        {
            // Pass the address of the boolean to toggle window visibility
            ImGui::MenuItem("Post Processing Panel", nullptr, &m_showPostProcessPanel);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Time")) { ImGui::EndMenu(); }
        if (ImGui::BeginMenu("Curve Manager")) { ImGui::EndMenu(); }
        ImGui::EndMenuBar();
    }
}

void EditorManager::DrawHierarchy() const noexcept
{
    ImGui::Begin(s_windowHierarchy);
    ImGui::End();
}

void EditorManager::DrawInspector(Scene* currentScene) const noexcept
{
    ImGui::Begin(s_windowInspector);

    // Fallback protection: safely ignore if no scene is loaded
    if (currentScene)
    {
        currentScene->DrawGUI();
    }

    ImGui::End();
}

void EditorManager::DrawConsole() const noexcept
{
    ImGui::Begin(s_windowConsole);
    ImGui::End();
}

void EditorManager::DrawProfiler() const noexcept
{
    ImGui::Begin(s_windowProfiler);
    ImGui::End();
}

void EditorManager::DrawPostProcess(Scene* currentScene) noexcept
{
    // Avoid processing ImGui logic if the user hasn't toggled the window open
    if (!m_showPostProcessPanel) return;

    // Pass the boolean pointer so ImGui renders an 'X' close button in the title bar
    if (ImGui::Begin(s_windowPostProcess, &m_showPostProcessPanel))
    {
        if (currentScene && currentScene->GetPostProcessManager())
        {
            auto* ppm = currentScene->GetPostProcessManager();

            // TOOLBAR: Save / Undo / Reset
            static constexpr std::string_view configPath{ "Data/Config/PostProcess.json" };

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