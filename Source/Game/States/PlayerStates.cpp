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
    auto* motor{ controller->GetMovement() };
    auto* anim{ controller->GetAnimation() };
    if (!motor || !anim) return;

    auto& blackboard{ controller->getAnimBlackboard() };

    // SAFE RESET: Only replenish air actions if physically touching the ground
    if (motor->isGrounded())
    {
        blackboard.setFlag(AnimFlag::has_air_dashed, false);
        blackboard.currentJumps = 0;
    }

    // BUG FIX: Do NOT trigger start animations here. 
    // Enter() is only called when landing from a jump or settling back to Idle after a Stop.
    m_startTimer = 0.0f;
    anim->PlaySlot(AnimSlot::Locomotion);
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
            m_fallTimer = 0.0f; // Bounding down stairs; suppress the fall
        }
    }
    else
    {
        m_fallTimer = 0.0f;
    }

    if (intent.bJumpTriggered && motor->isGrounded())
    {
        constexpr float JUMP_FORCE{ 6.5f };
        motor->Jump(JUMP_FORCE);

        auto& blackboard{ controller->getAnimBlackboard() };
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

    const float inputSq{ (intent.moveVector.x * intent.moveVector.x) + (intent.moveVector.y * intent.moveVector.y) };
    const bool isActivelyMoving{ inputSq > 0.01f };
    auto& blackboard{ controller->getAnimBlackboard() };

    // BRAKING CHECK: Player released the stick while moving fast
    if (!isActivelyMoving && blackboard.groundSpeed > 1.5f && motor->isGrounded())
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Stop));
        return;
    }

    // EDGE TRIGGER BUG FIX: Trigger start transient ONLY when player presses WASD from a standstill
    if (isActivelyMoving && blackboard.groundSpeed < 0.1f && m_startTimer <= 0.0f)
    {
        // Note: Keyboard input pushes inputSq straight to 1.0. Walk animations typically require a gamepad.
        const bool isWalking{ inputSq < 0.25f };

        int actionIdx{ 1 }; // Default to Run Start
        if (isWalking) actionIdx = 0;
        else if (intent.bSprintHeld) actionIdx = 2;

        if (blackboard.getFlag(AnimFlag::is_combat_active))
        {
            actionIdx += 3;
        }

        blackboard.actionIndex = actionIdx;
        if (auto* anim{ controller->GetAnimation() })
        {
            anim->PlaySlot(AnimSlot::Locomotion_Start, true);
            m_startTimer = anim->GetSlotDuration(AnimSlot::Locomotion_Start);
        }
    }

    motor->SetSprinting(intent.bSprintHeld && isActivelyMoving);
    motor->SetDesiredDirection(intent.worldMoveDirection);

    const DirectX::XMFLOAT3 velocity{ motor->GetTotalVelocity() };
    blackboard.groundSpeed = std::sqrt((velocity.x * velocity.x) + (velocity.z * velocity.z));
    blackboard.verticalVelocity = velocity.y;
    blackboard.setFlag(AnimFlag::is_grounded, motor->isGrounded());

    // Facing
    constexpr float turnRateDegPerSec{ 720.0f };
    DirectX::XMFLOAT2 faceTargetXZ{ intent.worldMoveDirection };
    if (blackboard.getFlag(AnimFlag::is_strafing))
    {
        const float cameraYawRad{ OrbitCameraDriverComponent::GetActiveYawRadians() };
        faceTargetXZ = { std::sin(cameraYawRad), std::cos(cameraYawRad) };
    }
    FacingResolver::SmoothFaceDirection(controller->GetOwner(), faceTargetXZ, turnRateDegPerSec, dt);

    // ANIMATION DISPATCH
    if (auto* anim{ controller->GetAnimation() })
    {
        if (m_startTimer > 0.0f)
        {
            m_startTimer -= dt;

            // AAA Input Grace Window (Deferred Action Upgrading):
            // Allow late sprint inputs to seamlessly upgrade the start animation during the
            // first 200ms of movement. Prevents strict frame-perfect input frustration.
            if (intent.bSprintHeld && anim->GetCurrentTimer() <= 0.20f)
            {
                const int targetIdx{ blackboard.getFlag(AnimFlag::is_combat_active) ? 5 : 2 };
                if (blackboard.actionIndex != targetIdx)
                {
                    blackboard.actionIndex = targetIdx;
                    anim->PlaySlot(AnimSlot::Locomotion_Start, true); // Force a smooth blend
                    m_startTimer = anim->GetSlotDuration(AnimSlot::Locomotion_Start);
                }
            }

            // Instantly abort start clip if player sharply reverses direction (>90 deg)
            const float turnDot{ (faceTargetXZ.x * intent.worldMoveDirection.x) + (faceTargetXZ.y * intent.worldMoveDirection.y) };
            if (turnDot < 0.0f) m_startTimer = 0.0f;
        }

        // Fall into the 1D loop once the start timer expires or is aborted
        if (m_startTimer <= 0.0f)
        {
            anim->PlaySlot(AnimSlot::Locomotion);
        }
    }
}

