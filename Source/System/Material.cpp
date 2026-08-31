#include <imgui.h>
#include "Material.h"

void Material::DrawInspector() noexcept
{
    // Shader Selection Dropdown
    static constexpr const char* const s_shaderNames[]{ "Basic", "Lambert", "Phong" };
    ImGui::Combo("Shader", &shaderId, s_shaderNames, IM_ARRAYSIZE(s_shaderNames));

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("ALBEDO / DIFFUSE");

    // Base Color Tint
    ImGui::ColorEdit4("Color Tint", &baseColor.x);

    // Albedo Texture Status
    if (baseMap)
    {
        ImGui::TextColored(ImVec4{ 0.2f, 0.9f, 0.2f, 1.0f }, "Texture: Attached");
    }
    else
    {
        ImGui::TextColored(ImVec4{ 0.9f, 0.2f, 0.2f, 1.0f }, "Texture: None");
    }

    // Dummy button 
    if (ImGui::Button("Browse Albedo..."))
    {
        // Placeholder for Phase 4
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("SURFACE PROPERTIES");

    // Normal Map Status
    if (normalMap)
    {
        ImGui::TextColored(ImVec4{ 0.2f, 0.9f, 0.2f, 1.0f }, "Normal Map: Attached");
    }
    else
    {
        ImGui::TextColored(ImVec4{ 0.9f, 0.2f, 0.2f, 1.0f }, "Normal Map: None");
    }

    if (ImGui::Button("Browse Normal..."))
    {
        // Placeholder for Phase 4
    }

    ImGui::Spacing();
    ImGui::ColorEdit3("Emissive", &emissiveColor.x);

    // PBR parameters remain visible for future-proofing, even if shaders don't use them yet
    ImGui::SliderFloat("Roughness", &roughness, 0.0f, 1.0f);
    ImGui::SliderFloat("Metalness", &metalness, 0.0f, 1.0f);
}