#pragma once

#include <DirectXMath.h>
#include "Character.h" 
#include "GameObject.h"
#include "IComponent.h"

// Bridges existing Character-derived classes into the GameObject hierarchy
// Automatically syncs Transform data between the Editor and the Game Logic
class LegacyCharacterComponent final : public IComponent
{
public:
    // Explicit constructor prevents accidental implicit conversions from Character pointers
    explicit LegacyCharacterComponent(Character* character) noexcept;
    ~LegacyCharacterComponent() override = default;

    // Delete copy/move to strictly enforce 1:1 component-to-entity lifecycle mapping
    LegacyCharacterComponent(const LegacyCharacterComponent&) = delete;
    LegacyCharacterComponent& operator=(const LegacyCharacterComponent&) = delete;

    void OnAttach(GameObject* owner) noexcept override;

    void Update(float dt) override;
    void Render(class ModelRenderer* renderer) override;
    void DrawInspector() override;

    [[nodiscard]] const char* GetTypeName() const noexcept override { return "LegacyCharacterComponent"; }
    [[nodiscard]] Character* GetCharacter() const noexcept { return m_character; }

private:
    // Helper function to safely compare floats for Editor manipulation detection
    [[nodiscard]] static constexpr bool IsFloatEqual(float a, float b, float epsilon = 0.001f) noexcept;

    Character* m_character{ nullptr }; // Non-owning pointer to the actual gameplay entity

    // Cached states to detect if the Editor modified the Transform manually this frame
    DirectX::XMFLOAT3 m_lastFramePos{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_lastFrameRot{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_lastFrameScale{ 1.0f, 1.0f, 1.0f };
};