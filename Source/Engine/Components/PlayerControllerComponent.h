#pragma once

#include <array>           
#include <DirectXMath.h>
#include <memory>
#include "AnimBlackboard.h"
#include "IComponent.h"
#include "StateMachine.h" 

// Strongly typed enum representing discrete gameplay logic layers
enum class PlayerStateType : std::uint8_t
{
    None = 0,
    Locomotion,
    PivotTurn,
    Slide,
    AirTraversal,
    ParkourWall,
    DashEvade,
    AttackPrimary,
    AttackContextual,
    AttackDirectional,
    AttackCharged,
    AttackAerial,
    ParryCounter,
    HitReact,
    Count // Automatically handles the pool sizing
};

// Forward declarations 
class CharacterMovementComponent;
class AnimationComponent; 
class PlayerState;        

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

    // Animation Component Accessor
    [[nodiscard]] AnimationComponent* GetAnimation() const noexcept { return m_animation; }

    // Exposes the state machine 
    [[nodiscard]] StateMachine* GetStateMachine() const noexcept { return m_stateMachine.get(); }

    // Exposes the blackboard for the Animation system to read
    [[nodiscard]] Engine::Animation::AnimBlackboard& getAnimBlackboard() noexcept { return m_blackboard; }
    [[nodiscard]] const Engine::Animation::AnimBlackboard& getAnimBlackboard() const noexcept { return m_blackboard; }

    // Fast O(1) state retrieval from the preallocated array 
    [[nodiscard]] PlayerState* GetState(PlayerStateType type) const noexcept
    {
        return m_states[static_cast<std::size_t>(type)].get();
    }

    void SetInputEnabled(bool enabled) noexcept { m_inputEnabled = enabled; }

private:
    void GatherHardwareInput() noexcept;

    InputIntent m_intent{};
    bool m_inputEnabled{ true };

    // Component Caches
    CharacterMovementComponent* m_movement{ nullptr };
    AnimationComponent* m_animation{ nullptr }; 

    // Owns the state machine logic
    std::unique_ptr<StateMachine> m_stateMachine{};

    // Preallocated states pool (Zero allocations at runtime) 
    std::array<std::unique_ptr<PlayerState>, static_cast<std::size_t>(PlayerStateType::Count)> m_states{};

	// Shared data contract between gameplay and animation systems
    Engine::Animation::AnimBlackboard m_blackboard{};
};