void PlayerLocomotion::Exit(PlayerControllerComponent* controller)
{
    // No explicit cleanup required
}

// PLAYER STOP (BRAKING & DECELERATION)
void PlayerStop::Enter(PlayerControllerComponent* controller)
{
    auto* motor{ controller->GetMovement() };
    auto* anim{ controller->GetAnimation() };
    if (!motor || !anim) return;

    // Cut engine propulsion. CharacterMovementComponent's impulseDrag will naturally slide the capsule to a halt.
    motor->SetDesiredDirection({ 0.0f, 0.0f });

    auto& blackboard{ controller->getAnimBlackboard() };

    // Threshold mapping 
    int actionIdx{ 1 }; // Default: Run Stop
    if (blackboard.groundSpeed < 3.5f) actionIdx = 0;        // Walk Stop
    else if (blackboard.groundSpeed > 10.0f) actionIdx = 2; // Fast Stop

    // Offset index by 3 if weapons are drawn
    if (blackboard.getFlag(AnimFlag::is_combat_active))
    {
        actionIdx += 3;
    }

    blackboard.actionIndex = actionIdx;
    anim->PlaySlot(AnimSlot::Locomotion_Stop, true);

    m_stopTimer = anim->GetSlotDuration(AnimSlot::Locomotion_Stop);
}

void PlayerStop::Update(PlayerControllerComponent* controller, float dt)
{
    m_stopTimer -= dt;

    const auto& intent{ controller->GetIntent() };
    auto* motor{ controller->GetMovement() };
    auto& blackboard{ controller->getAnimBlackboard() };

    // Continuously update the Blackboard physics during the Stop.
    // Without this, groundSpeed remains permanently frozen at your entry sprint speed, 
    // causing an infinite loop back into PlayerStop when exiting.
    if (motor)
    {
        const DirectX::XMFLOAT3 velocity{ motor->GetTotalVelocity() };
        blackboard.groundSpeed = std::sqrt((velocity.x * velocity.x) + (velocity.z * velocity.z));
        blackboard.verticalVelocity = velocity.y;
        blackboard.setFlag(AnimFlag::is_grounded, motor->isGrounded());

        // Safety Fallback: If you slide off a ledge while braking, transition to falling
        if (!motor->isGrounded())
        {
            blackboard.actionIndex = 1; // Fall Loop
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AirTraversal));
            return;
        }
    }

    // PLATINUM-STYLE INTERRUPTS: The player can cancel a stop animation instantly at any time.
    if (intent.bDashTriggered)
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::DashEvade));
        return;
    }
    if (intent.bJumpTriggered)
    {
        // Require ground check to prevent jumping off a ledge during a slide
        if (motor && motor->isGrounded())
        {
            motor->Jump(6.5f);
            blackboard.actionIndex = 0; // Jump Takeoff
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AirTraversal));
            return;
        }
    }
    if (intent.bAttackPressed)
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AttackPrimary));
        return;
    }

    // MOVEMENT CANCEL: If the player presses WASD again, instantly snap back into Locomotion.
    const float inputSq{ (intent.moveVector.x * intent.moveVector.x) + (intent.moveVector.y * intent.moveVector.y) };
    if (inputSq > 0.01f)
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
        return;
    }

    // [CRITICAL FIX 2] NATURAL EXIT
    // We must wait for BOTH the animation to finish AND the capsule to physically stop sliding.
    // If we transition out while still sliding at 2.0m/s, Locomotion will re-trigger the Stop state.
    if (m_stopTimer <= 0.0f && blackboard.groundSpeed < 0.1f)
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
    }
}

void PlayerStop::Exit(PlayerControllerComponent* controller)
{
    // No explicit cleanup required; state overrides handle the transition cleanly.
}

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