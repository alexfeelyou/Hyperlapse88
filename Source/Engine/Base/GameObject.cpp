#include <algorithm>
#include <imgui.h> 
#include "ComponentRegistry.h"
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

void GameObject::LateUpdate(float dt)
{
    // Fast fail if inactive or pending destruction
    if (!m_isActive || m_isDestroyed) return;

    // Late-update all attached components
    for (const auto& component : m_components)
    {
        component->LateUpdate(dt);
    }

    // Recursively late-update surviving children
    for (const auto& child : m_children)
    {
        if (child)
        {
            child->LateUpdate(dt);
        }
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

void GameObject::DrawGizmo(const GizmoContext& ctx) noexcept
{
    if (!m_isActive) return;
    for (auto& comp : m_components) comp->DrawGizmo(ctx);
    for (auto& child : m_children) child->DrawGizmo(ctx);
}

void GameObject::DrawInspector()
{
    // Capture the previous active state before ImGui modifies it
    const bool prevActive = m_isActive;

    // Unity-style Header: [X] Checkbox  [ Name Input ]
    ImGui::Checkbox("##Active", &m_isActive);

    // Dispatch lifecycle events the exact frame the box is clicked
    if (prevActive != m_isActive)
    {
        for (const auto& comp : m_components)
        {
            if (m_isActive) comp->OnEnable();
            else comp->OnDisable();
        }
    }

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
        if (ImGui::CollapsingHeader(component->GetTypeName()))
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

    if (ImGui::Button("+ Add Component", ImVec2{ buttonWidth, 30.0f }))
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

[[nodiscard]] std::unique_ptr<GameObject> GameObject::RemoveChild(GameObject* child) noexcept
{
    if (!child) return nullptr;

    const auto it = std::find_if(m_children.begin(), m_children.end(),
        [child](const std::unique_ptr<GameObject>& ptr) noexcept {
            return ptr.get() == child;
        });

    if (it == m_children.end()) return nullptr;

    std::unique_ptr<GameObject> extracted{ std::move(*it) };
    m_children.erase(it);

    extracted->m_parent = nullptr;
    extracted->transform.parent = nullptr;

    return extracted;
}

void GameObject::SetParent(GameObject* newParent) noexcept
{
    if (!newParent || newParent == this || m_parent == newParent) return;

    // Guard against cyclic parenting
    for (const GameObject* curr = newParent; curr != nullptr; curr = curr->m_parent)
    {
        if (curr == this) return;
    }

    // Preserve absolute world transform across hierarchy changes
    const DirectX::XMFLOAT4X4 worldMatrix{ transform.GetWorldMatrix() };

    GameObject* oldParent{ m_parent };
    if (!oldParent) return;

    // Locate and extract unique ownership from the prior parent
    auto it = std::find_if(oldParent->m_children.begin(), oldParent->m_children.end(),
        [this](const std::unique_ptr<GameObject>& ptr) noexcept {
            return ptr.get() == this;
        });

    if (it == oldParent->m_children.end()) return;

    std::unique_ptr<GameObject> selfOwned{ std::move(*it) };
    oldParent->m_children.erase(it);

    m_parent = newParent;
    transform.parent = &newParent->transform;

    // Recompute local coordinates relative to the new parent's world matrix
    const DirectX::XMFLOAT4X4 parentWorldF{ newParent->transform.GetWorldMatrix() };
    const DirectX::XMMATRIX pWorld{ DirectX::XMLoadFloat4x4(&parentWorldF) };
    DirectX::XMVECTOR det;
    const DirectX::XMMATRIX invPWorld{ DirectX::XMMatrixInverse(&det, pWorld) };
    const DirectX::XMMATRIX childWorld{ DirectX::XMLoadFloat4x4(&worldMatrix) };
    const DirectX::XMMATRIX newLocal{ DirectX::XMMatrixMultiply(childWorld, invPWorld) };

    DirectX::XMVECTOR vScale, vRotQuat, vTrans;
    if (DirectX::XMMatrixDecompose(&vScale, &vRotQuat, &vTrans, newLocal))
    {
        DirectX::XMStoreFloat3(&transform.position, vTrans);
        DirectX::XMStoreFloat3(&transform.scale, vScale);

        DirectX::XMFLOAT4X4 mRot{};
        DirectX::XMStoreFloat4x4(&mRot, DirectX::XMMatrixRotationQuaternion(vRotQuat));

        const float pitch{ asinf(std::clamp(-mRot._32, -1.0f, 1.0f)) };
        float yaw{ 0.0f };
        float roll{ 0.0f };
        if (cosf(pitch) > 0.0001f)
        {
            yaw = atan2f(mRot._31, mRot._33);
            roll = atan2f(mRot._12, mRot._22);
        }
        else
        {
            yaw = atan2f(-mRot._13, mRot._11);
            roll = 0.0f;
        }

        transform.rotation = {
            DirectX::XMConvertToDegrees(pitch),
            DirectX::XMConvertToDegrees(yaw),
            DirectX::XMConvertToDegrees(roll)
        };
    }

    newParent->m_children.push_back(std::move(selfOwned));
}

[[nodiscard]] std::unique_ptr<GameObject> GameObject::Clone() const
{
    // Derive base stem and format Maya/Blender/Unity-style clean numbering
    std::string baseStem{ m_name };
    size_t lastUnderscore{ baseStem.rfind('_') };
    bool hasNumericSuffix{ false };

    if (lastUnderscore != std::string::npos && lastUnderscore + 1 < baseStem.length())
    {
        hasNumericSuffix = true;
        for (size_t i{ lastUnderscore + 1 }; i < baseStem.length(); ++i)
        {
            if (!std::isdigit(static_cast<unsigned char>(baseStem[i])))
            {
                hasNumericSuffix = false;
                break;
            }
        }
    }

    if (hasNumericSuffix)
    {
        baseStem = baseStem.substr(0, lastUnderscore);
    }

    // Inspect siblings under the parent to find the highest existing index
    int maxIndex{ 0 };
    const GameObject* parentNode{ m_parent };
    if (parentNode)
    {
        for (const auto& sibling : parentNode->GetChildren())
        {
            if (!sibling) continue;
            const std::string& sibName{ sibling->GetName() };
            if (sibName.rfind(baseStem, 0) == 0)
            {
                if (sibName.length() > baseStem.length() && sibName[baseStem.length()] == '_')
                {
                    const std::string suffixStr{ sibName.substr(baseStem.length() + 1) };
                    char* endPtr{ nullptr };
                    long val{ std::strtol(suffixStr.c_str(), &endPtr, 10) };
                    if (endPtr != suffixStr.c_str() && *endPtr == '\0')
                    {
                        if (val > maxIndex) maxIndex = static_cast<int>(val);
                    }
                }
                else if (sibName == baseStem && maxIndex == 0)
                {
                    maxIndex = 1;
                }
            }
        }
    }

    char formattedName[256];
    snprintf(formattedName, sizeof(formattedName), "%s_%02d", baseStem.c_str(), maxIndex + 1);

    auto clone{ std::make_unique<GameObject>(formattedName) };
    clone->m_isActive = m_isActive;
    clone->transform.position = transform.position;
    clone->transform.rotation = transform.rotation;
    clone->transform.scale = transform.scale;

    // Deep-copy components via serialization/registry instantiation
    for (const auto& comp : m_components)
    {
        if (!comp) continue;

        if (auto newComp{ ComponentRegistry::Create(comp->GetTypeName()) })
        {
            nlohmann::json compData{};
            comp->Serialize(compData);
            newComp->Deserialize(compData);
            clone->AddComponent(std::move(newComp));
        }
    }

    // Recursively duplicate child hierarchies
    for (const auto& child : m_children)
    {
        if (child && !child->IsDestroyed())
        {
            clone->AddChild(child->Clone());
        }
    }

    return clone;
}