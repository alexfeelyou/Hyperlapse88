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

    // Seamless Momentum Sync:
    // By pulling live velocity the exact frame we enter Locomotion, we prevent 
    // stale 0.0m/s data from incorrectly triggering the Run_Fast_Start animation
    // when exiting a high-speed Pivot Turn.
    const DirectX::XMFLOAT3 vel{ motor->GetTotalVelocity() };
    blackboard.groundSpeed = std::sqrt((vel.x * vel.x) + (vel.z * vel.z));

    const auto& intent{ controller->GetIntent() };
    const float inputSq{ (intent.moveVector.x * intent.moveVector.x) + (intent.moveVector.y * intent.moveVector.y) };

    // Smart Entry Transient
    // Start animations (Walk_Start, Run_Start) are authored from a static Idle pose.
    // If the player is already moving fast (> 2.5m/s Walk Threshold) due to 
    // momentum injected by a PivotTurn or Landing, we bypass the start 
    // animation entirely to prevent snapping back to an idle-takeoff pose.
    const bool isStartingFromSlow = blackboard.groundSpeed <= 2.5f;

    if (inputSq > 0.01f && isStartingFromSlow)
    {
        m_wasActivelyMoving = false; // Force the Update loop to natively catch the 0->1 transition this frame
        m_startTimer = 0.0f;
    }
    else
    {
        m_wasActivelyMoving = (inputSq > 0.01f);
        m_startTimer = 0.0f;
        anim->PlaySlot(AnimSlot::Locomotion);
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
        const float probeDistance{ capsule ? (capsule->GetConfig().stepOffset + 0.3f) : 0.4f };
        const bool isGroundDirectlyBelow{ capsule && capsule->HasGroundBelow(probeDistance) };

        if (!isGroundDirectlyBelow)
        {
            m_fallTimer += dt;
            if (m_fallTimer > 0.10f) // A tiny 100ms debounce for jagged geometry seams
            {
                int actionIdx{ 2 }; // Index 2: Jump_Loop
                if (controller->getAnimBlackboard().getFlag(AnimFlag::is_combat_active)) actionIdx += 3; // Index 5: Combat Loop

                controller->getAnimBlackboard().actionIndex = actionIdx;
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

        // Momentum Injection:
        // Convert current locomotion speed into a ballistic impulse upon taking flight.
        if (blackboard.groundSpeed > 2.5f)
        {
            const float boost{ blackboard.groundSpeed * motor->GetConfig().jumpForwardImpulse };
            motor->AddImpulse({ intent.worldMoveDirection.x * boost, 0.0f, intent.worldMoveDirection.y * boost });
        }

        // 0 = Idle Jump, 1 = Forward Jump
        int actionIdx{ blackboard.groundSpeed > 2.5f ? 1 : 0 };
        if (blackboard.getFlag(AnimFlag::is_combat_active)) actionIdx += 3;

        blackboard.actionIndex = actionIdx;
        blackboard.currentJumps = 1;

        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AirTraversal));
        return;
    }

    // Ground Actions (Dash / Attack)
    if (intent.bDashTriggered)
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::DashEvade));
        return;
    }
    // Must be moving fast enough (above walk threshold) to initiate a slide
    // Uses bSlideTriggered to demand a fresh button press, preventing infinite slide loops
    if (intent.bSlideTriggered && controller->getAnimBlackboard().groundSpeed > 2.5f)
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Slide));
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
    // Aligned to 2.5f (Walk Threshold) so walking seamlessly slides to idle, 
    // and running triggers the heavy stop animations.
    if (!isActivelyMoving && blackboard.groundSpeed > 2.5f && motor->isGrounded())
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Stop));
        return;
    }

    // Universal Start Trigger
    // Captures the exact moment input transitions from inactive to active.
    // Velocity thresholding is now securely handled in the Enter() phase to allow explosive sprint bursts.
    if (isActivelyMoving && !m_wasActivelyMoving && m_startTimer <= 0.0f)
    {
        // Digital Sprint Override (Animation):
        // If the sprint button is held, we unconditionally bypass the analog walk threshold.
        const bool isWalking{ !intent.bSprintHeld && (inputSq < 0.25f) };

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

    // Digital Sprint Override (Physics):
    // If the sprint button is held, ignore partial analog stick tilts. Force the input 
    // magnitude to 1.0 (normalized) so the kinematic motor accelerates to full sprint speed.
    DirectX::XMFLOAT2 appliedDirection{ intent.worldMoveDirection };
    if (intent.bSprintHeld && isActivelyMoving)
    {
        const float mag{ std::sqrt(inputSq) }; // isActivelyMoving guarantees inputSq > 0.01f, preventing div-by-zero
        appliedDirection.x /= mag;
        appliedDirection.y /= mag;
    }

    motor->SetSprinting(intent.bSprintHeld && isActivelyMoving);
    motor->SetDesiredDirection(appliedDirection);

    const DirectX::XMFLOAT3 velocity{ motor->GetTotalVelocity() };
    const float actualSpeed{ std::sqrt((velocity.x * velocity.x) + (velocity.z * velocity.z)) };

    // Sprint Pivot Turn Trigger
    // If holding sprint, moving at sprint speeds, and the player yanks the stick backwards (>135 degrees),
    // trigger the skidding Pivot Turn instead of smoothly circling around.
    if (intent.bSprintHeld && actualSpeed > motor->GetConfig().maxRunSpeed + 1.0f && inputSq > 0.25f)
    {
        const float velNormX{ velocity.x / actualSpeed };
        const float velNormZ{ velocity.z / actualSpeed };
        const float turnDot{ (velNormX * intent.worldMoveDirection.x) + (velNormZ * intent.worldMoveDirection.y) };

        if (turnDot < -0.7f) // Approx 135-degree turnaround threshold
        {
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::PivotTurn));
            return;
        }
    }

    blackboard.groundSpeed = actualSpeed;
    blackboard.verticalVelocity = velocity.y;
    blackboard.setFlag(AnimFlag::is_grounded, motor->isGrounded());

    // Facing target calculation 
    DirectX::XMFLOAT2 faceTargetXZ{ intent.worldMoveDirection };
    if (blackboard.getFlag(AnimFlag::is_strafing))
    {
        const float cameraYawRad{ OrbitCameraDriverComponent::GetActiveYawRadians() };
        faceTargetXZ = { std::sin(cameraYawRad), std::cos(cameraYawRad) };
    }

    // Deadzone Facing Lock: Only rotate if the player is actively providing input.
    // This prevents the character from snapping to a default 0,0 direction when the stick is released.
    if (isActivelyMoving || blackboard.getFlag(AnimFlag::is_strafing))
    {
        constexpr float turnRateDegPerSec{ 720.0f };
        FacingResolver::SmoothFaceDirection(controller->GetOwner(), faceTargetXZ, turnRateDegPerSec, dt);
    }

    // ANIMATION DISPATCH
    if (auto* anim{ controller->GetAnimation() })
    {
        if (m_startTimer > 0.0f)
        {
            m_startTimer -= dt;

            // AAA Input Grace Window (Deferred Action Upgrading):
            // Analog sticks take 3-4 frames to physically travel from center to edge.
            // We evaluate the stick magnitude and sprint intent during the first 200ms,
            // seamlessly upgrading Walk -> Run -> Fast Run to prevent getting trapped.
            if (anim->GetCurrentTimer() <= 0.20f)
            {
                int targetIdx{ blackboard.actionIndex };

                if (intent.bSprintHeld)
                {
                    targetIdx = blackboard.getFlag(AnimFlag::is_combat_active) ? 5 : 2; // Fast Run
                }
                else if (inputSq >= 0.25f)
                {
                    targetIdx = blackboard.getFlag(AnimFlag::is_combat_active) ? 4 : 1; // Standard Run
                }

                // Only allow upward scaling (prevent downgrading if they briefly fumble the stick)
                if (targetIdx > blackboard.actionIndex)
                {
                    blackboard.actionIndex = targetIdx;
                    anim->PlaySlot(AnimSlot::Locomotion_Start, true); // Force a smooth blend
                    m_startTimer = anim->GetSlotDuration(AnimSlot::Locomotion_Start);
                }
            }
            else
            {
                // Dynamic Early-Out (Post-Grace Window Abort):
                // If the player drastically ramps up their input (e.g., slamming Sprint) after the 
                // 200ms grace window, do NOT trap them in a slow Walk_Start while physics accelerate 
                // to 15m/s. Abort the start transient entirely and drop directly into the Locomotion 
                // 1D Blend Tree, which perfectly matches continuous animation frames to physical speed.
                int intentIdx{ blackboard.actionIndex };
                if (intent.bSprintHeld) intentIdx = blackboard.getFlag(AnimFlag::is_combat_active) ? 5 : 2;
                else if (inputSq >= 0.25f) intentIdx = blackboard.getFlag(AnimFlag::is_combat_active) ? 4 : 1;

                if (intentIdx > blackboard.actionIndex)
                {
                    m_startTimer = 0.0f; // Abort instantly
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
    m_wasActivelyMoving = isActivelyMoving;
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

    // AAA Cycle-Phase Foot-Plant Selector
    // Assuming standard AAA authoring: 0.0 - 0.5 = Left Foot Forward | 0.5 - 1.0 = Right Foot Forward.
    // (If FBX cycles start on the Right foot, invert the > to < below).
    const float phase{ anim->GetCurrentPhase() };
    const bool isRightFootForward{ phase >= 0.5f };

    int actionIdx{ 0 };
    if (blackboard.groundSpeed < 3.5f)
    {
        actionIdx = isRightFootForward ? 1 : 0; // Walk Stop (L/R)
    }
    else if (blackboard.groundSpeed > 10.0f)
    {
        actionIdx = 4; // Fast Stop (Generic, no L/R variance)
    }
    else
    {
        actionIdx = isRightFootForward ? 3 : 2; // Run Stop (L/R)
    }

    // Offset index by 5 if weapons are drawn (Combat block starts at index 5)
    if (blackboard.getFlag(AnimFlag::is_combat_active))
    {
        actionIdx += 5;
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
        // Analog Deadzone Bypass (Pivot Turn Injection):
        // If the player crossed the deadzone and immediately ripped the stick backward, 
        // inject the Pivot Turn trigger directly here. This prevents the state machine 
        // from rapid-firing Stop -> Locomotion -> PivotTurn in 3 frames.
        if (motor)
        {
            const DirectX::XMFLOAT3 vel{ motor->GetTotalVelocity() };
            const float actualSpeed{ std::sqrt((vel.x * vel.x) + (vel.z * vel.z)) };

            // AAA Sprint-Only Restriction (Deadzone Bypass)
            if (intent.bSprintHeld && actualSpeed > motor->GetConfig().maxRunSpeed + 1.0f)
            {
                const float velNormX{ vel.x / actualSpeed };
                const float velNormZ{ vel.z / actualSpeed };
                const float turnDot{ (velNormX * intent.worldMoveDirection.x) + (velNormZ * intent.worldMoveDirection.y) };

                if (turnDot < -0.7f)
                {
                    controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::PivotTurn));
                    return;
                }
            }
        }

        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
        return;
    }

    // NATURAL EXIT
    // We must wait for BOTH the animation to finish AND the capsule to drop below the Run threshold.
    // Synced perfectly to 2.5f to prevent the "Double Stop" bug when returning to Locomotion.
    if (m_stopTimer <= 0.0f && blackboard.groundSpeed <= 2.5f)
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
    }
}

