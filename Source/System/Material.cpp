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
    static constexpr const char* const s_shaderNames[]{ "Basic", "Lambert", "Phong", "Pbr", "Toon" };
    ImGui::Combo("Shader", &shaderId, s_shaderNames, IM_ARRAYSIZE(s_shaderNames));

    // Alpha mode
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

    // ALBEDO / DIFFUSE (Visible on all shaders) 
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("ALBEDO / DIFFUSE");

    const ImVec2 texSize{ 64.0f, 64.0f };

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
            static Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> s_garbageBin{};
            s_garbageBin = baseMap;

            GpuResourceUtils::LoadTexture(device, newPath.c_str(), baseMap.ReleaseAndGetAddressOf());
            baseTextureFileName = newPath;
        }
    }
    ImGui::EndGroup();

    // SURFACE PROPERTIES (Conditional visibility) 
    // Hide entirely for Basic/Unlit (shaderId == 0)
    if (shaderId > 0)
    {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("SURFACE PROPERTIES");

        // Texture Maps
        // Normal Map: Visible only for Phong (2) and PBR (3)
        if (shaderId == 2 || shaderId == 3)
        {
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
                    static Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> s_normalGarbageBin{};
                    s_normalGarbageBin = normalMap;
                    GpuResourceUtils::LoadTexture(device, newPath.c_str(), normalMap.ReleaseAndGetAddressOf());
                    normalTextureFileName = newPath;
                }
            }
            ImGui::EndGroup();
            ImGui::Spacing();
        }

        // Metallic-Roughness (ORM) Map: Visible only for PBR (3)
        if (shaderId == 3)
        {
            if (metalnessRoughnessMap)
            {
                ImGui::Image(reinterpret_cast<ImTextureID>(metalnessRoughnessMap.Get()), texSize);
                ImGui::SameLine();
            }

            ImGui::BeginGroup();
            ImGui::TextColored(metalnessRoughnessMap ? ImVec4{ 0.2f, 0.9f, 0.2f, 1.0f } : ImVec4{ 0.9f, 0.2f, 0.2f, 1.0f },
                metalnessRoughnessMap ? "ORM Map: Attached" : "ORM Map: None");

            if (ImGui::Button("Browse ORM..."))
            {
                const std::string newPath{ OpenTextureDialog() };
                if (!newPath.empty())
                {
                    auto* device{ Graphics::Instance().GetDevice() };
                    static Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> s_ormGarbageBin{};
                    s_ormGarbageBin = metalnessRoughnessMap;
                    GpuResourceUtils::LoadTexture(device, newPath.c_str(), metalnessRoughnessMap.ReleaseAndGetAddressOf());
                    metalnessRoughnessTextureFileName = newPath;
                }
            }
            ImGui::EndGroup();
            ImGui::Spacing();
        }

        // Emissive Map: Visible for all lit shaders (>0)
        if (emissiveMap)
        {
            ImGui::Image(reinterpret_cast<ImTextureID>(emissiveMap.Get()), texSize);
            ImGui::SameLine();
        }

        ImGui::BeginGroup();
        ImGui::TextColored(emissiveMap ? ImVec4{ 0.2f, 0.9f, 0.2f, 1.0f } : ImVec4{ 0.9f, 0.2f, 0.2f, 1.0f },
            emissiveMap ? "Emissive Map: Attached" : "Emissive Map: None");

        if (ImGui::Button("Browse Emissive..."))
        {
            const std::string newPath{ OpenTextureDialog() };
            if (!newPath.empty())
            {
                auto* device{ Graphics::Instance().GetDevice() };
                static Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> s_emissiveGarbageBin{};
                s_emissiveGarbageBin = emissiveMap;
                GpuResourceUtils::LoadTexture(device, newPath.c_str(), emissiveMap.ReleaseAndGetAddressOf());
                emissiveTextureFileName = newPath;
            }
        }
        ImGui::EndGroup();
        ImGui::Spacing();

        // Sliders & Color
        // Roughness: Visible for Phong (2), PBR (3), Toon (4)
        if (shaderId >= 2)
        {
            ImGui::SliderFloat("Roughness", &roughness, 0.0f, 1.0f);
        }

        // Metalness: Visible only for PBR (3)
        if (shaderId == 3)
        {
            ImGui::SliderFloat("Metalness", &metalness, 0.0f, 1.0f);
        }

        // Emissive Color: Visible for Lambert (1), Phong (2), PBR (3), Toon (4)
        ImGui::ColorEdit3("Emissive Color", &emissiveColor.x);
    }

    // TOON OUTLINE (Conditional visibility) 
    // Visible only for Toon (4)
    if (shaderId == 4)
    {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("TOON OUTLINE");
        ImGui::Checkbox("Enable Outline", &enableOutline);

        if (enableOutline)
        {
            ImGui::SliderFloat("Width", &outlineWidth, 0.0f, 0.05f);
            ImGui::SliderFloat("Fade Start", &outlineFadeStart, 1.0f, 50.0f);
            ImGui::SliderFloat("Fade End", &outlineFadeEnd, 5.0f, 100.0f);
            ImGui::ColorEdit3("Color", &outlineColor.x);
        }
    }
}

void Material::Serialize(nlohmann::json& j) const
{
    j["Name"] = name;
    j["ShaderId"] = shaderId;
    j["AlphaMode"] = static_cast<int>(alphaMode);
    j["AlphaCutoff"] = alphaCutoff;

    j["BaseColor"] = { baseColor.x, baseColor.y, baseColor.z, baseColor.w };
    j["Emissive"] = { emissiveColor.x, emissiveColor.y, emissiveColor.z };
    j["Roughness"] = roughness;
    j["Metalness"] = metalness;

    j["TexBase"] = baseTextureFileName;
    j["TexNormal"] = normalTextureFileName;

    j["EnableOutline"] = enableOutline;
    j["OutlineWidth"] = outlineWidth;
    j["OutlineFadeStart"] = outlineFadeStart;
    j["OutlineFadeEnd"] = outlineFadeEnd;
    j["OutlineColor"] = { outlineColor.x, outlineColor.y, outlineColor.z, outlineColor.w };
}

void Material::Deserialize(const nlohmann::json& j)
{
    name = j.value("Name", "DefaultMaterial");
    shaderId = j.value("ShaderId", 2);
    alphaMode = static_cast<AlphaMode>(j.value("AlphaMode", 0));
    alphaCutoff = j.value("AlphaCutoff", 0.5f);

    if (j.contains("BaseColor"))
    {
        baseColor = { j["BaseColor"][0], j["BaseColor"][1], j["BaseColor"][2], j["BaseColor"][3] };
    }
    if (j.contains("Emissive"))
    {
        emissiveColor = { j["Emissive"][0], j["Emissive"][1], j["Emissive"][2] };
    }

    roughness = j.value("Roughness", 0.5f);
    metalness = j.value("Metalness", 0.0f);

    enableOutline = j.value("EnableOutline", true);
    outlineWidth = j.value("OutlineWidth", 0.015f);
    outlineFadeStart = j.value("OutlineFadeStart", 12.0f);
    outlineFadeEnd = j.value("OutlineFadeEnd", 28.0f);
    if (j.contains("OutlineColor"))
    {
        outlineColor = { j["OutlineColor"][0], j["OutlineColor"][1], j["OutlineColor"][2], j["OutlineColor"][3] };
    }
}