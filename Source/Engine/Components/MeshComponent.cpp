#include <imgui.h>
#include "System/AssetManager.h"
#include "System/Graphics.h"
#include "ComponentRegistry.h"
#include "MeshComponent.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>

namespace
{
    // Helper to open the native Windows File Explorer dialog
    [[nodiscard]] std::string OpenFileDialog() noexcept
    {
        char filename[MAX_PATH]{ "" };

        OPENFILENAMEA ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = nullptr;
        // Filter for 3D models
        ofn.lpstrFilter = "3D Models (*.gltf;*.glb)\0*.gltf;*.glb\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = filename;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
        ofn.lpstrDefExt = "";

        if (GetOpenFileNameA(&ofn))
        {
            return std::string{ filename };
        }
        return ""; // User canceled
    }
}

MeshComponent::MeshComponent(std::shared_ptr<Model> model) noexcept
{
    SetModel(std::move(model), "Code-Instantiated");
}

void MeshComponent::SetModel(std::shared_ptr<Model> model, std::string_view path) noexcept
{
    m_model = std::move(model);
    if (!path.empty())
    {
        m_modelPath = path;
    }
}

void MeshComponent::Render(ModelRenderer* renderer)
{
    // Fast fail on invalid state
    if (!m_owner || !m_model || !renderer) return;

    // Retrieve the fully resolved world matrix (Position * Rotation * Scale) from the GameObject
    const DirectX::XMFLOAT4X4 worldMatrix{ m_owner->transform.GetWorldMatrix() };

    // Submit to the ModelRenderer using the manual matrix overload 
    renderer->Draw(m_shaderId, m_model, m_color, worldMatrix);
}

void MeshComponent::DrawInspector()
{
    // Asset browser UI
    ImGui::TextDisabled("MODEL ASSET");

    // Display current path in a read-only field
    ImGui::PushItemWidth(-80.0f); // Leave room for the Browse button
    ImGui::InputText("##ModelPath", m_modelPath.data(), m_modelPath.capacity(), ImGuiInputTextFlags_ReadOnly);
    ImGui::PopItemWidth();

    ImGui::SameLine();

    if (ImGui::Button("Browse..."))
    {
        const std::string newPath{ OpenFileDialog() };
        if (!newPath.empty())
        {
            // Use the AssetManager to load (or fetch cached) model
            auto* device{ Graphics::Instance().GetDevice() };
            if (auto newModel{ Engine::System::AssetManager::Instance().GetOrLoadModel(device, newPath) })
            {
                SetModel(std::move(newModel), newPath);
            }
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Master overrides (Legacy compatibility)
    if (ImGui::CollapsingHeader("Master Rendering Settings", ImGuiTreeNodeFlags_DefaultOpen))
    {
        static constexpr const char* const s_shaderNames[]{ "Basic", "Lambert", "Phong" };
        int currentShader{ static_cast<int>(m_shaderId) };
        if (ImGui::Combo("Master Shader", &currentShader, s_shaderNames, IM_ARRAYSIZE(s_shaderNames)))
        {
            m_shaderId = static_cast<ShaderId>(currentShader);
        }
        ImGui::ColorEdit4("Master Tint", &m_color.x);


        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Sub-Mesh material slots
        ImGui::TextDisabled("MATERIALS (%zu)", m_model ? m_model->GetMaterials().size() : 0);

        if (m_model)
        {
            auto& materials{ m_model->GetMaterials() };
            for (size_t i = 0; i < materials.size(); ++i)
            {
                // Use ImGui::PushID to ensure sliders/buttons in different materials don't conflict
                ImGui::PushID(static_cast<int>(i));

                // Format slot name: "Slot 0: MaterialName"
                char headerName[64];
                snprintf(headerName, sizeof(headerName), "Slot %zu: %s", i, materials[i].name.c_str());

                if (ImGui::TreeNode(headerName))
                {
                    // Draw the standalone Material UI 
                    materials[i].DrawInspector();
                    ImGui::TreePop();
                }

                ImGui::PopID();
            }
        }
        else
        {
            ImGui::TextColored(ImVec4{ 1.0f, 0.2f, 0.2f, 1.0f }, "Warning: No Model Attached!");
        }
    }
}

// Automatically registers MeshComponent before main() runs
REGISTER_COMPONENT(MeshComponent)