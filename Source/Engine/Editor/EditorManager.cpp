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
    inline constexpr const char* s_windowHierarchy{ "Hierarchy" };
    inline constexpr const char* s_windowInspector{ "Inspector" };
    inline constexpr const char* s_windowConsole{ "Console" };
    inline constexpr const char* s_windowProfiler{ "Profiler" };
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
}

void EditorManager::Draw(Scene* currentScene) const noexcept
{
    if constexpr (!s_isDebugMode) return;

    DrawDockSpace();

    DrawHierarchy();
    DrawInspector(currentScene);
    DrawConsole();
    DrawProfiler();

    ImGui::End();
}

void EditorManager::DrawDockSpace() const noexcept
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

    // --- CARVE THE UNITY-STYLE LAYOUT ONCE ---
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
        ImGui::DockBuilderDockWindow(s_windowHierarchy, dockLeft);
        ImGui::DockBuilderDockWindow(s_windowInspector, dockRight);
        ImGui::DockBuilderDockWindow(s_windowConsole, dockBottom);
        ImGui::DockBuilderDockWindow(s_windowProfiler, dockBottom); 

        ImGui::DockBuilderFinish(dockspaceId);
    }

    ImGui::DockSpace(dockspaceId, ImVec2{ 0.0f, 0.0f }, ImGuiDockNodeFlags_PassthruCentralNode);
}

void EditorManager::DrawMenuBar() const noexcept
{
    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("SceneSelect")) { ImGui::EndMenu(); }
        if (ImGui::BeginMenu("Debug")) { ImGui::EndMenu(); }
        if (ImGui::BeginMenu("Graphics")) { ImGui::EndMenu(); }
        if (ImGui::BeginMenu("Time")) { ImGui::EndMenu(); }
        if (ImGui::BeginMenu("CurveManager")) { ImGui::EndMenu(); }
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