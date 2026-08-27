#pragma once

#include "IComponent.h"

class Stage; // Forward declaration avoids unnecessary compilation dependencies

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

    void Update(float dt) override;
    void DrawInspector() override;

    [[nodiscard]] const char* GetTypeName() const noexcept override { return "StageComponent"; }

private:
    Stage* m_stage{ nullptr }; // Non-owning raw pointer to the actual Stage
};