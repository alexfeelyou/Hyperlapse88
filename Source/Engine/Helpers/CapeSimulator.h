#pragma once

#include <vector>
#include <string>
#include <memory>
#include <DirectXMath.h>
#include "System/Model.h"

class CapeSimulator
{
public:
    CapeSimulator() = default;
    ~CapeSimulator() = default;

    CapeSimulator(const CapeSimulator&) = delete;
    CapeSimulator& operator=(const CapeSimulator&) = delete;

    // Registers a single vertical strip of cloth
    void AddChain(std::shared_ptr<Model> model, const std::vector<std::string>& boneNames);

    // Clears all chains (useful if player changes costumes)
    void ClearChains() { m_chains.clear(); }

    void Update(float dt, const DirectX::XMFLOAT3& currentVelocity);

    // Tuning pointers for ImGui
    float* GetStiffness() { return &m_stiffness; }
    float* GetDamping() { return &m_damping; }
    float* GetMaxSway() { return &m_maxSway; }
    float* GetBodyClipLimit() { return &m_bodyClipLimit; }
    float* GetGravityAngle() { return &m_gravityAngleX; }
    float* GetSwaySensitivity() { return &m_swaySensitivity; }

private:
    struct SimulatedBone
    {
        int nodeIndex{ -1 };
        float currentAngleX{ 0.0f };
        float currentAngleZ{ 0.0f };
        float velocityX{ 0.0f };
        float velocityZ{ 0.0f };
    };

    // BUG PREVENTION: Group bones into isolated vertical chains to prevent cross-contamination
    struct BoneChain
    {
        std::vector<SimulatedBone> bones{};
    };

    std::weak_ptr<Model> m_model{};
    std::vector<BoneChain> m_chains{};

    // Default parameters
    float m_stiffness{ 200.0f };
    float m_damping{ 12.0f };
    float m_maxSway{ 0.25f };
    float m_bodyClipLimit{ 0.15f };
    float m_gravityAngleX{ -0.15f };
    float m_swaySensitivity{ 0.030f };
};