void PlayerStop::Exit(PlayerControllerComponent* controller)
{
    // No explicit cleanup required; state overrides handle the transition cleanly.
}

// PLAYER PIVOT TURN
void PlayerPivotTurn::Enter(PlayerControllerComponent* controller)
{
    auto* motor{ controller->GetMovement() };
    auto* anim{ controller->GetAnimation() };
    if (!motor || !anim) return;

    auto& blackboard{ controller->getAnimBlackboard() };
    const auto& intent{ controller->GetIntent() };

    const DirectX::XMFLOAT3 vel{ motor->GetTotalVelocity() };

    // Calculate 2D Cross Product to determine if the stick was pulled over the Left or Right shoulder
    const float cross{ (vel.x * intent.worldMoveDirection.y) - (vel.z * intent.worldMoveDirection.x) };

    // Graph Selector: 0 = Left, 1 = Right, 2 = Combat Left, 3 = Combat Right
    int actionIdx{ cross > 0.0f ? 0 : 1 };
    if (blackboard.getFlag(AnimFlag::is_combat_active)) actionIdx += 2;

    blackboard.actionIndex = actionIdx;
    anim->PlaySlot(AnimSlot::PivotTurn, true);

    // Cut intentional locomotion drive so the player physically "skids" to a halt via drag
    motor->SetDesiredDirection({ 0.0f, 0.0f });
}

