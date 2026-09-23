#include "AnimationComponent.h"
#include "CapsuleColliderComponent.h"
#include "CharacterMovementComponent.h"
#include "FacingResolver.h"
#include "GameObject.h"
#include "OrbitCameraDriverComponent.h"
#include "PlayerStates.h"
#include "StateMachine.h"

using namespace Engine::Animation;

// GROUND & LOCOMOTION
void PlayerLocomotion::Enter(PlayerControllerComponent* controller)
{
    if (auto* motor{ controller->GetMovement() })
    {
        motor->SetDesiredDirection({ 0.0f, 0.0f });

        // SAFE RESET: Only replenish air actions if physically touching the ground
        if (motor->isGrounded())
        {
            controller->getAnimBlackboard().setFlag(AnimFlag::has_air_dashed, false);
            controller->getAnimBlackboard().currentJumps = 0;
        }
    }
}

void PlayerLocomotion::Update(PlayerControllerComponent* controller, float dt)
{
    const auto& intent{ controller->GetIntent() };
    auto* motor{ controller->GetMovement() };
    if (!motor) return;

    if (!motor->isGrounded())
    {
        // Stair/Slope Probing
        auto* capsule{ controller->GetOwner()->GetComponent<CapsuleColliderComponent>() };

        // Probe distance equals the stair step offset + a 0.3m safety buffer
        const float probeDistance{ capsule ? (capsule->GetConfig().stepOffset + 0.3f) : 0.4f };
        const bool isGroundDirectlyBelow{ capsule && capsule->HasGroundBelow(probeDistance) };

        if (!isGroundDirectlyBelow)
        {
            m_fallTimer += dt;
            if (m_fallTimer > 0.10f) // A tiny 100ms debounce for jagged geometry seams
            {
                controller->getAnimBlackboard().actionIndex = 1; // Route directly to Air Fall Loop
                controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AirTraversal));
                return;
            }
        }
        else
        {
            // We are bounding down stairs. The sweep detected the next step. Suppress the fall
            m_fallTimer = 0.0f;
        }
    }
    else
    {
        m_fallTimer = 0.0f; // Reset timer while touching ground
    }

    if (intent.bJumpTriggered && motor->isGrounded())
    {
        constexpr float JUMP_FORCE{ 6.5f };
        motor->Jump(JUMP_FORCE);

        auto& blackboard = controller->getAnimBlackboard();
        blackboard.actionIndex = 0;     // Route to Jump Takeoff
        blackboard.currentJumps = 1;    // Register the first jump

        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AirTraversal));
        return;
    }

    // Ground Actions (Dash / Attack)
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
    // ... the rest of the Locomotion logic remains identical ...

    auto& blackboard{ controller->getAnimBlackboard() };
    const DirectX::XMFLOAT3 velocity{ motor->GetTotalVelocity() };

    blackboard.groundSpeed = std::sqrt((velocity.x * velocity.x) + (velocity.z * velocity.z));
    blackboard.verticalVelocity = velocity.y;
    blackboard.setFlag(AnimFlag::is_grounded, motor->isGrounded());

    // Facing: continuously re-face every frame while Locomotion is active.
    //  - Strafing (is_strafing true, currently unset anywhere — dormant until a lock-on
    //    system sets it): always face the camera's look direction, so the player can
    //    circle-strafe independent of movement direction.
    //  - Free (default): face the resolved world-space move direction. No input leaves
    //    facing untouched (SmoothFaceDirection no-ops on a zero-length direction).
    constexpr float turnRateDegPerSec{ 720.0f };

    DirectX::XMFLOAT2 faceTargetXZ{ intent.worldMoveDirection };
    if (blackboard.getFlag(AnimFlag::is_strafing))
    {
        const float cameraYawRad{ OrbitCameraDriverComponent::GetActiveYawRadians() };
        faceTargetXZ = { std::sin(cameraYawRad), std::cos(cameraYawRad) };
    }

    FacingResolver::SmoothFaceDirection(controller->GetOwner(), faceTargetXZ, turnRateDegPerSec, dt);

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

