#include <algorithm>
#include <cmath>
#include <imgui.h>
#include "System/Model.h"
#include "ComponentRegistry.h"
#include "EditorManager.h"
#include "GameObject.h"
#include "SocketComponent.h"

namespace { static int s_editorPreviewProfile = 0; }

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
    if (!m_targetAnim) return;

    if (auto model{ m_targetAnim->GetModel() })
    {
        m_targetBoneIndex[0] = m_targetBoneName[0].empty() ? -1 : model->GetNodeIndex(m_targetBoneName[0].c_str());
        m_targetBoneIndex[1] = m_targetBoneName[1].empty() ? -1 : model->GetNodeIndex(m_targetBoneName[1].c_str());
    }
}

[[nodiscard]] std::size_t SocketComponent::GetActiveProfileIndex() const noexcept
{
    // Highest Priority: Live Blackboard Contract.
    // This allows the Combat Stance UI checkbox to instantly draw the weapon in the Editor.
    if (m_targetAnim)
    {
        if (auto* bb = m_targetAnim->GetBlackboard())
        {
            if (bb->getFlag(Engine::Animation::AnimFlag::is_combat_active)) return 1;
        }
    }

    // Editor Fallbacks (If the flag is false, but we are previewing an attack)
    if (EditorManager::Instance().GetEditorMode() == EditorMode::Edit)
    {
        // AUTO-COMBAT PREVIEW: Since the C++ State Machine doesn't run in the Graph Editor, 
        // we dynamically check the semantic slot to ensure combat anims get the combat grip.
        if (m_targetAnim && m_targetAnim->IsPreviewing())
        {
            const std::size_t stateIdx = m_targetAnim->GetCurrentStateIndex();
            const auto& states = m_targetAnim->GetStates();
            if (stateIdx < states.size())
            {
                const auto slot = states[stateIdx].slot;
                if (slot >= Engine::Animation::AnimSlot::Attack_Primary && slot <= Engine::Animation::AnimSlot::HitReact)
                {
                    return 1;
                }
            }
        }
        return static_cast<std::size_t>(s_editorPreviewProfile);
    }

    return 0; // Default to Holster
}