void PlayerPivotTurn::Update(PlayerControllerComponent* controller, float dt)
{
    auto* anim{ controller->GetAnimation() };
    auto* motor{ controller->GetMovement() };
    if (!anim || !motor) return;

    bool canCancel{ false };
    for (const auto& ev : anim->GetFiredEvents())
    {
        if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::CancelWindow_Open)) canCancel = true;
    }

    const auto& intent{ controller->GetIntent() };
    const float inputSq{ (intent.moveVector.x * intent.moveVector.x) + (intent.moveVector.y * intent.moveVector.y) };

    // A Pivot Turn animation is a mathematically perfect 180-degree flip.
    // If we snap the capsule to the raw analog stick vector (e.g., 170 degrees), 
    // the mesh will look dislocated. We must strictly lock the exit facing to 
    // exactly 180 degrees from the entry capsule yaw to match the model
    auto ApplyExitFacing = [&]() -> DirectX::XMFLOAT2 {
        const float backwardYawRad{ DirectX::XMConvertToRadians(controller->GetOwner()->GetRotation().y + 180.0f) };
        const DirectX::XMFLOAT2 perfectExitDir{ std::sin(backwardYawRad), std::cos(backwardYawRad) };
        FacingResolver::SnapFaceDirection(controller->GetOwner(), perfectExitDir);
        return perfectExitDir;
        };

    // Allow dodging/dashing out of the skid once the animation permits it
    if (canCancel && intent.bDashTriggered)
    {
        ApplyExitFacing();
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::DashEvade));
        anim->Update(0.25f); // Flush Slerp buffer
        return;
    }

    // Seamless Momentum Transfer:
    // Once the skid animation finishes, return control to Locomotion.
    if (anim->GetCurrentTimer() >= anim->GetSlotDuration(AnimSlot::PivotTurn) - 0.05f)
    {
        const DirectX::XMFLOAT2 exitDir{ ApplyExitFacing() };

        // Seamless Momentum Transfer:
        // If the player holds any movement input at the end of the pivot, inject physical 
        // momentum perfectly along the 180-degree exit vector. This forces groundSpeed > 2.5f 
        // on the next frame, explicitly bypassing the Locomotion_Start clip for a fluid exit.
        if (inputSq > 0.01f)
        {
            const float boost{ intent.bSprintHeld ? motor->GetConfig().sprintSpeed : motor->GetConfig().maxRunSpeed };
            motor->AddImpulse({ exitDir.x * boost, 0.0f, exitDir.y * boost });
        }

        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));

        // Zero-Blend Injection Hack:
        // Since we mathematically snapped the physical capsule 180 degrees, the AnimationComponent's 
        // 0.2s local-space spherical blend will clash and cause a 360-degree visual twist pop. 
        // We forcefully fast-forward the AnimationComponent by 0.25s right now to instantly flush 
        // the transition buffer, guaranteeing a perfectly seamless 0-frame pop.
        anim->Update(0.25f);
    }
}

