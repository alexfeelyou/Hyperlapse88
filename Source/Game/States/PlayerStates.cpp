#include "AnimationComponent.h"
#include "CharacterMovementComponent.h"
#include "PlayerStates.h"
#include "StateMachine.h"
#include "StringHash.h"

// UNIFIED LOCOMOTION STATE
void PlayerLocomotion::Enter(PlayerControllerComponent* controller)
{
    m_isWalking = false;

    // Evaluate hash at compile time, request state at O(1) runtime speed
    if (auto* anim{ controller->GetAnimation() })
    {
        anim->PlayStateByHash(Core::Hash("Idle"));
    }

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

    // Action Priorities (Interrupts)
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

    // Process Locomotion Intent
    motor->SetDesiredDirection(intent.moveVector);

    // Calculate input magnitude squared to determine if we are attempting to move
    const float inputSq{ (intent.moveVector.x * intent.moveVector.x) + (intent.moveVector.y * intent.moveVector.y) };
    const bool wantsToMove{ inputSq > 0.01f };

    // Animation State Blending
    if (wantsToMove && !m_isWalking)
    {
        anim->PlayStateByHash(Core::Hash("Walk"));
        m_isWalking = true;
    }
    else if (!wantsToMove && m_isWalking)
    {
        anim->PlayStateByHash(Core::Hash("Idle"));
        m_isWalking = false;
    }
}

void PlayerLocomotion::Exit(PlayerControllerComponent* controller) {}

// DASH STATE
void PlayerDash::Enter(PlayerControllerComponent* controller)
{
    m_timer = DASH_DURATION;
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

    // Play dodge animation
    if (auto* anim{ controller->GetAnimation() })
    {
        anim->PlayStateByHash(Core::Hash("Dash")); 
    }
}

void PlayerDash::Update(PlayerControllerComponent* controller, float dt)
{
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