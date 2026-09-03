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
    // Convert Local Windows Encoding (Shift-JIS) to JSON-safe UTF-8
    [[nodiscard]] std::string LocalToUTF8(const std::string& localStr) noexcept
    {
        if (localStr.empty()) return "";

        // Convert Local (Shift-JIS) to UTF-16 Wide String
        const int wSize{ MultiByteToWideChar(CP_ACP, 0, localStr.data(), static_cast<int>(localStr.size()), nullptr, 0) };
        std::wstring wStr(wSize, 0);
        MultiByteToWideChar(CP_ACP, 0, localStr.data(), static_cast<int>(localStr.size()), wStr.data(), wSize);

        // Convert UTF-16 to UTF-8
        const int uSize{ WideCharToMultiByte(CP_UTF8, 0, wStr.data(), static_cast<int>(wStr.size()), nullptr, 0, nullptr, nullptr) };
        std::string utf8Str(uSize, 0);
        WideCharToMultiByte(CP_UTF8, 0, wStr.data(), static_cast<int>(wStr.size()), utf8Str.data(), uSize, nullptr, nullptr);

        return utf8Str;
    }

    // Convert JSON UTF-8 back to Local Windows Encoding (Shift-JIS)
    [[nodiscard]] std::string UTF8ToLocal(const std::string& utf8Str) noexcept
    {
        if (utf8Str.empty()) return "";

        // Convert UTF-8 to UTF-16 Wide String
        const int wSize{ MultiByteToWideChar(CP_UTF8, 0, utf8Str.data(), static_cast<int>(utf8Str.size()), nullptr, 0) };
        std::wstring wStr(wSize, 0);
        MultiByteToWideChar(CP_UTF8, 0, utf8Str.data(), static_cast<int>(utf8Str.size()), wStr.data(), wSize);

        // Convert UTF-16 back to Local (Shift-JIS)
        const int lSize{ WideCharToMultiByte(CP_ACP, 0, wStr.data(), static_cast<int>(wStr.size()), nullptr, 0, nullptr, nullptr) };
        std::string localStr(lSize, 0);
        WideCharToMultiByte(CP_ACP, 0, wStr.data(), static_cast<int>(wStr.size()), localStr.data(), lSize, nullptr, nullptr);

        return localStr;
    }

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
    renderer->Draw(m_model, m_color, worldMatrix);
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

void MeshComponent::Serialize(nlohmann::json& j) const
{
    // Encode the Shift-JIS path into UTF-8 so the JSON library doesn't crash
    j["ModelPath"] = LocalToUTF8(m_modelPath);
    j["Color"] = { m_color.x, m_color.y, m_color.z, m_color.w };

    // Save all material overrides
    if (m_model)
    {
        nlohmann::json matArray = nlohmann::json::array();
        for (const auto& mat : m_model->GetMaterials())
        {
            nlohmann::json matJson{};
            mat.Serialize(matJson);
            matArray.push_back(matJson);
        }
        j["Materials"] = matArray;
    }
}

void MeshComponent::Deserialize(const nlohmann::json& j)
{
    if (j.contains("Color"))
    {
        m_color = { j["Color"][0], j["Color"][1], j["Color"][2], j["Color"][3] };
    }

    // Decode the JSON UTF-8 string back into Shift-JIS so AssetManager can find the file
    std::string path = UTF8ToLocal(j.value("ModelPath", ""));
    if (!path.empty() && path != "None")
    {
        auto* device{ Graphics::Instance().GetDevice() };
        if (auto newModel{ Engine::System::AssetManager::Instance().GetOrLoadModel(device, path) })
        {
            SetModel(std::move(newModel), path);

            // Apply loaded material overrides to the model
            if (j.contains("Materials"))
            {
                auto& materials = m_model->GetMaterials();
                const auto& matJsonArray = j["Materials"];

                for (size_t i = 0; i < materials.size() && i < matJsonArray.size(); ++i)
                {
                    materials[i].Deserialize(matJsonArray[i]);
                }
            }
        }
    }
}

// Automatically registers MeshComponent before main() runs
REGISTER_COMPONENT(MeshComponent)