void PlayerPivotTurn::Exit(PlayerControllerComponent* controller) {}

void PlayerSlide::Enter(PlayerControllerComponent* controller)
{
    auto* motor{ controller->GetMovement() };
    auto* anim{ controller->GetAnimation() };
    if (!motor || !anim) return;

    m_canCancel = false;
    m_phase = SlideSubPhase::Entry_Drop;

    if (auto* capsule = controller->GetOwner()->GetComponent<CapsuleColliderComponent>())
    {
        m_standingHeight = capsule->GetConfig().height;
        m_standingRadius = capsule->GetConfig().radius;

        // Squash to a compact 0.1m cylinder to slide under obstacles
        capsule->ResizeFootAnchored(m_standingRadius, 0.1f);
    }

    motor->SetDesiredDirection({ 0.0f, 0.0f });
    motor->HaltMomentum(1.0f); // Strip lingering sprint momentum so the slide impulse has pure authority
    motor->SetFrictionMultiplier(motor->GetConfig().slideFrictionMultiplier);

    const auto& intent{ controller->GetIntent() };
    m_slideDir = FacingResolver::ResolveDirectionOrCurrentFacing(controller->GetOwner(), intent.worldMoveDirection);
    FacingResolver::SnapFaceDirection(controller->GetOwner(), m_slideDir);

    const float impulse = motor->GetConfig().slideImpulse;
    motor->AddImpulse({ m_slideDir.x * impulse, 0.0f, m_slideDir.y * impulse });

    anim->SetPlaybackSpeed(1.0f); // Playback starts normally
    anim->PlaySlot(Engine::Animation::AnimSlot::Slide, true);
}

