#include "PlayerStates.h"
#include "CharacterMovementComponent.h"
#include "StateMachine.h"

// --- IDLE STATE ---

void PlayerIdle::Enter(PlayerControllerComponent* controller)
{
    // Tell the motor we want to stop
    if (auto* motor{ controller->GetMovement() })
    {
        motor->SetDesiredDirection({ 0.0f, 0.0f });
    }

    // TODO: controller->GetAnimator()->Play("Idle", true, 0.2f);
}

void PlayerIdle::Update(PlayerControllerComponent* controller, float dt)
{
    const auto& intent{ controller->GetIntent() };
    auto* motor{ controller->GetMovement() };
    if (!motor) return;

    // Combat / Action Priority
    if (intent.bDashTriggered)
    {
        controller->GetStateMachine()->ChangeState(controller, std::make_unique<PlayerDash>());
        return;
    }

    if (intent.bAttackPressed)
    {
        // TODO: Proximity check for Slash vs Shoot
        controller->GetStateMachine()->ChangeState(controller, std::make_unique<PlayerShoot>());
        return;
    }

    // Locomotion Fallback
    if (intent.moveVector.x != 0.0f || intent.moveVector.y != 0.0f)
    {
        controller->GetStateMachine()->ChangeState(controller, std::make_unique<PlayerMoving>());
    }
}

void PlayerIdle::Exit(PlayerControllerComponent* controller) {}

// --- MOVING STATE ---

void PlayerMoving::Enter(PlayerControllerComponent* controller)
{
    // TODO: controller->GetAnimator()->Play("RunPistol", true, 0.2f);
}

void PlayerMoving::Update(PlayerControllerComponent* controller, float dt)
{
    const auto& intent{ controller->GetIntent() };
    auto* motor{ controller->GetMovement() };
    if (!motor) return;

    // Combat Priority
    if (intent.bDashTriggered)
    {
        controller->GetStateMachine()->ChangeState(controller, std::make_unique<PlayerDash>());
        return;
    }

    if (intent.bAttackPressed)
    {
        controller->GetStateMachine()->ChangeState(controller, std::make_unique<PlayerShoot>());
        return;
    }

    // Feed Intent to Motor
    motor->SetDesiredDirection(intent.moveVector);

    // Idle Fallback
    if (intent.moveVector.x == 0.0f && intent.moveVector.y == 0.0f && !motor->IsMoving())
    {
        controller->GetStateMachine()->ChangeState(controller, std::make_unique<PlayerIdle>());
    }
}

void PlayerMoving::Exit(PlayerControllerComponent* controller) {}

// --- DASH STATE ---

void PlayerDash::Enter(PlayerControllerComponent* controller)
{
    m_timer = DASH_DURATION;
    const auto& intent{ controller->GetIntent() };

    // Store dash direction based on current input; fallback to forward if idle
    m_dashDir = intent.moveVector;
    if (m_dashDir.x == 0.0f && m_dashDir.y == 0.0f)
    {
        m_dashDir = { 0.0f, 1.0f };
    }

    if (auto* motor{ controller->GetMovement() })
    {
        // Add instant kinetic impulse. The motor's internal drag handles the sliding friction.
        motor->AddImpulse({
            m_dashDir.x * DASH_IMPULSE_FORCE,
            0.0f,
            m_dashDir.y * DASH_IMPULSE_FORCE
            });
    }

    // TODO: controller->GetHealth()->TriggerInvincibility(DASH_IFRAME_DURATION);
    // TODO: Play Dash VFX & SFX
}

void PlayerDash::Update(PlayerControllerComponent* controller, float dt)
{
    m_timer -= dt;

    if (m_timer <= 0.0f)
    {
        const auto& intent{ controller->GetIntent() };
        if (intent.moveVector.x != 0.0f || intent.moveVector.y != 0.0f)
        {
            controller->GetStateMachine()->ChangeState(controller, std::make_unique<PlayerMoving>());
        }
        else
        {
            controller->GetStateMachine()->ChangeState(controller, std::make_unique<PlayerIdle>());
        }
    }
}

void PlayerDash::Exit(PlayerControllerComponent* controller) {}

// --- SLASH STATE ---

void PlayerSlash::Enter(PlayerControllerComponent* controller)
{
    m_timer = SLASH_DURATION;

    // TODO: controller->SetActiveWeapon(Sword);
    // TODO: controller->GetAnimator()->PlayUpper("Parry", false);

    if (auto* motor{ controller->GetMovement() })
    {
        // Increase friction artificially so the lunge decelerates very quickly
        motor->SetFrictionMultiplier(SLASH_DRAG_MULTIPLIER);

        // TODO: Get Yaw Rotation from GameObject to determine Forward Vector
        // motor->AddImpulse({ fwd.x * SLASH_LUNGE_FORCE, 0.0f, fwd.z * SLASH_LUNGE_FORCE });
    }
}

void PlayerSlash::Update(PlayerControllerComponent* controller, float dt)
{
    m_timer -= dt;

    if (m_timer <= 0.0f)
    {
        controller->GetStateMachine()->ChangeState(controller, std::make_unique<PlayerIdle>());
    }
}

void PlayerSlash::Exit(PlayerControllerComponent* controller)
{
    if (auto* motor{ controller->GetMovement() })
    {
        // Restore standard friction
        motor->SetFrictionMultiplier(1.0f);
    }
}

// --- SHOOT STATE ---

void PlayerShoot::Enter(PlayerControllerComponent* controller)
{
    m_timer = BASE_SHOOT_DELAY * HOLD_PENALTY_MULTIPLIER;
    m_minTapCooldown = BASE_SHOOT_DELAY;

    // TODO: Fire Projectile & Play SFX
}

void PlayerShoot::Update(PlayerControllerComponent* controller, float dt)
{
    const auto& intent{ controller->GetIntent() };

    if (intent.bDashTriggered)
    {
        controller->GetStateMachine()->ChangeState(controller, std::make_unique<PlayerDash>());
        return;
    }

    m_timer -= dt;
    m_minTapCooldown -= dt;

    if (!intent.bAttackPressed && m_minTapCooldown <= 0.0f)
    {
        controller->GetStateMachine()->ChangeState(controller, std::make_unique<PlayerIdle>());
        return;
    }

    if (intent.bAttackPressed && m_timer <= 0.0f)
    {
        m_timer = BASE_SHOOT_DELAY * HOLD_PENALTY_MULTIPLIER;
        m_minTapCooldown = BASE_SHOOT_DELAY;
        // TODO: Fire Projectile & Play SFX
    }
}

void PlayerShoot::Exit(PlayerControllerComponent* controller) {}