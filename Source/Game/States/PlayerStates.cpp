#include "AnimationComponent.h"
#include "CharacterMovementComponent.h"
#include "PlayerStates.h"
#include "StateMachine.h"

using namespace Engine::Animation;

// GROUND & LOCOMOTION

void PlayerLocomotion::Enter(PlayerControllerComponent* controller)
{
    if (auto* motor{ controller->GetMovement() })
    {
        motor->SetDesiredDirection({ 0.0f, 0.0f });
    }
}

void PlayerLocomotion::Update(PlayerControllerComponent* controller, float dt)
{
    const auto& intent{ controller->GetIntent() };
    auto* motor{ controller->GetMovement() };
    if (!motor) return;

    if (intent.bDashTriggered)
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::DashEvade));
        return;
    }
    if (intent.bAttackPressed)
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AttackPrimary));
        return;
    }

    motor->SetDesiredDirection(intent.worldMoveDirection);

    auto& blackboard{ controller->getAnimBlackboard() };
    const DirectX::XMFLOAT3 velocity{ motor->GetTotalVelocity() };

    blackboard.groundSpeed = std::sqrt((velocity.x * velocity.x) + (velocity.z * velocity.z));
    blackboard.verticalVelocity = velocity.y;
    blackboard.setFlag(AnimFlag::is_grounded, motor->isGrounded());

    // O(1) Semantic Execution (Zero String Hashing)
    if (auto* anim{ controller->GetAnimation() })
    {
        anim->PlaySlot(AnimSlot::Locomotion);
    }
}
void PlayerLocomotion::Exit(PlayerControllerComponent* controller) {}

// Placeholders for expanded movement
void PlayerPivotTurn::Enter(PlayerControllerComponent*) {}
void PlayerPivotTurn::Update(PlayerControllerComponent*, float) {}
void PlayerPivotTurn::Exit(PlayerControllerComponent*) {}

void PlayerSlide::Enter(PlayerControllerComponent*) {}
void PlayerSlide::Update(PlayerControllerComponent*, float) {}
void PlayerSlide::Exit(PlayerControllerComponent*) {}

// 2. AERIAL & PARKOUR

void PlayerAirTraversal::Enter(PlayerControllerComponent*) {}
void PlayerAirTraversal::Update(PlayerControllerComponent*, float) {}
void PlayerAirTraversal::Exit(PlayerControllerComponent*) {}

void PlayerParkourWall::Enter(PlayerControllerComponent*) {}
void PlayerParkourWall::Update(PlayerControllerComponent*, float) {}
void PlayerParkourWall::Exit(PlayerControllerComponent*) {}

void PlayerDashEvade::Enter(PlayerControllerComponent* controller)
{
    m_canCancel = false;
    const auto& intent{ controller->GetIntent() };

    m_dashDir = intent.moveVector;
    if (m_dashDir.x == 0.0f && m_dashDir.y == 0.0f)
    {
        m_dashDir = { 0.0f, 1.0f };
    }

    if (auto* motor{ controller->GetMovement() })
    {
        motor->AddImpulse(DirectX::XMFLOAT3{
            m_dashDir.x * DASH_IMPULSE_FORCE,
            0.0f,
            m_dashDir.y * DASH_IMPULSE_FORCE
            });
    }

    if (auto* anim{ controller->GetAnimation() })
    {
        anim->PlaySlot(AnimSlot::DashEvade);
        m_timer = anim->GetSlotDuration(AnimSlot::DashEvade);
    }
}

void PlayerDashEvade::Update(PlayerControllerComponent* controller, float dt)
{
    m_timer -= dt;

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

    if (m_canCancel && intent.bAttackPressed)
    {
        controller->getAnimBlackboard().actionIndex = 2; // Route to Dash Attack via blackboard
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AttackContextual));
        return;
    }

    if (m_canCancel && playerWantsToMove)
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
        return;
    }

    if (m_timer <= 0.0f)
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
    }
}
void PlayerDashEvade::Exit(PlayerControllerComponent* controller) {}

// COMBAT (GROUND)