void PlayerSlide::Update(PlayerControllerComponent* controller, float dt)
{
    const auto& intent{ controller->GetIntent() };
    auto* motor{ controller->GetMovement() };
    auto* anim{ controller->GetAnimation() };
    if (!motor || !anim) return;

    auto& blackboard{ controller->getAnimBlackboard() };
    const DirectX::XMFLOAT3 vel{ motor->GetTotalVelocity() };
    blackboard.groundSpeed = std::sqrt((vel.x * vel.x) + (vel.z * vel.z));
    blackboard.verticalVelocity = vel.y;
    blackboard.setFlag(AnimFlag::is_grounded, motor->isGrounded());

    // EVENT LISTENER 
    for (const auto& ev : anim->GetFiredEvents())
    {
        if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::CancelWindow_Open))
        {
            m_canCancel = true;
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Slide_GlidePose) && m_phase == SlideSubPhase::Entry_Drop)
        {
            m_phase = SlideSubPhase::Sustain_Glide;
        }
    }

    auto* capsule = controller->GetOwner()->GetComponent<CapsuleColliderComponent>();
    const bool hasClearance = capsule ? capsule->HasCeilingClearance(m_standingHeight) : true;

    // JUMP / EDGE CANCELS 
    // Allowed absolutely anytime before the slow Exit_Recovery phase, but must respect the Entry Drop commitment
    if (m_canCancel && intent.bJumpTriggered && motor->isGrounded() && m_phase != SlideSubPhase::Exit_Recovery && hasClearance)
    {
        if (capsule) capsule->ResizeFootAnchored(m_standingRadius, m_standingHeight);
        motor->Jump(6.5f);
        if (blackboard.groundSpeed > 2.5f)
        {
            const float boost{ blackboard.groundSpeed * motor->GetConfig().jumpForwardImpulse };
            DirectX::XMFLOAT2 faceDir = FacingResolver::ResolveDirectionOrCurrentFacing(controller->GetOwner(), intent.worldMoveDirection);
            motor->AddImpulse({ faceDir.x * boost, 0.0f, faceDir.y * boost });
        }

        blackboard.actionIndex = 1;
        blackboard.currentJumps = 1;
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AirTraversal));
        return;
    }

    if (!motor->isGrounded() && vel.y < -1.5f)
    {
        if (capsule) capsule->ResizeFootAnchored(m_standingRadius, m_standingHeight);
        blackboard.actionIndex = 1;
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AirTraversal));
        return;
    }

    // Cancel Slide into Locomotion
    const float inputSq{ (intent.moveVector.x * intent.moveVector.x) + (intent.moveVector.y * intent.moveVector.y) };
    if (m_canCancel && !intent.bSlideHeld && blackboard.groundSpeed > 2.5f && m_phase != SlideSubPhase::Exit_Recovery && hasClearance)
    {
        if (capsule) capsule->ResizeFootAnchored(m_standingRadius, m_standingHeight);
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
        return;
    }

    // STEERING (Visual Facing & Physical Trajectory)
    // Smoothly rotates character facing and redirects the physical sliding impulse
    // to match directional input, providing turning authority identical to Locomotion
    motor->SetDesiredDirection({ 0.0f, 0.0f });

    if (inputSq > 0.01f && m_phase != SlideSubPhase::Exit_Recovery)
    {
        constexpr float turnRateDegPerSec{ 720.0f }; // Identical turn rate to Locomotion
        FacingResolver::SmoothFaceDirection(controller->GetOwner(), intent.worldMoveDirection, turnRateDegPerSec, dt);

        const float yawRad{ DirectX::XMConvertToRadians(controller->GetOwner()->GetRotation().y) };
        m_slideDir = { std::sin(yawRad), std::cos(yawRad) };

        // Redirect the active slide velocity along the updated facing vector.
        // Maintains exact physical velocity decay from slideFrictionMultiplier while carving.
        if (blackboard.groundSpeed > 0.05f)
        {
            motor->HaltMomentum(1.0f);
            motor->AddImpulse({ m_slideDir.x * blackboard.groundSpeed, 0.0f, m_slideDir.y * blackboard.groundSpeed });
        }
    }

    // BEHAVIOR
    if (m_phase == SlideSubPhase::Sustain_Glide)
    {
        // As long as the player holds the button and has momentum, freeze the animation at the low posture
        if ((intent.bSlideHeld && blackboard.groundSpeed > 1.5f) || !hasClearance)
        {
            anim->SetPlaybackSpeed(0.0f); // CLAMP POSE (0 updates per frame)

            // Anti-stuck safety: if they run out of speed completely but are trapped beneath a ceiling
            if (!hasClearance && blackboard.groundSpeed < 1.0f)
            {
                motor->HaltMomentum(1.0f);
                motor->AddImpulse({ m_slideDir.x * 2.0f, 0.0f, m_slideDir.y * 2.0f });
            }
        }
        else
        {
            // Friction killed our speed OR we released the button while moving slow -> Fall into recovery
            m_phase = SlideSubPhase::Exit_Recovery;
            anim->SetPlaybackSpeed(1.0f); // UNFREEZE
            motor->SetFrictionMultiplier(1.0f); // Restore natural friction to stop cleanly
        }
    }
    else if (m_phase == SlideSubPhase::Exit_Recovery)
    {
        // Wait for the GetUp animation to naturally conclude
        if (anim->GetCurrentTimer() >= anim->GetSlotDuration(Engine::Animation::AnimSlot::Slide) - 0.05f)
        {
            if (capsule) capsule->ResizeFootAnchored(m_standingRadius, m_standingHeight);
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
            return;
        }
    }
}

