#pragma once

#include <DirectXMath.h>
#include <json.hpp>
#include <string>
#include "AnimationComponent.h"
#include "IComponent.h"

// Anchors a GameObject to a specific skeletal bone of a parent AnimationComponent.
// Strictly evaluates in O(1) against contiguous global matrices.
class SocketComponent final : public IComponent
{
public:
    SocketComponent() noexcept = default;
    ~SocketComponent() override = default;

    SocketComponent(const SocketComponent&) = delete;
    SocketComponent& operator=(const SocketComponent&) = delete;

    void OnAttach(GameObject* owner) noexcept override;
    void Update(float dt) override;
    void DrawInspector() override;

    void Serialize(nlohmann::json& outJson) const override;
    void Deserialize(const nlohmann::json& inJson) override;

    [[nodiscard]] const char* GetTypeName() const noexcept override { return "SocketComponent"; }

private:
    void ResolveBoneIndex() noexcept;
    [[nodiscard]] std::size_t GetActiveProfileIndex() const noexcept;

    AnimationComponent* m_targetAnim{ nullptr };

    // Dual-Profile Storage: [0] = Holster, [1] = Combat
    std::string m_targetBoneName[2]{};
    int m_targetBoneIndex[2]{ -1, -1 };

    // The localized offsets applied ON TOP of the bone's exact transform
    DirectX::XMFLOAT3 m_localPosition[2]{ {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
    DirectX::XMFLOAT3 m_localRotation[2]{ {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
    DirectX::XMFLOAT3 m_localScale[2]{ {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f} };

    // Dynamic additive convergence accumulators (Smoothly blends between different attack grips)
    DirectX::XMFLOAT3 m_currentGripDeltaPos{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_currentGripDeltaRot{ 0.0f, 0.0f, 0.0f };
};