// AERIAL & PARKOUR
void PlayerAirTraversal::Enter(PlayerControllerComponent* controller)
{
    m_airTimer = 0.0f;
    m_isAcrobatic = false;

    if (auto* anim{ controller->GetAnimation() })
    {
        anim->PlaySlot(AnimSlot::AirTraversal);
    }
}

void PlayerAirTraversal::Update(PlayerControllerComponent* controller, float dt)
{
    m_airTimer += dt; // Tick the air timer

    const auto& intent{ controller->GetIntent() };
    auto* motor{ controller->GetMovement() };
    if (!motor) return;

    auto& blackboard{ controller->getAnimBlackboard() };
    const DirectX::XMFLOAT3 velocity{ motor->GetTotalVelocity() };

    blackboard.groundSpeed = std::sqrt((velocity.x * velocity.x) + (velocity.z * velocity.z));
    blackboard.verticalVelocity = velocity.y;
    blackboard.setFlag(AnimFlag::is_grounded, motor->isGrounded());

    if (motor->isGrounded() && velocity.y <= 0.05f)
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Landing));
        return;
    }

    // THE DANGER ZONE LOCKOUT: 
    // If falling faster than -24 m/s (approaching the -28 m/s hard land), disable panic actions.
    const bool inDangerZone = (velocity.y < -24.0f);

    // Air Dash (Only allowed if not in danger zone, and has not dashed yet)
    if (intent.bDashTriggered && !inDangerZone && !blackboard.getFlag(AnimFlag::has_air_dashed))
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::DashEvade));
        return;
    }

    // Double Jump Trigger (Requires 200ms debounce from previous jump, not in danger zone, max 2 jumps)
    if (intent.bJumpTriggered && !inDangerZone && blackboard.currentJumps < 2 && m_airTimer > 0.2f)
    {
        blackboard.currentJumps++;
        motor->Jump(7.0f);

        m_isAcrobatic = true;
        if (auto* anim{ controller->GetAnimation() })
        {
            anim->PlaySlot(AnimSlot::Jump_Acrobatic, true);
        }
    }

    constexpr float AIR_CONTROL{ 0.35f };
    motor->SetDesiredDirection({ intent.worldMoveDirection.x * AIR_CONTROL, intent.worldMoveDirection.y * AIR_CONTROL });
    FacingResolver::SmoothFaceDirection(controller->GetOwner(), intent.worldMoveDirection, 200.0f, dt);

    if (auto* anim{ controller->GetAnimation() })
    {
        if (m_isAcrobatic && anim->GetCurrentTimer() >= anim->GetSlotDuration(AnimSlot::Jump_Acrobatic))
        {
            m_isAcrobatic = false;
        }

        if (!m_isAcrobatic)
        {
            anim->PlaySlot(AnimSlot::AirTraversal);
        }
    }
}

void PlayerAirTraversal::Exit(PlayerControllerComponent* controller) {}

// LANDING STATE
void PlayerLanding::Enter(PlayerControllerComponent* controller)
{
    m_canCancel = false;
    auto* motor = controller->GetMovement();
    auto* anim = controller->GetAnimation();
    if (!motor || !anim) return;

    // Snapshot the terminal velocity from the exact frame of impact
    const float terminalVelocity = motor->GetVerticalVelocity();
    const auto& intent = controller->GetIntent();

    // Check if player is holding directional input (intent to keep moving)
    const float inputSq = (intent.moveVector.x * intent.moveVector.x) + (intent.moveVector.y * intent.moveVector.y);
    const bool holdingMove = (inputSq > 0.01f);

    motor->SetDesiredDirection({ 0.0f, 0.0f });

    // Landing Threshold Logic
    int actionIdx = 0;
    if (terminalVelocity > -14.0f)
    {
        // Soft Land (Small hops / stairs)
        actionIdx = 0;
    }
    else if (terminalVelocity <= -14.0f && terminalVelocity > -28.0f && holdingMove)
    {
        // Roll Land (Parkour out of medium falls)
        actionIdx = 1;

        // Preserve momentum through the roll
        motor->SetDesiredDirection(intent.worldMoveDirection);
        FacingResolver::SnapFaceDirection(controller->GetOwner(), intent.worldMoveDirection);
    }
    else
    {
        // Hard Land (Extreme falls / no input)
        actionIdx = 2;
    }

    controller->getAnimBlackboard().actionIndex = actionIdx;
    anim->PlaySlot(AnimSlot::Landing, true);
    m_timer = anim->GetSlotDuration(AnimSlot::Landing);
}

