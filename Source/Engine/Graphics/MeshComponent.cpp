#include <imgui.h>
#include "System/Model.h"
#include "MeshComponent.h"

MeshComponent::MeshComponent(std::shared_ptr<Model> model) noexcept
    : m_model{ std::move(model) }
{}

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
    // Ensure the array exactly matches the ShaderId enum declaration order
    static constexpr const char* const s_shaderNames[]{ "Basic", "Lambert", "Phong" };

    int currentShader{ static_cast<int>(m_shaderId) };

    // Shader Selection Dropdown
    if (ImGui::Combo("Shader", &currentShader, s_shaderNames, IM_ARRAYSIZE(s_shaderNames)))
    {
        m_shaderId = static_cast<ShaderId>(currentShader);
    }

    // Material Tint
    ImGui::ColorEdit4("Color Tint", &m_color.x);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Model Diagnostics (Read-Only)
    ImGui::TextDisabled("Mesh Diagnostics");
    if (m_model)
    {
        ImGui::Text("Sub-Meshes: %zu", m_model->GetMeshes().size());
        ImGui::Text("Materials: %zu", m_model->GetMaterials().size());
    }
    else
    {
        ImGui::TextColored(ImVec4{ 1.0f, 0.2f, 0.2f, 1.0f }, "Warning: No Model Attached!");
    }
}