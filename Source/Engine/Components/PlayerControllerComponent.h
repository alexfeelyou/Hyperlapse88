#pragma once

#include <DirectXMath.h>
#include <memory>
#include "IComponent.h"
#include "StateMachine.h" 

// Forward declarations 
class CharacterMovementComponent;

struct InputIntent
{
    DirectX::XMFLOAT2 moveVector{ 0.0f, 0.0f }; // Left Stick / WASD
    DirectX::XMFLOAT3 aimWorldTarget{ 0.0f, 0.0f, 0.0f }; // Right Stick / Mouse Raycast
    bool bDashTriggered{ false };
    bool bAttackPressed{ false };
};

// Translates hardware input into InputIntent and evaluates the State Machine
class PlayerControllerComponent final : public IComponent
{
public:
    PlayerControllerComponent() noexcept;
    ~PlayerControllerComponent() override;

    PlayerControllerComponent(const PlayerControllerComponent&) = delete;
    PlayerControllerComponent& operator=(const PlayerControllerComponent&) = delete;

    void OnAttach(GameObject* owner) noexcept override;
    void Update(float dt) override;
    void DrawInspector() override;

    [[nodiscard]] const char* GetTypeName() const noexcept override { return "PlayerControllerComponent"; }

    // Core Component Accessors
    [[nodiscard]] const InputIntent& GetIntent() const noexcept { return m_intent; }
    [[nodiscard]] CharacterMovementComponent* GetMovement() const noexcept { return m_movement; }

    // Exposes the state machine 
    [[nodiscard]] StateMachine* GetStateMachine() const noexcept { return m_stateMachine.get(); }

    void SetInputEnabled(bool enabled) noexcept { m_inputEnabled = enabled; }

private:
    void GatherHardwareInput() noexcept;

    InputIntent m_intent{};
    bool m_inputEnabled{ true };

    // Component Caches
    CharacterMovementComponent* m_movement{ nullptr };

    // Owns the state machine logic
    std::unique_ptr<StateMachine> m_stateMachine{};
};