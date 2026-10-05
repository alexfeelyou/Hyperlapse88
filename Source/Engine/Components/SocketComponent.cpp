#include <algorithm>
#include <cmath>
#include <imgui.h>
#include "System/Model.h"
#include "ComponentRegistry.h"
#include "GameObject.h"
#include "SocketComponent.h"

void SocketComponent::OnAttach(GameObject* owner) noexcept
{
    IComponent::OnAttach(owner);

    // A socket is conceptually a child of the skeletal mesh.
    // Auto-resolve the AnimationComponent by looking at the parent GameObject.
    if (m_owner && m_owner->GetParent())
    {
        m_targetAnim = m_owner->GetParent()->GetComponent<AnimationComponent>();
        ResolveBoneIndex();
    }
}

void SocketComponent::ResolveBoneIndex() noexcept
{
    if (!m_targetAnim || m_targetBoneName.empty())
    {
        m_targetBoneIndex = -1;
        return;
    }

    if (auto model{ m_targetAnim->GetModel() })
    {
        m_targetBoneIndex = model->GetNodeIndex(m_targetBoneName.c_str());
    }
}

void SocketComponent::Update(float dt)
{
    // Fast-fail if the hierarchy isn't valid or the model hasn't loaded yet
    if (!m_targetAnim || m_targetBoneIndex < 0 || !m_owner) return;

    const auto& globals{ m_targetAnim->GetCurrentNodeGlobals() };
    if (static_cast<std::size_t>(m_targetBoneIndex) >= globals.size()) return;

    // Build the Socket's local offset matrix
    const DirectX::XMMATRIX matScale{ DirectX::XMMatrixScaling(m_localScale.x, m_localScale.y, m_localScale.z) };
    const DirectX::XMMATRIX matRot{ DirectX::XMMatrixRotationRollPitchYaw(
        DirectX::XMConvertToRadians(m_localRotation.x),
        DirectX::XMConvertToRadians(m_localRotation.y),
        DirectX::XMConvertToRadians(m_localRotation.z)
    ) };
    const DirectX::XMMATRIX matTrans{ DirectX::XMMatrixTranslation(m_localPosition.x, m_localPosition.y, m_localPosition.z) };

    const DirectX::XMMATRIX matSocketLocal{ matScale * matRot * matTrans };

    // Multiply by the evaluated bone matrix (which is already in the parent's local space)
    const DirectX::XMMATRIX matBoneGlobal{ DirectX::XMLoadFloat4x4(&globals[m_targetBoneIndex]) };
    const DirectX::XMMATRIX matFinalLocal{ matSocketLocal * matBoneGlobal };

    // Decompose back into the Transform component to feed the Engine's downstream renderer
    DirectX::XMVECTOR vScale, vRotQuat, vTrans;
    if (DirectX::XMMatrixDecompose(&vScale, &vRotQuat, &vTrans, matFinalLocal))
    {
        DirectX::XMStoreFloat3(&m_owner->transform.position, vTrans);
        DirectX::XMStoreFloat3(&m_owner->transform.scale, vScale);

        // Convert the extracted quaternion securely back to Euler angles
        const DirectX::XMFLOAT4X4 mRot = [&]() {
            DirectX::XMFLOAT4X4 temp;
            DirectX::XMStoreFloat4x4(&temp, DirectX::XMMatrixRotationQuaternion(vRotQuat));
            return temp;
            }();

        float pitch{ std::asin(std::clamp(-mRot._32, -1.0f, 1.0f)) };
        float yaw, roll;
        if (std::cos(pitch) > 0.0001f)
        {
            yaw = std::atan2(mRot._31, mRot._33);
            roll = std::atan2(mRot._12, mRot._22);
        }
        else
        {
            yaw = std::atan2(-mRot._13, mRot._11);
            roll = 0.0f;
        }

        m_owner->transform.rotation = {
            DirectX::XMConvertToDegrees(pitch),
            DirectX::XMConvertToDegrees(yaw),
            DirectX::XMConvertToDegrees(roll)
        };
    }
}

void SocketComponent::DrawInspector()
{
    ImGui::TextDisabled("Skeletal Attachment Motor");
    ImGui::Separator();

    if (!m_targetAnim && m_owner && m_owner->GetParent())
    {
        m_targetAnim = m_owner->GetParent()->GetComponent<AnimationComponent>();
        ResolveBoneIndex();
    }

    if (!m_targetAnim)
    {
        ImGui::TextColored(ImVec4{ 1.0f, 0.2f, 0.2f, 1.0f }, "Error: Parent lacks an AnimationComponent!");
        return;
    }

    auto model{ m_targetAnim->GetModel() };
    if (!model)
    {
        ImGui::TextColored(ImVec4{ 1.0f, 0.2f, 0.2f, 1.0f }, "Error: Parent AnimationComponent lacks a 3D Model!");
        return;
    }

    // Dynamic Bone Resolution Dropdown
    const auto& nodes{ model->GetNodes() };
    const std::string preview{ m_targetBoneName.empty() ? "Select Bone..." : m_targetBoneName };

    ImGui::PushItemWidth(-1.0f);
    if (ImGui::BeginCombo("##BoneSelect", preview.c_str()))
    {
        for (std::size_t i{ 0 }; i < nodes.size(); ++i)
        {
            const bool isSelected{ m_targetBoneIndex == static_cast<int>(i) };
            if (ImGui::Selectable(nodes[i].name.c_str(), isSelected))
            {
                m_targetBoneIndex = static_cast<int>(i);
                m_targetBoneName = nodes[i].name;
            }
            if (isSelected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("SOCKET OFFSET (Relative to Bone)");

    // Allow fine-tuning the weapon position dynamically while animations play
    ImGui::DragFloat3("Socket Position", &m_localPosition.x, 0.01f);
    ImGui::DragFloat3("Socket Rotation", &m_localRotation.x, 1.0f);
    ImGui::DragFloat3("Socket Scale", &m_localScale.x, 0.05f);
}

void SocketComponent::Serialize(nlohmann::json& outJson) const
{
    outJson["BoneName"] = m_targetBoneName;
    outJson["LocalPosX"] = m_localPosition.x;
    outJson["LocalPosY"] = m_localPosition.y;
    outJson["LocalPosZ"] = m_localPosition.z;
    outJson["LocalRotX"] = m_localRotation.x;
    outJson["LocalRotY"] = m_localRotation.y;
    outJson["LocalRotZ"] = m_localRotation.z;
    outJson["LocalSclX"] = m_localScale.x;
    outJson["LocalSclY"] = m_localScale.y;
    outJson["LocalSclZ"] = m_localScale.z;
}

void SocketComponent::Deserialize(const nlohmann::json& inJson)
{
    m_targetBoneName = inJson.value("BoneName", "");

    m_localPosition = {
        inJson.value("LocalPosX", 0.0f),
        inJson.value("LocalPosY", 0.0f),
        inJson.value("LocalPosZ", 0.0f)
    };

    m_localRotation = {
        inJson.value("LocalRotX", 0.0f),
        inJson.value("LocalRotY", 0.0f),
        inJson.value("LocalRotZ", 0.0f)
    };

    m_localScale = {
        inJson.value("LocalSclX", 1.0f),
        inJson.value("LocalSclY", 1.0f),
        inJson.value("LocalSclZ", 1.0f)
    };

    // Bone index will be late-resolved during OnAttach or first Update frame
    m_targetBoneIndex = -1;
}

REGISTER_COMPONENT(SocketComponent)