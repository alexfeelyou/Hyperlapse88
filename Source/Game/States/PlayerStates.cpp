#include "AnimationComponent.h"
#include "CharacterMovementComponent.h"
#include "PlayerStates.h"
#include "StateMachine.h"
#include "StringHash.h"

// UNIFIED LOCOMOTION STATE
void PlayerLocomotion::Enter(PlayerControllerComponent* controller)
{
    // Set to None so the first Update() evaluates the WASD keys and transitions
    // Directly from Dash to the correct state, preserving the animation history.
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

    // Process Locomotion Intent
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

    // Command Crossfade only if the state changed
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
            anim->PlayStateByHash(Core::Hash("Run")); 
        }

        m_locoState = desiredState;
    }
}

void PlayerLocomotion::Exit(PlayerControllerComponent* controller) {}

// DASH STATE
void PlayerDash::Enter(PlayerControllerComponent* controller)
{
    m_canCancel = false;
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

        // DATA-DRIVEN TIMING: Read the exact length of the animation from the JSON data
        m_timer = anim->GetStateDurationByHash(dashHash);
    }
}

void PlayerDash::Update(PlayerControllerComponent* controller, float dt)
{
    m_timer -= dt;

    // Latch the Cancel Window open permanently once the event fires
    for (std::uint32_t eventId : controller->GetAnimation()->GetFiredEvents())
    {
        if (eventId == static_cast<std::uint32_t>(CombatEventId::CancelWindow_Open))
        {
            m_canCancel = true;
            break;
        }
    }

    const auto& intent = controller->GetIntent();
    const float inputSq = (intent.moveVector.x * intent.moveVector.x) + (intent.moveVector.y * intent.moveVector.y);
    const bool playerWantsToMove = (inputSq > 0.01f);

    // Dual-Exit Logic
    if (m_canCancel && playerWantsToMove) 
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
        return;
    }

    // The player is not touching the keyboard. Wait for the timer to reach 0 to go to Idle.
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