void SocketComponent::Update(float dt)
{
    // Fast-fail if the hierarchy isn't valid or the model hasn't loaded yet
    if (!m_targetAnim || !m_owner) return;

    const std::size_t activeProfile{ GetActiveProfileIndex() };
    const int targetBone{ m_targetBoneIndex[activeProfile] };

    if (targetBone < 0) return;

    const auto& globals{ m_targetAnim->GetCurrentNodeGlobals() };
    if (static_cast<std::size_t>(targetBone) >= globals.size()) return;

    // Sample dynamic attack grip override if in Combat Stance (Profile 1)
    DirectX::XMFLOAT3 targetDeltaPos{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 targetDeltaRot{ 0.0f, 0.0f, 0.0f };

    if (activeProfile == 1)
    {
        const AnimNode* overrideNode = m_targetAnim->GetActiveAnimNode();

        // EDITOR QOL: Immediate Mode GUI Slider Tracking
        // When paused in the editor, AnimationComponent resets the active node to Idle (0,0).
        // We use zero-allocation data diffing to detect which node's sliders are actively being dragged 
        // in the UI, and seamlessly latch onto that node for live 3D viewport previewing.
        if (EditorManager::Instance().GetEditorMode() == EditorMode::Edit)
        {
            static std::vector<DirectX::XMFLOAT3> s_prevPos;
            static std::vector<DirectX::XMFLOAT3> s_prevRot;
            static const AnimNode* s_latchedNode = nullptr;

            std::size_t flatIdx = 0;
            const auto& states = m_targetAnim->GetStates();

            for (const auto& state : states)
            {
                for (const auto& node : state.nodes)
                {
                    if (node.hasGripOverride)
                    {
                        if (flatIdx >= s_prevPos.size())
                        {
                            s_prevPos.push_back(node.gripPosition);
                            s_prevRot.push_back(node.gripRotation);
                        }
                        else if (s_prevPos[flatIdx].x != node.gripPosition.x || s_prevPos[flatIdx].y != node.gripPosition.y || s_prevPos[flatIdx].z != node.gripPosition.z ||
                            s_prevRot[flatIdx].x != node.gripRotation.x || s_prevRot[flatIdx].y != node.gripRotation.y || s_prevRot[flatIdx].z != node.gripRotation.z)
                        {
                            // Slider drag detected, Latch onto this node.
                            s_latchedNode = &node;
                            s_prevPos[flatIdx] = node.gripPosition;
                            s_prevRot[flatIdx] = node.gripRotation;
                        }
                        flatIdx++;
                    }
                }
            }

            if (flatIdx < s_prevPos.size())
            {
                s_prevPos.resize(flatIdx);
                s_prevRot.resize(flatIdx);
            }

            if (!m_targetAnim->IsPreviewing() || (!overrideNode || !overrideNode->hasGripOverride))
            {
                if (s_latchedNode && s_latchedNode->hasGripOverride)
                {
                    overrideNode = s_latchedNode;
                }
                else if (!s_latchedNode && flatIdx > 0)
                {
                    // Fallback to first available override if nothing latched yet
                    for (const auto& state : states) {
                        for (const auto& node : state.nodes) {
                            if (node.hasGripOverride) {
                                overrideNode = &node;
                                s_latchedNode = overrideNode;
                                goto FallbackFound;
                            }
                        }
                    }
                FallbackFound:;
                }
            }
            else if (overrideNode && overrideNode->hasGripOverride)
            {
                s_latchedNode = overrideNode; // Resync latch to active playback
            }
        }

        if (overrideNode && overrideNode->hasGripOverride)
        {
            targetDeltaPos = overrideNode->gripPosition;
            targetDeltaRot = overrideNode->gripRotation;
        }
    }

    // Converge smoothly to prevent visual snapping during attack transitions
    if (dt <= 0.0001f)
    {
        m_currentGripDeltaPos = targetDeltaPos;
        m_currentGripDeltaRot = targetDeltaRot;
    }
    else
    {
        constexpr float CONVERGENCE_RATE{ 16.0f };
        const float blendFactor{ std::clamp(dt * CONVERGENCE_RATE, 0.0f, 1.0f) };

        m_currentGripDeltaPos.x += (targetDeltaPos.x - m_currentGripDeltaPos.x) * blendFactor;
        m_currentGripDeltaPos.y += (targetDeltaPos.y - m_currentGripDeltaPos.y) * blendFactor;
        m_currentGripDeltaPos.z += (targetDeltaPos.z - m_currentGripDeltaPos.z) * blendFactor;

        m_currentGripDeltaRot.x += (targetDeltaRot.x - m_currentGripDeltaRot.x) * blendFactor;
        m_currentGripDeltaRot.y += (targetDeltaRot.y - m_currentGripDeltaRot.y) * blendFactor;
        m_currentGripDeltaRot.z += (targetDeltaRot.z - m_currentGripDeltaRot.z) * blendFactor;
    }

    const DirectX::XMFLOAT3 effectivePos{
        m_localPosition[activeProfile].x + (activeProfile == 1 ? m_currentGripDeltaPos.x : 0.0f),
        m_localPosition[activeProfile].y + (activeProfile == 1 ? m_currentGripDeltaPos.y : 0.0f),
        m_localPosition[activeProfile].z + (activeProfile == 1 ? m_currentGripDeltaPos.z : 0.0f)
    };
    const DirectX::XMFLOAT3 effectiveRot{
        m_localRotation[activeProfile].x + (activeProfile == 1 ? m_currentGripDeltaRot.x : 0.0f),
        m_localRotation[activeProfile].y + (activeProfile == 1 ? m_currentGripDeltaRot.y : 0.0f),
        m_localRotation[activeProfile].z + (activeProfile == 1 ? m_currentGripDeltaRot.z : 0.0f)
    };

    // Build the Socket's local offset matrix with converged grip offsets
    const DirectX::XMMATRIX matScale{ DirectX::XMMatrixScaling(m_localScale[activeProfile].x, m_localScale[activeProfile].y, m_localScale[activeProfile].z) };
    const DirectX::XMMATRIX matRot{ DirectX::XMMatrixRotationRollPitchYaw(
        DirectX::XMConvertToRadians(effectiveRot.x),
        DirectX::XMConvertToRadians(effectiveRot.y),
        DirectX::XMConvertToRadians(effectiveRot.z)
    ) };
    const DirectX::XMMATRIX matTrans{ DirectX::XMMatrixTranslation(effectivePos.x, effectivePos.y, effectivePos.z) };

    const DirectX::XMMATRIX matSocketLocal{ matScale * matRot * matTrans };

    // Multiply by the evaluated bone matrix (which is already in the parent's local space)
    const DirectX::XMMATRIX matBoneGlobal{ DirectX::XMLoadFloat4x4(&globals[targetBone]) };
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

    ImGui::RadioButton("Profile: Holster", &s_editorPreviewProfile, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Profile: Combat", &s_editorPreviewProfile, 1);

    ImGui::Spacing();
    ImGui::Separator();

    const std::size_t pIdx{ static_cast<std::size_t>(s_editorPreviewProfile) };

    // Dynamic Bone Resolution Dropdown
    const auto& nodes{ model->GetNodes() };
    const std::string preview{ m_targetBoneName[pIdx].empty() ? "Select Bone..." : m_targetBoneName[pIdx] };

    ImGui::TextDisabled("TARGET BONE");
    ImGui::PushItemWidth(-1.0f);
    if (ImGui::BeginCombo("##BoneSelect", preview.c_str()))
    {
        for (std::size_t i{ 0 }; i < nodes.size(); ++i)
        {
            const bool isSelected{ m_targetBoneIndex[pIdx] == static_cast<int>(i) };
            if (ImGui::Selectable(nodes[i].name.c_str(), isSelected))
            {
                m_targetBoneIndex[pIdx] = static_cast<int>(i);
                m_targetBoneName[pIdx] = nodes[i].name;
            }
            if (isSelected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();

    ImGui::Spacing();
    ImGui::TextDisabled("SOCKET OFFSET (Relative to Bone)");

    // Allow fine-tuning the weapon position dynamically while animations play
    ImGui::DragFloat3("Socket Position", &m_localPosition[pIdx].x, 0.01f);
    ImGui::DragFloat3("Socket Rotation", &m_localRotation[pIdx].x, 1.0f);
    ImGui::DragFloat3("Socket Scale", &m_localScale[pIdx].x, 0.05f);
}

void SocketComponent::Serialize(nlohmann::json& outJson) const
{
    for (std::size_t i = 0; i < 2; ++i)
    {
        const std::string prefix = (i == 0) ? "Holster" : "Combat";
        outJson[prefix + "BoneName"] = m_targetBoneName[i];
        outJson[prefix + "LocalPosX"] = m_localPosition[i].x;
        outJson[prefix + "LocalPosY"] = m_localPosition[i].y;
        outJson[prefix + "LocalPosZ"] = m_localPosition[i].z;
        outJson[prefix + "LocalRotX"] = m_localRotation[i].x;
        outJson[prefix + "LocalRotY"] = m_localRotation[i].y;
        outJson[prefix + "LocalRotZ"] = m_localRotation[i].z;
        outJson[prefix + "LocalSclX"] = m_localScale[i].x;
        outJson[prefix + "LocalSclY"] = m_localScale[i].y;
        outJson[prefix + "LocalSclZ"] = m_localScale[i].z;
    }
}

void SocketComponent::Deserialize(const nlohmann::json& inJson)
{
    // Legacy single-profile fallback for existing JSONs
    if (inJson.contains("BoneName"))
    {
        m_targetBoneName[0] = inJson.value("BoneName", "");
        m_localPosition[0] = { inJson.value("LocalPosX", 0.0f), inJson.value("LocalPosY", 0.0f), inJson.value("LocalPosZ", 0.0f) };
        m_localRotation[0] = { inJson.value("LocalRotX", 0.0f), inJson.value("LocalRotY", 0.0f), inJson.value("LocalRotZ", 0.0f) };
        m_localScale[0] = { inJson.value("LocalSclX", 1.0f), inJson.value("LocalSclY", 1.0f), inJson.value("LocalSclZ", 1.0f) };
    }

    for (std::size_t i = 0; i < 2; ++i)
    {
        const std::string prefix = (i == 0) ? "Holster" : "Combat";
        if (inJson.contains(prefix + "BoneName"))
        {
            m_targetBoneName[i] = inJson.value(prefix + "BoneName", "");
            m_localPosition[i] = { inJson.value(prefix + "LocalPosX", 0.0f), inJson.value(prefix + "LocalPosY", 0.0f), inJson.value(prefix + "LocalPosZ", 0.0f) };
            m_localRotation[i] = { inJson.value(prefix + "LocalRotX", 0.0f), inJson.value(prefix + "LocalRotY", 0.0f), inJson.value(prefix + "LocalRotZ", 0.0f) };
            m_localScale[i] = { inJson.value(prefix + "LocalSclX", 1.0f), inJson.value(prefix + "LocalSclY", 1.0f), inJson.value(prefix + "LocalSclZ", 1.0f) };
        }
        m_targetBoneIndex[i] = -1; // Late-resolve
    }
}

REGISTER_COMPONENT(SocketComponent)