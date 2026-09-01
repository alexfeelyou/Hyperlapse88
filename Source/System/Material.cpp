#include <imgui.h>
#include "System/GpuResourceUtils.h"
#include "System/Graphics.h"
#include "Material.h"

// Lean Windows include just for the file dialog
#define WIN32_LEAN_AND_MEAN
#include <commdlg.h>
#include <windows.h>

namespace
{
    [[nodiscard]] std::string OpenTextureDialog() noexcept
    {
        char filename[MAX_PATH]{ "" };
        OPENFILENAMEA ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = nullptr;
        // Filter for common image formats
        ofn.lpstrFilter = "Images (*.png;*.jpg;*.tga)\0*.png;*.jpg;*.tga\0All Files (*.*)\0*.*\0";
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

void Material::DrawInspector() noexcept
{
    // Shader Selection Dropdown
    static constexpr const char* const s_shaderNames[]{ "Basic", "Lambert", "Phong" };
    ImGui::Combo("Shader", &shaderId, s_shaderNames, IM_ARRAYSIZE(s_shaderNames));

    // Alpha mode
    // Allows the user to fix broken GLB exports
    ImGui::Spacing();
    static constexpr const char* const s_alphaModes[]{ "Opaque", "Mask", "Blend" };
    int currentAlphaMode{ static_cast<int>(alphaMode) };
    if (ImGui::Combo("Alpha Mode", &currentAlphaMode, s_alphaModes, IM_ARRAYSIZE(s_alphaModes)))
    {
        alphaMode = static_cast<AlphaMode>(currentAlphaMode);
    }

    if (alphaMode == AlphaMode::Mask)
    {
        ImGui::SliderFloat("Alpha Cutoff", &alphaCutoff, 0.0f, 1.0f);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("ALBEDO / DIFFUSE");

    const ImVec2 texSize{ 64.0f, 64.0f };

	// Albedo Texture Thumbnail
    if (baseMap)
    {
        ImGui::Image(reinterpret_cast<ImTextureID>(baseMap.Get()), texSize);
        ImGui::SameLine();
    }

    ImGui::BeginGroup();
    ImGui::TextColored(baseMap ? ImVec4{ 0.2f, 0.9f, 0.2f, 1.0f } : ImVec4{ 0.9f, 0.2f, 0.2f, 1.0f },
        baseMap ? "Texture: Attached" : "Texture: None");

    if (ImGui::Button("Browse Albedo..."))
    {
        const std::string newPath{ OpenTextureDialog() };
        if (!newPath.empty())
        {
            auto* device{ Graphics::Instance().GetDevice() };

            // Keep the old texture alive in memory until the frame finishes rendering
            static Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> s_garbageBin;
            s_garbageBin = baseMap;

            GpuResourceUtils::LoadTexture(device, newPath.c_str(), baseMap.ReleaseAndGetAddressOf());
            baseTextureFileName = newPath;
        }
    }
    ImGui::EndGroup();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("SURFACE PROPERTIES");

	// Normal Map Thumbnail
    if (normalMap)
    {
        ImGui::Image(reinterpret_cast<ImTextureID>(normalMap.Get()), texSize);
        ImGui::SameLine();
    }

    ImGui::BeginGroup();
    ImGui::TextColored(normalMap ? ImVec4{ 0.2f, 0.9f, 0.2f, 1.0f } : ImVec4{ 0.9f, 0.2f, 0.2f, 1.0f },
        normalMap ? "Normal Map: Attached" : "Normal Map: None");

    if (ImGui::Button("Browse Normal..."))
    {
        const std::string newPath{ OpenTextureDialog() };
        if (!newPath.empty())
        {
            auto* device{ Graphics::Instance().GetDevice() };

            // Keep the old SRV alive until the next texture swap
            static Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> s_normalGarbageBin{};
            s_normalGarbageBin = normalMap;

            GpuResourceUtils::LoadTexture(device, newPath.c_str(), normalMap.ReleaseAndGetAddressOf());
            normalTextureFileName = newPath;
        }
    }
    ImGui::EndGroup();

    ImGui::Spacing();
    ImGui::ColorEdit3("Emissive", &emissiveColor.x);
    ImGui::SliderFloat("Roughness", &roughness, 0.0f, 1.0f);
    ImGui::SliderFloat("Metalness", &metalness, 0.0f, 1.0f);
}