void PlayerSlide::Exit(PlayerControllerComponent* controller)
{
    if (auto* motor{ controller->GetMovement() }) motor->SetFrictionMultiplier(1.0f);
    if (auto* anim{ controller->GetAnimation() }) anim->SetPlaybackSpeed(1.0f);

    // Safety fallback: if violently interrupted (e.g., getting hit or falling), guarantee restore
    if (auto* capsule = controller->GetOwner()->GetComponent<CapsuleColliderComponent>())
    {
        if (capsule->GetConfig().height < m_standingHeight)
        {
            capsule->ResizeFootAnchored(m_standingRadius, m_standingHeight);
        }
    }
}

// AERIAL & PARKOUR
void PlayerAirTraversal::Enter(PlayerControllerComponent* controller)
{
    m_airTimer = 0.0f;
    m_isAcrobatic = false;
    m_canCancelAcrobatic = true; // Unlocked by default for standard falling

    // State-Agnostic Forfeiture: If the player entered the air state without 
    // spending a jump (e.g., walking or dodging off a cliff), we consume 
    // their first jump automatically so they are restricted to just one double jump.
    auto& blackboard{ controller->getAnimBlackboard() };
    if (blackboard.currentJumps == 0)
    {
        blackboard.currentJumps = 1;
    }

    if (auto* motor{ controller->GetMovement() })
    {
        // Drastically reduce drag in the air so jump impulses sail freely
        motor->SetFrictionMultiplier(0.15f);
    }

    if (auto* anim{ controller->GetAnimation() })
    {
        anim->PlaySlot(AnimSlot::AirTraversal);
    }
}