void PlayerLanding::Update(PlayerControllerComponent* controller, float dt)
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

    // Evasion or Attack branches out of landing recovery
    if (m_canCancel && intent.bDashTriggered)
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::DashEvade));
        return;
    }
    if (m_canCancel && intent.bAttackPressed)
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AttackPrimary));
        return;
    }

    if ((m_canCancel && playerWantsToMove) || m_timer <= 0.0f)
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
    }
}

void PlayerLanding::Exit(PlayerControllerComponent* controller) {}

void PlayerParkourWall::Enter(PlayerControllerComponent*) {}
void PlayerParkourWall::Update(PlayerControllerComponent*, float) {}
void PlayerParkourWall::Exit(PlayerControllerComponent*) {}

void PlayerDashEvade::Enter(PlayerControllerComponent* controller)
{
    m_canCancel = false;
    const auto& intent{ controller->GetIntent() };

    m_dashDir = FacingResolver::ResolveDirectionOrCurrentFacing(controller->GetOwner(), intent.worldMoveDirection);

    if (auto* motor{ controller->GetMovement() })
    {
        motor->AddImpulse(DirectX::XMFLOAT3{
            m_dashDir.x * DASH_IMPULSE_FORCE,
            0.0f,
            m_dashDir.y * DASH_IMPULSE_FORCE
            });

        // Air Dash specific logic
        if (!motor->isGrounded())
        {
            motor->SetVerticalVelocity(0.0f);
            // Mark that the player has consumed their air dash
            controller->getAnimBlackboard().setFlag(AnimFlag::has_air_dashed, true);
        }
    }

    FacingResolver::SnapFaceDirection(controller->GetOwner(), m_dashDir);

    if (auto* anim{ controller->GetAnimation() })
    {
        anim->PlaySlot(AnimSlot::DashEvade, true);
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

    // Dash Jump: Jumping out of a dash transitions immediately to air traversal while keeping momentum
    if (m_canCancel && intent.bJumpTriggered && controller->GetMovement()->isGrounded())
    {
        controller->GetMovement()->Jump(6.5f);
        controller->getAnimBlackboard().actionIndex = 0; // Jump Takeoff
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AirTraversal));
        return;
    }

    const float inputSq = (intent.moveVector.x * intent.moveVector.x) + (intent.moveVector.y * intent.moveVector.y);
    const bool playerWantsToMove = (inputSq > 0.01f);

    // Dash Attack Cancel
    if (m_canCancel && intent.bAttackPressed)
    {
        if (controller->GetMovement()->isGrounded())
        {
            controller->getAnimBlackboard().actionIndex = 2; // Ground Dash Attack
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AttackContextual));
        }
        else
        {
            controller->getAnimBlackboard().actionIndex = 4; // Air Dash Attack
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AttackAerial));
        }
        return;
    }

    // Movement Cancel (ONLY ALLOWED ON GROUND)
    if (m_canCancel && playerWantsToMove && controller->GetMovement()->isGrounded())
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
        return;
    }

    if (m_timer <= 0.0f)
    {
        // Re-evaluate ground status to prevent falling into locomotion if an Air Dash finishes mid-air
        if (!controller->GetMovement()->isGrounded())
        {
            controller->getAnimBlackboard().actionIndex = 1; // Fall Loop
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AirTraversal));
        }
        else
        {
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
        }
    }
}

void PlayerDashEvade::Exit(PlayerControllerComponent* controller) {}