void PlayerAttackPrimary::Enter(PlayerControllerComponent* controller)
{
    m_comboIndex = 0;

    const auto& intent = controller->GetIntent();
    m_lungeDirection = intent.moveVector;
    if (m_lungeDirection.x == 0.0f && m_lungeDirection.y == 0.0f)
    {
        m_lungeDirection = { 0.0f, 1.0f };
    }

    if (auto* motor = controller->GetMovement())
    {
        motor->SetDesiredDirection({ 0.0f, 0.0f });
    }

    PlayCurrentAttack(controller);
}

void PlayerAttackPrimary::PlayCurrentAttack(PlayerControllerComponent* controller) noexcept
{
    m_canCancel = false;
    m_attackBufferTimer = 0.0f;

    controller->getAnimBlackboard().actionIndex = m_comboIndex;

    if (auto* anim = controller->GetAnimation())
    {
        anim->PlaySlot(AnimSlot::Attack_Primary, true);
        m_exitTimer = anim->GetSlotDuration(AnimSlot::Attack_Primary);
    }
}

void PlayerAttackPrimary::Update(PlayerControllerComponent* controller, float dt)
{
    m_exitTimer -= dt;

    if (m_attackBufferTimer > 0.0f) m_attackBufferTimer -= dt;

    const auto& intent = controller->GetIntent();
    if (intent.bAttackPressed)
    {
        m_attackBufferTimer = 0.25f;
    }

    for (const auto& ev : controller->GetAnimation()->GetFiredEvents())
    {
        if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::CancelWindow_Open))
        {
            m_canCancel = true;
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Lunge_Impulse))
        {
            if (auto* motor = controller->GetMovement())
            {
                motor->AddImpulse(DirectX::XMFLOAT3{
                    m_lungeDirection.x * ev.payload,
                    0.0f,
                    m_lungeDirection.y * ev.payload
                    });
            }
        }
    }

    if (m_canCancel && intent.bDashTriggered)
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::DashEvade));
        return;
    }

    if (m_canCancel && m_attackBufferTimer > 0.0f)
    {
        if (m_comboIndex < 3)
        {
            m_comboIndex++;

            m_lungeDirection = intent.moveVector;
            if (m_lungeDirection.x == 0.0f && m_lungeDirection.y == 0.0f)
            {
                m_lungeDirection = { 0.0f, 1.0f };
            }

            PlayCurrentAttack(controller);
            return;
        }
    }

    if (m_exitTimer <= 0.0f)
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
    }
}
void PlayerAttackPrimary::Exit(PlayerControllerComponent* controller) {}

// Placeholders for Expanded Combat
void PlayerAttackContextual::Enter(PlayerControllerComponent*) {}
void PlayerAttackContextual::Update(PlayerControllerComponent*, float) {}
void PlayerAttackContextual::Exit(PlayerControllerComponent*) {}

void PlayerAttackDirectional::Enter(PlayerControllerComponent*) {}
void PlayerAttackDirectional::Update(PlayerControllerComponent*, float) {}
void PlayerAttackDirectional::Exit(PlayerControllerComponent*) {}

void PlayerAttackCharged::Enter(PlayerControllerComponent*) {}
void PlayerAttackCharged::Update(PlayerControllerComponent*, float) {}
void PlayerAttackCharged::Exit(PlayerControllerComponent*) {}

void PlayerAttackAerial::Enter(PlayerControllerComponent*) {}
void PlayerAttackAerial::Update(PlayerControllerComponent*, float) {}
void PlayerAttackAerial::Exit(PlayerControllerComponent*) {}

// DEFENSE & REACTION
void PlayerParryCounter::Enter(PlayerControllerComponent*) {}
void PlayerParryCounter::Update(PlayerControllerComponent*, float) {}
void PlayerParryCounter::Exit(PlayerControllerComponent*) {}

void PlayerHitReact::Enter(PlayerControllerComponent*) {}
void PlayerHitReact::Update(PlayerControllerComponent*, float) {}
void PlayerHitReact::Exit(PlayerControllerComponent*) {}