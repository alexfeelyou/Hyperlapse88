#pragma once

#include <DirectXMath.h>
#include "IComponent.h"

class Stage; // Forward declaration 

// Bridges the static Stage geometry into the GameObject hierarchy
class StageComponent final : public IComponent
{
public:
    // Explicit constructor prevents accidental implicit conversions
    explicit StageComponent(Stage* stage) noexcept;
    ~StageComponent() override = default;

    // Delete copy/move semantics to enforce strict 1:1 ownership
    StageComponent(const StageComponent&) = delete;
    StageComponent& operator=(const StageComponent&) = delete;

    void OnAttach(GameObject* owner) noexcept override;
    void Update(float dt) override;
    void DrawInspector() override;

    [[nodiscard]] const char* GetTypeName() const noexcept override { return "StageComponent"; }

private:
    [[nodiscard]] static constexpr bool IsFloatEqual(float a, float b, float epsilon = 0.0001f) noexcept;

    Stage* m_stage{ nullptr }; // Non-owning raw pointer to the actual Stage

    // Cached states to detect if the Editor modified the Transform manually this frame
    DirectX::XMFLOAT3 m_lastFramePos{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_lastFrameRot{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_lastFrameScale{ 1.0f, 1.0f, 1.0f };
};