// COMBAT (GROUND)
void PlayerAttackPrimary::Enter(PlayerControllerComponent* controller)
{
    m_comboIndex = 0;

    const auto& intent = controller->GetIntent();
    m_lungeDirection = FacingResolver::ResolveDirectionOrCurrentFacing(controller->GetOwner(), intent.worldMoveDirection);

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

    // Snap once per combo hit — each hit locks facing for its own duration; consecutive
    // hits can still turn the character between swings since m_lungeDirection is
    // recomputed per hit below, but never mid-swing.
    FacingResolver::SnapFaceDirection(controller->GetOwner(), m_lungeDirection);

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

    // Evasion Cancel (Respect Air Dash Limits)
    if (m_canCancel && intent.bDashTriggered)
    {
        auto* motor = controller->GetMovement();
        auto& blackboard = controller->getAnimBlackboard();

        // Block the evasion if airborne and the air dash is already consumed
        if (!motor->isGrounded() && blackboard.getFlag(AnimFlag::has_air_dashed))
        {
            // Do nothing, lockout applies.
        }
        else
        {
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::DashEvade));
            return;
        }
    }

    // Combo Chaining
    if (m_canCancel && m_attackBufferTimer > 0.0f)
    {
        if (m_comboIndex < 3)
        {
            m_comboIndex++;
            m_lungeDirection = FacingResolver::ResolveDirectionOrCurrentFacing(controller->GetOwner(), intent.worldMoveDirection);
            PlayCurrentAttack(controller);
            return;
        }
    }

    // Natural Exit (Context-Aware)
    if (m_exitTimer <= 0.0f)
    {
        if (controller->GetMovement()->isGrounded())
        {
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
        }
        else
        {
            controller->getAnimBlackboard().actionIndex = 1; // Fall Loop
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AirTraversal));
        }
    }
}
void PlayerAttackPrimary::Exit(PlayerControllerComponent* controller) {}

// Placeholders for Expanded Combat
void PlayerAttackContextual::Enter(PlayerControllerComponent* controller)
{
    const auto& intent = controller->GetIntent();
    m_lungeDirection = FacingResolver::ResolveDirectionOrCurrentFacing(controller->GetOwner(), intent.worldMoveDirection);

    if (auto* motor = controller->GetMovement())
    {
        motor->SetDesiredDirection({ 0.0f, 0.0f });
    }

    PlayCurrentAttack(controller);
}

void PlayerAttackContextual::PlayCurrentAttack(PlayerControllerComponent* controller) noexcept
{
    m_canCancel = false;
    m_attackBufferTimer = 0.0f;

    FacingResolver::SnapFaceDirection(controller->GetOwner(), m_lungeDirection);

    if (auto* anim = controller->GetAnimation())
    {
        anim->PlaySlot(AnimSlot::Attack_Contextual, true);
        m_exitTimer = anim->GetSlotDuration(AnimSlot::Attack_Contextual);
    }
}

void PlayerAttackContextual::Update(PlayerControllerComponent* controller, float dt)
{
    m_exitTimer -= dt;
    if (m_attackBufferTimer > 0.0f) m_attackBufferTimer -= dt;

    const auto& intent = controller->GetIntent();
    if (intent.bAttackPressed) m_attackBufferTimer = 0.25f;

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
                    m_lungeDirection.x * ev.payload, 0.0f, m_lungeDirection.y * ev.payload
                    });
            }
        }
    }

    // Evasion Cancel (Respect Air Limits)
    if (m_canCancel && intent.bDashTriggered)
    {
        auto* motor = controller->GetMovement();
        if (motor->isGrounded() || !controller->getAnimBlackboard().getFlag(AnimFlag::has_air_dashed))
        {
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::DashEvade));
            return;
        }
    }

    // Natural Exit (Context-Aware)
    if (m_exitTimer <= 0.0f)
    {
        if (controller->GetMovement()->isGrounded())
        {
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
        }
        else
        {
            controller->getAnimBlackboard().actionIndex = 1; // Fall Loop
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AirTraversal));
        }
    }
}

void PlayerAttackContextual::Exit(PlayerControllerComponent* controller) {}

void PlayerAttackDirectional::Enter(PlayerControllerComponent*) {}
void PlayerAttackDirectional::Update(PlayerControllerComponent*, float) {}
void PlayerAttackDirectional::Exit(PlayerControllerComponent*) {}

