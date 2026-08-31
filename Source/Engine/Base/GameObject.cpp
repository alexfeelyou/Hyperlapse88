#include <algorithm>
#include <imgui.h> 
#include "Character.h"
#include "CharacterMovement.h"
#include "ComponentRegistry.h"
#include "GameObject.h"
#include "LegacyCharacterComponent.h"

// Use the member-initializer list to set up variables before the constructor body executes
GameObject::GameObject(std::string_view name) noexcept
    : m_name{ name }
{
    // The transform must know which GameObject it belongs to
    transform.owner = this;
}

void GameObject::Update(float dt)
{
    // Fast fail if inactive or pending destruction
    if (!m_isActive || m_isDestroyed) return;

    // Update all attached components
    for (const auto& component : m_components)
    {
        component->Update(dt);
    }

    // Moves elements marked for destruction to the back, then truncates the vector
    const auto eraseBegin = std::remove_if(
        m_children.begin(),
        m_children.end(),
        [](const std::unique_ptr<GameObject>& child) noexcept
        {
            return !child || child->IsDestroyed();
        }
    );

    if (eraseBegin != m_children.end())
    {
        m_children.erase(eraseBegin, m_children.end());
    }

    // Recursively tick surviving children
    for (const auto& child : m_children)
    {
        child->Update(dt);
    }
}

void GameObject::Render(ModelRenderer* renderer)
{
    // Active check: If deactivated, stop rendering self and all children
    if (!m_isActive) return;

    // Render all visual components attached to this node
    for (const auto& component : m_components)
    {
        component->Render(renderer);
    }

    // Recurse down the hierarchy
    for (const auto& child : m_children)
    {
        child->Render(renderer);
    }
}

void GameObject::DrawInspector()
{
    // Unity-style Header: [X] Checkbox  [ Name Input ]
    ImGui::Checkbox("##Active", &m_isActive);
    ImGui::SameLine();

    static char s_nameBuffer[128];
    strncpy_s(s_nameBuffer, sizeof(s_nameBuffer), m_name.c_str(), _TRUNCATE);

    // Push item width to fill the remaining horizontal space
    ImGui::PushItemWidth(-1.0f);
    if (ImGui::InputText("##Name", s_nameBuffer, sizeof(s_nameBuffer)))
    {
        m_name = s_nameBuffer;
    }
    ImGui::PopItemWidth();

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
        if (ImGui::CollapsingHeader(component->GetTypeName(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            component->DrawInspector();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // "+ Add Component" Button
    constexpr float buttonWidth{ 200.0f };
    const float availWidth{ ImGui::GetContentRegionAvail().x };
    ImGui::SetCursorPosX((availWidth * 0.5f) - (buttonWidth * 0.5f));

    if (ImGui::Button("+ Add Component", ImVec2{ buttonWidth, 26.0f }))
    {
        ImGui::OpenPopup("AddComponentPopup");
    }

    // Render the dropdown list populated dynamically from the Registry
    if (ImGui::BeginPopup("AddComponentPopup"))
    {
        ImGui::TextDisabled("Available Components");
        ImGui::Separator();

        for (const auto& compName : ComponentRegistry::GetAvailableNames())
        {
            if (ImGui::MenuItem(compName.c_str()))
            {
                // Instantiate and attach the selected component
                if (auto newComp{ ComponentRegistry::Create(compName) })
                {
                    AddComponent(std::move(newComp));
                }
            }
        }
        ImGui::EndPopup();
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

void GameObject::BroadcastTransformUpdate() noexcept
{
    for (const auto& comp : m_components)
    {
        if (auto* charComp{ dynamic_cast<LegacyCharacterComponent*>(comp.get()) })
        {
            if (Character * character{ charComp->GetCharacter() })
            {
                // Route position and rotation safely through the movement container
                if (CharacterMovement * move{ character->GetMovement() })
                {
                    move->SetPosition(m_position);
                    move->SetRotation(m_rotation);
                }

                character->scale = m_scale;
            }
        }
    }
}