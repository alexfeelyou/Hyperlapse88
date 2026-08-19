#include "CapeSimulator.h"
#include <algorithm>
#include <cmath>

void CapeSimulator::AddChain(std::shared_ptr<Model> model, const std::vector<std::string>& boneNames)
{
    if (!model) return;
    m_model = model;

    BoneChain newChain;
    newChain.bones.reserve(boneNames.size()); // Optimization: Prevent reallocation

    for (const std::string& name : boneNames)
    {
        int index = model->GetNodeIndex(name.c_str());

        // BUG PREVENTION: Only simulate bones that actually exist!
        if (index != -1)
        {
            newChain.bones.push_back({ index, 0.0f, 0.0f, 0.0f, 0.0f });
        }
    }

    // Only save the chain if we actually found valid bones
    if (!newChain.bones.empty())
    {
        m_chains.push_back(std::move(newChain)); // Optimization: Move semantics
    }
}

void CapeSimulator::Update(float dt, const DirectX::XMFLOAT3& currentVelocity)
{
    if (m_chains.empty()) return;
    std::shared_ptr<Model> model = m_model.lock();
    if (!model) return;

    // Safety: Prevent math explosions on lag spikes
    float safeDt = (std::min)(dt, 0.033f);

    // Base target angles from player velocity
    float baseTargetAngleX = m_gravityAngleX + (currentVelocity.z * m_swaySensitivity);
    float baseTargetAngleZ = (currentVelocity.x * m_swaySensitivity);

    float limitMin = (std::min)(-m_maxSway, m_bodyClipLimit);
    float limitMax = (std::max)(-m_maxSway, m_bodyClipLimit);

    baseTargetAngleX = std::clamp(baseTargetAngleX, limitMin, limitMax);
    baseTargetAngleZ = std::clamp(baseTargetAngleZ, -m_maxSway, m_maxSway);

    // Update each strip of cloth completely independently!
    for (BoneChain& chain : m_chains)
    {
        // Reset the target angle for the TOP bone of THIS chain
        float targetAngleX = baseTargetAngleX;
        float targetAngleZ = baseTargetAngleZ;

        for (SimulatedBone& bone : chain.bones)
        {
            // Spring Math
            float forceX = (targetAngleX - bone.currentAngleX) * m_stiffness - (bone.velocityX * m_damping);
            float forceZ = (targetAngleZ - bone.currentAngleZ) * m_stiffness - (bone.velocityZ * m_damping);

            bone.velocityX += forceX * safeDt;
            bone.velocityZ += forceZ * safeDt;
            bone.currentAngleX += bone.velocityX * safeDt;
            bone.currentAngleZ += bone.velocityZ * safeDt;

            if (bone.currentAngleX < limitMin)
            {
                bone.currentAngleX = limitMin;
                bone.velocityX = 0.0f; // Hit the wall, kill the momentum instantly!
            }
            else if (bone.currentAngleX > limitMax)
            {
                bone.currentAngleX = limitMax;
                bone.velocityX = 0.0f; // Hit the wall, kill the momentum instantly!
            }

            // Also strictly clamp Z (Left/Right) so it doesn't spin wildly 
            if (bone.currentAngleZ < -m_maxSway) {
                bone.currentAngleZ = -m_maxSway;
                bone.velocityZ = 0.0f;
            }
            else if (bone.currentAngleZ > m_maxSway) {
                bone.currentAngleZ = m_maxSway;
                bone.velocityZ = 0.0f;
            }

            // Apply to Model
            Model::Node& node = model->GetNodes()[bone.nodeIndex];

            DirectX::XMMATRIX physicsTwist = DirectX::XMMatrixRotationRollPitchYaw(
                bone.currentAngleX, 0.0f, bone.currentAngleZ
            );

            DirectX::XMVECTOR animRot = DirectX::XMLoadFloat4(&node.rotation);
            DirectX::XMMATRIX animMatrix = DirectX::XMMatrixRotationQuaternion(animRot);

            DirectX::XMVECTOR finalRot = DirectX::XMQuaternionRotationMatrix(physicsTwist * animMatrix);
            DirectX::XMStoreFloat4(&node.rotation, finalRot);

            // Pass momentum to the next bone DOWN this specific chain
            targetAngleX = bone.currentAngleX * 0.8f;
            targetAngleZ = bone.currentAngleZ * 0.8f;
        }
    }
}