void PlayerAttackCharged::Enter(PlayerControllerComponent*) {}
void PlayerAttackCharged::Update(PlayerControllerComponent*, float) {}
void PlayerAttackCharged::Exit(PlayerControllerComponent*) {}

void PlayerAttackAerial::Enter(PlayerControllerComponent* controller)
{
    // The actionIndex (e.g., 4 for Air Dash Attack) was set by DashEvade before transitioning.
    // We only reset to 0 if it wasn't a contextual entry.
    auto& blackboard = controller->getAnimBlackboard();
    m_comboIndex = blackboard.actionIndex;

    const auto& intent = controller->GetIntent();
    m_lungeDirection = FacingResolver::ResolveDirectionOrCurrentFacing(controller->GetOwner(), intent.worldMoveDirection);

    PlayCurrentAttack(controller);
}

void PlayerAttackAerial::PlayCurrentAttack(PlayerControllerComponent* controller) noexcept
{
    m_canCancel = false;
    m_attackBufferTimer = 0.0f;

    controller->getAnimBlackboard().actionIndex = m_comboIndex;
    FacingResolver::SnapFaceDirection(controller->GetOwner(), m_lungeDirection);

    if (auto* anim = controller->GetAnimation())
    {
        anim->PlaySlot(AnimSlot::Attack_Aerial, true);
        m_exitTimer = anim->GetSlotDuration(AnimSlot::Attack_Aerial);
    }
}

void PlayerAttackAerial::Update(PlayerControllerComponent* controller, float dt)
{
    auto* motor = controller->GetMovement();
    if (!motor) return;

    // AERIAL SPECIFIC: If the player hits the ground mid-swing, abort the attack instantly
    if (motor->isGrounded())
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
        return;
    }

    m_exitTimer -= dt;
    if (m_attackBufferTimer > 0.0f) m_attackBufferTimer -= dt;

    const auto& intent = controller->GetIntent();
    if (intent.bAttackPressed) m_attackBufferTimer = 0.25f;

    for (const auto& ev : controller->GetAnimation()->GetFiredEvents())
    {
        if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::CancelWindow_Open))
        {
            m_canCancel = true;
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Lunge_Impulse))
        {
            // Apply horizontal mid-air lunge
            motor->AddImpulse(DirectX::XMFLOAT3{
                m_lungeDirection.x * ev.payload, 0.0f, m_lungeDirection.y * ev.payload
                });
        }
    }

    // Evasion Cancel (Respect Air Limits)
    if (m_canCancel && intent.bDashTriggered)
    {
        if (!controller->getAnimBlackboard().getFlag(AnimFlag::has_air_dashed))
        {
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::DashEvade));
            return;
        }
    }

    // Aerial Combo Chaining
    if (m_canCancel && m_attackBufferTimer > 0.0f)
    {
        // Standard air combo is usually 2 or 3 hits (Indices 0, 1, 2). 
        // If we came from a dash attack (Index 4), don't combo further.
        if (m_comboIndex < 2)
        {
            m_comboIndex++;
            m_lungeDirection = FacingResolver::ResolveDirectionOrCurrentFacing(controller->GetOwner(), intent.worldMoveDirection);
            PlayCurrentAttack(controller);
            return;
        }
    }

    // Natural Exit back to falling
    if (m_exitTimer <= 0.0f)
    {
        controller->getAnimBlackboard().actionIndex = 1; // Fall Loop
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AirTraversal));
    }
}

void PlayerAttackAerial::Exit(PlayerControllerComponent* controller) {}

// DEFENSE & REACTION
void PlayerParryCounter::Enter(PlayerControllerComponent*) {}
void PlayerParryCounter::Update(PlayerControllerComponent*, float) {}
void PlayerParryCounter::Exit(PlayerControllerComponent*) {}

void PlayerHitReact::Enter(PlayerControllerComponent*) {}
void PlayerHitReact::Update(PlayerControllerComponent*, float) {}
void PlayerHitReact::Exit(PlayerControllerComponent*) {}