void PlayerAirTraversal::Update(PlayerControllerComponent* controller, float dt)
{
    m_airTimer += dt; // Tick the air timer

    if (m_isAcrobatic)
    {
        for (const auto& ev : controller->GetAnimation()->GetFiredEvents())
        {
            if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::CancelWindow_Open)) m_canCancelAcrobatic = true;
        }
    }

    const auto& intent{ controller->GetIntent() };
    auto* motor{ controller->GetMovement() };
    if (!motor) return;

    auto& blackboard{ controller->getAnimBlackboard() };
    const DirectX::XMFLOAT3 velocity{ motor->GetTotalVelocity() };

    blackboard.groundSpeed = std::sqrt((velocity.x * velocity.x) + (velocity.z * velocity.z));
    blackboard.verticalVelocity = velocity.y;
    blackboard.setFlag(AnimFlag::is_grounded, motor->isGrounded());

    // Hang-Time Masking: Wait until velocity is definitively downward (-1.5 m/s) 
    // before triggering the Fall Loop. This allows the Jump_Start_F animation 
    // to resolve its forward lean and naturally straighten out at the apex,
    // hiding the posture pop when blending into the static Jump_Loop.
    if (velocity.y < -1.5f && !m_isAcrobatic)
    {
        const int fallIdx{ blackboard.getFlag(AnimFlag::is_combat_active) ? 5 : 2 };
        if (blackboard.actionIndex != fallIdx) blackboard.actionIndex = fallIdx;
    }

    if (motor->isGrounded() && velocity.y <= 0.05f)
    {
        const float inputSq{ (intent.moveVector.x * intent.moveVector.x) + (intent.moveVector.y * intent.moveVector.y) };

        // Slide Intercept: If the player lands with high momentum and is holding or tapping slide,
        // completely bypass the Roll Landing and seamlessly transition into a Slide.
        // (-28.0f check ensures Hard Landings still take absolute priority).
        if (inputSq > 0.01f && blackboard.groundSpeed > 2.5f && velocity.y > -28.0f && (intent.bSlideHeld || intent.bSlideTriggered))
        {
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Slide));
            return;
        }

        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Landing));
        return;
    }

    // THE DANGER ZONE LOCKOUT: 
    // If falling faster than -24 m/s (approaching the -28 m/s hard land), disable panic actions.
    const bool inDangerZone = (velocity.y < -24.0f);

    // Air Dash (Only allowed if not in danger zone, and has not dashed yet)
    // If performing an acrobatic double jump, enforce the animation cancel window
    if (intent.bDashTriggered && !inDangerZone && !blackboard.getFlag(AnimFlag::has_air_dashed) && (!m_isAcrobatic || m_canCancelAcrobatic))
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
        m_canCancelAcrobatic = false; // Lock out the dash until the CancelWindow_Open event fires
        blackboard.actionIndex = blackboard.getFlag(AnimFlag::is_combat_active) ? 1 : 0;

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
        // Proportional Blend-Out: Trigger the transition when 90% of the clip has played.
        // This prevents fast-playing clips (e.g. 2.0x speed) from being abruptly truncated 
        // by an absolute time margin, while still ensuring a fluid crossfade into the fall loop.
        const float duration{ anim->GetSlotDuration(AnimSlot::Jump_Acrobatic) };
        if (m_isAcrobatic && anim->GetCurrentTimer() >= (duration * 0.90f))
        {
            m_isAcrobatic = false;

            // CRITICAL: Force the blackboard into the Fall Loop (Index 2 or 5).
            // If the flip finishes while still moving upwards, we must not accidentally route 
            // back to the grounded Jump Takeoff (Index 0 or 1) while mid-air.
            blackboard.actionIndex = blackboard.getFlag(AnimFlag::is_combat_active) ? 5 : 2;
        }

        if (!m_isAcrobatic)
        {
            anim->PlaySlot(AnimSlot::AirTraversal);
        }
    }
}

