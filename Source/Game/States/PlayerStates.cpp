#include "AnimationComponent.h"
#include "CharacterMovementComponent.h"
#include "PlayerStates.h"
#include "StateMachine.h"
#include "StringHash.h"

// UNIFIED LOCOMOTION STATE
void PlayerLocomotion::Enter(PlayerControllerComponent* controller)
{
    // Set to None so the first Update() evaluates the WASD keys and transitions
    // DIRECTLY from Dash to the correct state, preserving the animation history.
    m_locoState = LocoState::None;

    if (auto* motor{ controller->GetMovement() })
    {
        motor->SetDesiredDirection({ 0.0f, 0.0f });
    }
}

void PlayerLocomotion::Update(PlayerControllerComponent* controller, float dt)
{
    const auto& intent{ controller->GetIntent() };
    auto* motor{ controller->GetMovement() };
    auto* anim{ controller->GetAnimation() };

    if (!motor || !anim) return;

    // 1. Action Priorities (Interrupts)
    if (intent.bDashTriggered)
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Dash));
        return;
    }

    if (intent.bAttackPressed)
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Attack));
        return;
    }

    // 2. Process Locomotion Intent
    motor->SetDesiredDirection(intent.moveVector);

    // Calculate input magnitude squared to determine speed threshold
    const float inputSq{ (intent.moveVector.x * intent.moveVector.x) + (intent.moveVector.y * intent.moveVector.y) };

    // Evaluate the target state based on analog/dampened stick input
    LocoState desiredState = LocoState::Idle;
    if (inputSq > 0.2f) // E.g., WASD is pressed fully (1.0)
    {
        desiredState = LocoState::Run;
    }
    else if (inputSq > 0.01f) // E.g., WASD + Alt is pressed (0.35 squared = 0.12)
    {
        desiredState = LocoState::Walk;
    }

    // 3. Command Crossfade only if the state changed
    if (m_locoState != desiredState)
    {
        if (desiredState == LocoState::Idle)
        {
            anim->PlayStateByHash(Core::Hash("Idle"));
        }
        else if (desiredState == LocoState::Walk)
        {
            anim->PlayStateByHash(Core::Hash("Walk"));
        }
        else
        {
            anim->PlayStateByHash(Core::Hash("Run")); // Make sure you add "Run" in your Inspector!
        }

        m_locoState = desiredState;
    }
}

void PlayerLocomotion::Exit(PlayerControllerComponent* controller) {}

// DASH STATE
void PlayerDash::Enter(PlayerControllerComponent* controller)
{
    const auto& intent{ controller->GetIntent() };

    m_dashDir = intent.moveVector;
    if (m_dashDir.x == 0.0f && m_dashDir.y == 0.0f)
    {
        m_dashDir = { 0.0f, 1.0f }; // Default forward dodge
    }

    if (auto* motor{ controller->GetMovement() })
    {
        motor->AddImpulse({
            m_dashDir.x * DASH_IMPULSE_FORCE,
            0.0f,
            m_dashDir.y * DASH_IMPULSE_FORCE
            });
    }

    if (auto* anim{ controller->GetAnimation() })
    {
        const std::uint64_t dashHash{ Core::Hash("Dash") };
        anim->PlayStateByHash(dashHash);

        // DATA-DRIVEN TIMING: Read the exact length of the animation from the JSON data!
        m_timer = anim->GetStateDurationByHash(dashHash);
    }
}

void PlayerDash::Update(PlayerControllerComponent* controller, float dt)
{
    // Simplified purely to time-based exiting
    m_timer -= dt;
    if (m_timer <= 0.0f)
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
    }
}

void PlayerDash::Exit(PlayerControllerComponent* controller) {}

// ATTACK STATE (Placeholder)
void PlayerAttackState::Enter(PlayerControllerComponent* controller) {}
void PlayerAttackState::Update(PlayerControllerComponent* controller, float dt)
{
    // Temporary fallback to exit attack instantly until Phase 5 logic is wired
    controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
}
void PlayerAttackState::Exit(PlayerControllerComponent* controller) {}

// HIT REACT STATE (Placeholder)
void PlayerHitReactState::Enter(PlayerControllerComponent* controller) {}
void PlayerHitReactState::Update(PlayerControllerComponent* controller, float dt) {}
void PlayerHitReactState::Exit(PlayerControllerComponent* controller) {}