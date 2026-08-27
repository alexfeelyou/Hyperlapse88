#include <algorithm>
#include <imgui.h> 
#include "GameObject.h"

// Use the member-initializer list to set up variables before the constructor body executes
GameObject::GameObject(std::string_view name) noexcept
    : m_name{ name }
{
    // The transform must know which GameObject it belongs to
    transform.owner = this;
}

void GameObject::Update(float dt)
{
    if (!m_isActive) return;

    // Update all attached components
    for (const auto& component : m_components)
    {
        component->Update(dt);
    }

    // Recursively update all children in the hierarchy
    for (const auto& child : m_children)
    {
        child->Update(dt);
    }
}

void GameObject::DrawInspector()
{
    // Draw the basic GameObject properties (Name & Active state)
    static char s_nameBuffer[128];
    strncpy_s(s_nameBuffer, sizeof(s_nameBuffer), m_name.c_str(), _TRUNCATE);

    if (ImGui::InputText("Name", s_nameBuffer, sizeof(s_nameBuffer)))
    {
        m_name = s_nameBuffer;
    }

    ImGui::Checkbox("Active", &m_isActive);

    ImGui::Separator();

    // Draw the hardcoded Transform UI
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::DragFloat3("Position", &transform.position.x, 0.1f);
        ImGui::DragFloat3("Rotation", &transform.rotation.x, 1.0f);
        ImGui::DragFloat3("Scale", &transform.scale.x, 0.05f);
    }

    ImGui::Separator();

    // Delegate to each Component to draw its own specialized UI
    for (const auto& component : m_components)
    {
        // Use the component's TypeName as the CollapsingHeader title
        if (ImGui::CollapsingHeader(component->GetTypeName(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            component->DrawInspector();
        }
    }
}

void GameObject::AddChild(std::unique_ptr<GameObject> child)
{
    if (!child) return;

    child->transform.parent = &this->transform;
    child->m_parent = this;
    m_children.push_back(std::move(child));
}

void GameObject::SetParent(GameObject* newParent) noexcept
{
    // Prevent setting self as parent
    if (newParent == this) return;

    // Store the old parent to orchestrate the ownership transfer
    GameObject* oldParent{ m_parent };

    if (oldParent == newParent) return;

    m_parent = newParent;
    transform.parent = newParent ? &newParent->transform : nullptr;
}