void PlayerAirTraversal::Exit(PlayerControllerComponent* controller)
{
    if (auto* motor{ controller->GetMovement() })
    {
        // Restore standard friction upon landing
        motor->SetFrictionMultiplier(1.0f);
    }
}

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
    int actionIdx{ 0 };

    // Hard drops always take absolute priority to stagger the player
    if (terminalVelocity <= -28.0f)
    {
        actionIdx = 2; // Hard Land
    }
    // High-Momentum Roll: Bypass Y-velocity limits entirely.
    // If the player lands while actively running/dashing (> 2.5m/s), 
    // seamlessly transition into a forward roll to maintain combat pacing.
    else if (holdingMove && controller->getAnimBlackboard().groundSpeed > 2.5f)
    {
        actionIdx = 1; // Roll Land

        // Preserve input direction momentum through the roll
        motor->SetDesiredDirection(intent.worldMoveDirection);
        FacingResolver::SnapFaceDirection(controller->GetOwner(), intent.worldMoveDirection);
    }
    else
    {
        // Soft Land (Stationary jumps, slow walking, or no input)
        actionIdx = 0;
    }

    if (controller->getAnimBlackboard().getFlag(AnimFlag::is_combat_active)) actionIdx += 3;

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
    FacingResolver::SnapFaceDirection(controller->GetOwner(), m_dashDir);

    auto* anim{ controller->GetAnimation() };
    auto* motor{ controller->GetMovement() };

    if (anim)
    {
        // Route the Selector Graph (0: Normal, 1: Air, 2: Combat, 3: Air Combat)
        int actionIdx{ (motor && motor->isGrounded()) ? 0 : 1 };
        if (controller->getAnimBlackboard().getFlag(AnimFlag::is_combat_active))
        {
            actionIdx += 2;
        }
        controller->getAnimBlackboard().actionIndex = actionIdx;

        anim->PlaySlot(AnimSlot::DashEvade, true);
        m_timer = anim->GetSlotDuration(AnimSlot::DashEvade);
    }

    if (motor)
    {
        motor->SetDesiredDirection({ 0.0f, 0.0f });

        if (motor->isGrounded())
        {
            // Impulse-Decay: Distance = InitialVelocity / Drag => InitialVelocity = Distance * Drag
            // This provides an explosive initial burst that perfectly decelerates to the target distance.
            const float dashImpulse{ motor->GetConfig().dashGroundDistance * motor->GetConfig().impulseDrag };
            motor->AddImpulse(DirectX::XMFLOAT3{
                m_dashDir.x * dashImpulse,
                0.0f,
                m_dashDir.y * dashImpulse
                });
        }
        else
        {
            const float airImpulse{ motor->GetConfig().dashAirImpulse };
            motor->AddImpulse(DirectX::XMFLOAT3{
                m_dashDir.x * airImpulse,
                0.0f,
                m_dashDir.y * airImpulse
                });

            // Air Dash specific logic
            motor->SetVerticalVelocity(0.0f);
            controller->getAnimBlackboard().setFlag(AnimFlag::has_air_dashed, true);
        }
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
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Movement_Halt))
        {
            if (auto* motor{ controller->GetMovement() })
            {
                motor->HaltMomentum(ev.payload);
            }
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
        // Seamless Momentum Transfer:
        // Inject immediate physical momentum in the requested direction. This forces 
        // groundSpeed > 2.5f upon entering Locomotion, explicitly bypassing the 
        // Locomotion_Start takeoff clip and snapping the pose fluidly into a run.
        auto* motor = controller->GetMovement();
        const float boost{ intent.bSprintHeld ? motor->GetConfig().sprintSpeed : motor->GetConfig().maxRunSpeed };
        motor->AddImpulse(DirectX::XMFLOAT3{
            intent.worldMoveDirection.x * boost,
            0.0f,
            intent.worldMoveDirection.y * boost
            });

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

void PlayerDashEvade::Exit(PlayerControllerComponent* controller)
{
    if (auto* motor{ controller->GetMovement() })
    {
        motor->ClearKinematicOverride();
    }
}

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
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Movement_Halt))
        {
            if (auto* motor = controller->GetMovement())
            {
                motor->HaltMomentum(ev.payload);
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
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Movement_Halt))
        {
            if (auto* motor = controller->GetMovement())
            {
                motor->HaltMomentum(ev.payload);
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
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Movement_Halt))
        {
            motor->HaltMomentum(ev.payload);
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