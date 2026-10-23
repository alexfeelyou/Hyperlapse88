#include <imgui.h>
#include "System/AssetManager.h"
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

Material Material::Clone() const noexcept
{
    Material mat{};
    mat.name = name;
    mat.shaderId = shaderId;
    mat.alphaMode = alphaMode;
    mat.alphaCutoff = alphaCutoff;
    mat.uvProjectionMode = uvProjectionMode;
    mat.uvTiling = uvTiling;
    mat.uvOffset = uvOffset;
    mat.baseColor = baseColor;
    mat.emissiveColor = emissiveColor;
    mat.metalness = metalness;
    mat.roughness = roughness;
    mat.occlusionStrength = occlusionStrength;
    mat.subsurfaceStrength = subsurfaceStrength;
    mat.isOccluder = isOccluder;
    mat.isOccludable = isOccludable;
    mat.occluderScale = occluderScale;
    mat.enableOutline = enableOutline;
    mat.outlineWidth = outlineWidth;
    mat.outlineFadeStart = outlineFadeStart;
    mat.outlineFadeEnd = outlineFadeEnd;
    mat.outlineColor = outlineColor;
    mat.baseTextureFileName = baseTextureFileName;
    mat.normalTextureFileName = normalTextureFileName;
    mat.emissiveTextureFileName = emissiveTextureFileName;
    mat.occlusionTextureFileName = occlusionTextureFileName;
    mat.metalnessRoughnessTextureFileName = metalnessRoughnessTextureFileName;
    mat.baseMap = baseMap;
    mat.normalMap = normalMap;
    mat.emissiveMap = emissiveMap;
    mat.occlusionMap = occlusionMap;
    mat.metalnessRoughnessMap = metalnessRoughnessMap;
    return mat;
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

    // UV Projection & Tiling Mode
    ImGui::Spacing();
    static constexpr const char* const s_projectionModes[]{ "Mesh UV", "Box Projection (Greybox)" };
    int currentProjMode{ static_cast<int>(uvProjectionMode) };
    if (ImGui::Combo("UV Mode", &currentProjMode, s_projectionModes, IM_ARRAYSIZE(s_projectionModes)))
    {
        uvProjectionMode = static_cast<UVProjectionMode>(currentProjMode);
    }

    if (uvProjectionMode == UVProjectionMode::BoxProjection)
    {
        ImGui::DragFloat2("Grid Tiling (1/m)", &uvTiling.x, 0.05f, 0.001f, 50.0f, "%.2f");
        ImGui::DragFloat2("UV Offset", &uvOffset.x, 0.05f, -50.0f, 50.0f, "%.2f");
        ImGui::TextDisabled("Anchored to actor; scales/multiplies with transform dimensions");
    }
    else
    {
        ImGui::DragFloat2("UV Tiling", &uvTiling.x, 0.05f, 0.001f, 50.0f, "%.2f");
        ImGui::DragFloat2("UV Offset", &uvOffset.x, 0.05f, -50.0f, 50.0f, "%.2f");
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
            if (auto srv{ Engine::System::AssetManager::Instance().GetOrLoadTexture(device, newPath) })
            {
                baseMap = std::move(srv);
                baseTextureFileName = newPath;
            }
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
                    if (auto srv{ Engine::System::AssetManager::Instance().GetOrLoadTexture(device, newPath) })
                    {
                        normalMap = std::move(srv);
                        normalTextureFileName = newPath;
                    }
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
                    if (auto srv{ Engine::System::AssetManager::Instance().GetOrLoadTexture(device, newPath) })
                    {
                        metalnessRoughnessMap = std::move(srv);
                        metalnessRoughnessTextureFileName = newPath;
                    }
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
                if (auto srv{ Engine::System::AssetManager::Instance().GetOrLoadTexture(device, newPath) })
                {
                    emissiveMap = std::move(srv);
                    emissiveTextureFileName = newPath;
                }
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

        // Subsurface / Foliage Backlighting: Visible for all lit shaders (>0)
        ImGui::SliderFloat("Subsurface / Foliage Glow", &subsurfaceStrength, 0.0f, 2.0f, "%.2f");
    }

    // OCCLUSION CULLING SETTINGS
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("OCCLUSION CULLING");
    if (alphaMode == AlphaMode::Opaque)
    {
        ImGui::Checkbox("Treat as Occluder", &isOccluder);
        if (isOccluder)
        {
            ImGui::SliderFloat("Occluder Scale", &occluderScale, 0.5f, 1.0f);
        }
    }
    else
    {
        ImGui::TextDisabled("Treat as Occluder (Opaque only)");
    }
    ImGui::Checkbox("Allow Being Occluded", &isOccludable);

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
    j["UVProjectionMode"] = static_cast<int>(uvProjectionMode);
    j["UVTiling"] = { uvTiling.x, uvTiling.y };
    j["UVOffset"] = { uvOffset.x, uvOffset.y };

    j["BaseColor"] = { baseColor.x, baseColor.y, baseColor.z, baseColor.w };
    j["Emissive"] = { emissiveColor.x, emissiveColor.y, emissiveColor.z };
    j["Roughness"] = roughness;
    j["Metalness"] = metalness;
    j["SubsurfaceStrength"] = subsurfaceStrength;
    j["IsOccluder"] = isOccluder;
    j["IsOccludable"] = isOccludable;
    j["OccluderScale"] = occluderScale;
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
    uvProjectionMode = static_cast<UVProjectionMode>(j.value("UVProjectionMode", 0));
    if (j.contains("UVTiling"))
    {
        uvTiling = { j["UVTiling"][0], j["UVTiling"][1] };
    }
    if (j.contains("UVOffset"))
    {
        uvOffset = { j["UVOffset"][0], j["UVOffset"][1] };
    }

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
    subsurfaceStrength = j.value("SubsurfaceStrength", 0.0f);
    isOccluder = j.value("IsOccluder", false);
    isOccludable = j.value("IsOccludable", true);
    occluderScale = j.value("OccluderScale", 0.95f);

    // Restore texture paths
    const std::string newBaseTex{ j.value("TexBase", "") };
    const std::string newNormalTex{ j.value("TexNormal", "") };

    // Reload texture descriptors if saved paths differ or texture is missing
    if (!newBaseTex.empty() && (newBaseTex != baseTextureFileName || !baseMap))
    {
        auto* device{ Graphics::Instance().GetDevice() };
        if (auto srv{ Engine::System::AssetManager::Instance().GetOrLoadTexture(device, newBaseTex) })
        {
            baseMap = std::move(srv);
            baseTextureFileName = newBaseTex;
        }
    }

    if (!newNormalTex.empty() && (newNormalTex != normalTextureFileName || !normalMap))
    {
        auto* device{ Graphics::Instance().GetDevice() };
        if (auto srv{ Engine::System::AssetManager::Instance().GetOrLoadTexture(device, newNormalTex) })
        {
            normalMap = std::move(srv);
            normalTextureFileName = newNormalTex;
        }
    }

    enableOutline = j.value("EnableOutline", true);
    outlineWidth = j.value("OutlineWidth", 0.015f);
    outlineFadeStart = j.value("OutlineFadeStart", 12.0f);
    outlineFadeEnd = j.value("OutlineFadeEnd", 28.0f);
    if (j.contains("OutlineColor"))
    {
        outlineColor = { j["OutlineColor"][0], j["OutlineColor"][1], j["OutlineColor"][2], j["OutlineColor"][3] };
    }
}