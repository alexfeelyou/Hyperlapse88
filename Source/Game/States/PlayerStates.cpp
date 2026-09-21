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

    // Latch the Cancel Window 
    for (const auto& ev : controller->GetAnimation()->GetFiredEvents())
    {
        if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::CancelWindow_Open))
        {
            m_canCancel = true;
            break;
        }
    }

    const auto& intent = controller->GetIntent();
    const float inputSq = (intent.moveVector.x * intent.moveVector.x) + (intent.moveVector.y * intent.moveVector.y);
    const bool playerWantsToMove = (inputSq > 0.01f);

    // Dash Attack Interrupt (Highest Priority)
    if (m_canCancel && intent.bAttackPressed)
    {
        // Grab the attack state, flag it as a dash attack, and transition
        auto* attackState = static_cast<PlayerAttackState*>(controller->GetState(PlayerStateType::Attack));
        attackState->SetDashAttackNext(true);
        controller->GetStateMachine()->ChangeState(controller, attackState);
        return;
    }

    // Standard Dash Cancel
    if (m_canCancel && playerWantsToMove)
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
        return;
    }

    // Natural Exit
    if (m_timer <= 0.0f)
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
    }
}

void PlayerDash::Exit(PlayerControllerComponent* controller) {}

// ATTACK STATE
// A static array of hashes defining your 4-hit combo path, plus the dash attack at index 4
static const std::uint64_t COMBO_CHAIN[5] = {
    Core::Hash("Attack1"),
    Core::Hash("Attack2"),
    Core::Hash("Attack3"),
    Core::Hash("Attack4"),
    Core::Hash("DashAttack") // Index 4: Placeholder for when you get the animation
};

void PlayerAttackState::Enter(PlayerControllerComponent* controller)
{
    // Route to Dash Attack or Standard Combo
    if (m_wantsDashAttack)
    {
        m_comboIndex = 4;
        m_wantsDashAttack = false; // Reset for next time
    }
    else
    {
        m_comboIndex = 0;
    }

    // Lock in the Lunge Direction at the start of the attack
    const auto& intent = controller->GetIntent();
    m_lungeDirection = intent.moveVector;
    if (m_lungeDirection.x == 0.0f && m_lungeDirection.y == 0.0f)
    {
        // Neutral input defaults to a straight forward lunge
        m_lungeDirection = { 0.0f, 1.0f };
    }

    // Hard stop existing locomotion velocity on frame 1 of the attack
    if (auto* motor = controller->GetMovement())
    {
        motor->SetDesiredDirection({ 0.0f, 0.0f });
    }

    PlayCurrentAttack(controller);
}

void PlayerAttackState::PlayCurrentAttack(PlayerControllerComponent* controller) noexcept
{
    m_canCancel = false;
    m_attackBufferTimer = 0.0f;

    if (auto* anim = controller->GetAnimation())
    {
        anim->PlayStateByHash(COMBO_CHAIN[m_comboIndex]);
        m_exitTimer = anim->GetStateDurationByHash(COMBO_CHAIN[m_comboIndex]);
    }
}

void PlayerAttackState::Update(PlayerControllerComponent* controller, float dt)
{
    m_exitTimer -= dt;

    // INPUT BUFFERING (Keep player intent alive for 250ms)
    if (m_attackBufferTimer > 0.0f) m_attackBufferTimer -= dt;

    const auto& intent = controller->GetIntent();
    if (intent.bAttackPressed)
    {
        m_attackBufferTimer = 0.25f; // Store attack intent to prevent dropped inputs
    }

    // EVENT PARSING (Extracting the Payload)
    for (const auto& ev : controller->GetAnimation()->GetFiredEvents())
    {
        if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::CancelWindow_Open))
        {
            m_canCancel = true;
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Lunge_Impulse))
        {
            // PROCEDURAL LUNGE: Propel the character physically along the locked attack direction!
            if (auto* motor = controller->GetMovement())
            {
                motor->AddImpulse({
                    m_lungeDirection.x * ev.payload,
                    0.0f,
                    m_lungeDirection.y * ev.payload
                    });
            }
        }
    }

    // PRIORITY HIERARCHY EVALUATION

    // Priority A: Evasion Cancel (Highest - always break combos to survive)
    if (m_canCancel && intent.bDashTriggered)
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Dash));
        return;
    }

    // Priority B: Combo Advance
    if (m_canCancel && m_attackBufferTimer > 0.0f)
    {
        // Standard Combo Chain (Index 0 -> 1 -> 2 -> 3)
        if (m_comboIndex < 3)
        {
            m_comboIndex++;

            // Re-evaluate directional intent so the player can re-aim between combo strikes
            m_lungeDirection = intent.moveVector;
            if (m_lungeDirection.x == 0.0f && m_lungeDirection.y == 0.0f)
            {
                m_lungeDirection = { 0.0f, 1.0f };
            }

            PlayCurrentAttack(controller);
            return;
        }
        // Dash Attack Route (Chains seamlessly from DashAttack into Attack2)
        else if (m_comboIndex == 4)
        {
            m_comboIndex = 1;
            PlayCurrentAttack(controller);
            return;
        }
    }

    // Priority C: Natural Exit (Sword returns to resting position)
    if (m_exitTimer <= 0.0f)
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
    }
}

void PlayerAttackState::Exit(PlayerControllerComponent* controller) {}

// HIT REACT STATE (Placeholder)
void PlayerHitReactState::Enter(PlayerControllerComponent* controller) {}
void PlayerHitReactState::Update(PlayerControllerComponent* controller, float dt) {}
void PlayerHitReactState::Exit(PlayerControllerComponent* controller) {}