#include "AIControllerComponent.h"
#include "AnimationComponent.h"
#include "CapsuleColliderComponent.h"
#include "CharacterMovementComponent.h"
#include "CombatDirector.h"
#include "CombatMath.h"
#include "FacingResolver.h"
#include "GameObject.h"
#include "HealthComponent.h"
#include "MeleeHitboxComponent.h"
#include "OrbitCameraDriverComponent.h"
#include "PlayerStates.h"
#include "StateMachine.h"
#include "VirtualCameraComponent.h"

using namespace Engine::Animation;

namespace
{
    // 攻撃ステート終了・中断時に判定を安全かつ確実に閉じるインラインヘルパー
    void ClosePlayerHitbox(PlayerControllerComponent* controller) noexcept
    {
        if (!controller || !controller->GetOwner()) return;

        // Check if MeleeHitboxComponent is attached directly to the player
        if (auto* hitbox{ controller->GetOwner()->GetComponent<MeleeHitboxComponent>() })
        {
            hitbox->CloseHitbox();
            return;
        }

        // Check if MeleeHitboxComponent is attached to the weapon socket child
        for (const auto& child : controller->GetOwner()->GetChildren())
        {
            if (child)
            {
                if (auto* hitbox{ child->GetComponent<MeleeHitboxComponent>() })
                {
                    hitbox->CloseHitbox();
                    return;
                }
            }
        }
    }

    // Evaluates directional combat intent (Command Normals). Returns Target Node Index, or -1 if none.
    [[nodiscard]] int EvaluateCommandNormal(const GameObject* owner, const InputIntent& intent, bool requireFreshInput = true) noexcept
    {
        if (!owner) return -1;

        // Raw Up/W Input always triggers Up Attack inside combat, but only on fresh input in locomotion.
        // intent.moveVector.y maps directly to the raw W key or Left Stick Y-axis (+1.0 = Up).
        constexpr float s_forwardFreshnessThreshold{ 0.25f }; // Synchronized with 250ms combat attack buffer
        if (intent.moveVector.y > 0.4f)
        {
            if (!requireFreshInput || intent.forwardIntentTimer <= s_forwardFreshnessThreshold) return 0; // Node 0: Attack_Up_Floor_to_Air_02
        }

        // Back Attack triggers when world input strictly opposes the character's physical facing.
        // E.g., Character faces Right (+X), Player presses Left (-X). Dot product is -1.0.
        const float yawRad{ DirectX::XMConvertToRadians(owner->GetRotation().y) };
        const DirectX::XMFLOAT2 charFwd{ std::sin(yawRad), std::cos(yawRad) };
        const float fwdDot{ (intent.worldMoveDirection.x * charFwd.x) + (intent.worldMoveDirection.y * charFwd.y) };

        // Stricter dot product threshold (-0.85f) ensures only ~150-180 degree back-pulls trigger the retreat slash,
        // preventing lateral or diagonal (-0.707f) inputs from unintentionally hijacking the move.
        if (fwdDot < -0.85f) return 1; // Node 1

        return -1; // Neutral
    }

    // Evaluates predictive soft-lock distance clamping for airborne attack lunges.
    // Preserves the 1.3m sweet-spot gap when locked onto a juggled target,
    // and provides a capped forward nudge during air whiffs.
    void ApplyAirLungeKinematics(
        PlayerControllerComponent* controller,
        CharacterMovementComponent* motor,
        const DirectX::XMFLOAT2& facingDir,
        const float payload,
        const float dt) noexcept
    {
        if (!controller || !controller->GetOwner() || !motor) return;

        const DirectX::XMFLOAT3 pPos{ controller->GetOwner()->GetPosition() };
        auto* targetEnemy{ CombatDirector::Instance().FindNearestAirborneEnemy(pPos, 4.0f) };

        if (targetEnemy && targetEnemy->GetOwner())
        {
            const DirectX::XMFLOAT3 ePos{ targetEnemy->GetOwner()->GetPosition() };
            const float dx{ ePos.x - pPos.x };
            const float dz{ ePos.z - pPos.z };
            const float distSq{ (dx * dx) + (dz * dz) };
            constexpr float sweetSpotDist{ 1.3f };

            const float distXZ{ std::sqrt(distSq) };
            const float invDist{ (distXZ > 0.0001f) ? (1.0f / distXZ) : 0.0f };
            const DirectX::XMFLOAT2 lockDir{ dx * invDist, dz * invDist };

            // Auto-converge horizontal facing toward victim
            FacingResolver::SnapFaceDirection(controller->GetOwner(), lockDir);

            if (distXZ > sweetSpotDist)
            {
                // Solve v0 = Gap * Drag so Player glides smoothly to the sweet spot boundary
                const float gap{ distXZ - sweetSpotDist };
                const float drag{ (std::max)(0.1f, motor->GetConfig().impulseDrag) };
                const float requiredImpulse{ gap * drag };
                const float appliedSpeed{ (std::min)(payload, requiredImpulse) };

                motor->HaltMomentum(1.0f);
                motor->AddImpulse(DirectX::XMFLOAT3{ lockDir.x * appliedSpeed, 0.0f, lockDir.y * appliedSpeed });
            }
            else
            {
                // Already at or inside sweet spot: clamp player to prevent overshooting behind victim
                motor->HaltMomentum(1.0f);

                // If cramped (< 1.3m), push victim backward along lock direction to preserve blade sweep arc
                const float pushGap{ sweetSpotDist - distXZ };
                if (pushGap > 0.02f)
                {
                    if (auto* eMotor{ targetEnemy->GetOwner()->GetComponent<CharacterMovementComponent>() })
                    {
                        const float eDrag{ (std::max)(0.1f, eMotor->GetConfig().impulseDrag) };
                        eMotor->HaltMomentum(1.0f);
                        eMotor->AddImpulse(DirectX::XMFLOAT3{ lockDir.x * (pushGap * eDrag), 0.0f, lockDir.y * (pushGap * eDrag) });
                    }
                }
            }

            // Synchronize victim elevation directly to attacker's aerial combat plane
            if (auto* eCapsule{ targetEnemy->GetOwner()->GetComponent<CapsuleColliderComponent>() })
            {
                const float heightDiff{ pPos.y - ePos.y };
                if (std::abs(heightDiff) > 0.03f && std::abs(heightDiff) < 3.0f)
                {
                    const DirectX::XMFLOAT3 alignedPos{ ePos.x, pPos.y, ePos.z };
                    targetEnemy->GetOwner()->SetPosition(alignedPos);
                    eCapsule->Teleport(alignedPos);
                }
            }
        }
        else
        {
            // Empty-air swing: Execute the full authored timeline impulse as a cosmetic forward glide
            // so the character visibly propels forward matching the attack animation.
            motor->HaltMomentum(1.0f);
            motor->AddImpulse(DirectX::XMFLOAT3{ facingDir.x * payload, 0.0f, facingDir.y * payload });
        }
    }

    // Soft-Lock Target Selector (XZ Ground Sector)
    [[nodiscard]] AIControllerComponent* FindGroundSoftLockTarget(
        const GameObject* player,
        const DirectX::XMFLOAT2& referenceDirXZ,
        const float maxDistance = 5.0f) noexcept
    {
        if (!player) return nullptr;

        const float refLenSq{ (referenceDirXZ.x * referenceDirXZ.x) + (referenceDirXZ.y * referenceDirXZ.y) };
        if (refLenSq < 0.0001f) return nullptr;

        const auto& enemies{ CombatDirector::Instance().GetEnemies() };
        if (enemies.empty()) return nullptr;

        const DirectX::XMFLOAT3 pPos{ player->GetPosition() };
        AIControllerComponent* bestEnemy{ nullptr };
        float bestScore{ -1.0f };

        // Melee Sector: 75-degree cone (cos(75 deg) ~= 0.2588f) allows intuitive lock-on without jarring right-angle snaps
        constexpr float minDot{ 0.2588f };
        const float maxDistSq{ maxDistance * maxDistance };

        for (auto* enemy : enemies)
        {
            if (!enemy || !enemy->GetOwner() || enemy->GetOwner()->IsDestroyed()) continue;
            if (enemy->GetCurrentState() == EnemyState::dead) continue;

            const auto* health{ enemy->GetOwner()->GetComponent<HealthComponent>() };
            if (health && health->IsDead()) continue;

            const DirectX::XMFLOAT3 ePos{ enemy->GetOwner()->GetPosition() };

            // Reject vertical outliers (wall lurkers or enemies over ledge cliffs)
            const float dy{ ePos.y - pPos.y };
            if (std::abs(dy) > 2.0f) continue;

            const float dx{ ePos.x - pPos.x };
            const float dz{ ePos.z - pPos.z };
            const float distSq{ (dx * dx) + (dz * dz) };
            if (distSq < 0.0001f || distSq > maxDistSq) continue;

            const float dist{ std::sqrt(distSq) };
            const float invDist{ 1.0f / dist };
            const float dirX{ dx * invDist };
            const float dirZ{ dz * invDist };

            const float dot{ (referenceDirXZ.x * dirX) + (referenceDirXZ.y * dirZ) };
            if (dot < minDot) continue;

            // Weighted Scoring: 65% directional aim alignment, 35% proximity weighting
            const float normDist{ dist / maxDistance };
            const float score{ (dot * 0.65f) + ((1.0f - normDist) * 0.35f) };

            if (score > bestScore)
            {
                bestScore = score;
                bestEnemy = enemy;
            }
        }

        return bestEnemy;
    }

    // Resolves initial attack facing: snaps to soft-locked enemy center if in cone, else preserves raw input
    [[nodiscard]] DirectX::XMFLOAT2 ResolveSoftLockOrFacingDirection(
        const GameObject* player,
        const DirectX::XMFLOAT2& rawWorldDirectionXZ,
        const float maxDistance = 5.0f) noexcept
    {
        DirectX::XMFLOAT2 dir{ FacingResolver::ResolveDirectionOrCurrentFacing(player, rawWorldDirectionXZ) };
        if (const auto* target{ FindGroundSoftLockTarget(player, dir, maxDistance) })
        {
            const DirectX::XMFLOAT3 pPos{ player->GetPosition() };
            const DirectX::XMFLOAT3 ePos{ target->GetOwner()->GetPosition() };
            const float dx{ ePos.x - pPos.x };
            const float dz{ ePos.z - pPos.z };
            const float distSq{ (dx * dx) + (dz * dz) };
            if (distSq > 0.0001f)
            {
                const float invDist{ 1.0f / std::sqrt(distSq) };
                dir = { dx * invDist, dz * invDist };
            }
        }
        return dir;
    }

    // Evaluates dynamic gap-closing kinematics targeting enemy capsule center
    void ApplyGroundLungeKinematics(
        PlayerControllerComponent* controller,
        CharacterMovementComponent* motor,
        DirectX::XMFLOAT2& inOutFacingDir,
        const float authoredPayload) noexcept
    {
        if (!controller || !controller->GetOwner() || !motor) return;

        auto* targetEnemy{ FindGroundSoftLockTarget(controller->GetOwner(), inOutFacingDir) };

        if (targetEnemy && targetEnemy->GetOwner())
        {
            const DirectX::XMFLOAT3 pPos{ controller->GetOwner()->GetPosition() };
            const DirectX::XMFLOAT3 ePos{ targetEnemy->GetOwner()->GetPosition() };
            const float dx{ ePos.x - pPos.x };
            const float dz{ ePos.z - pPos.z };
            const float distSq{ (dx * dx) + (dz * dz) };

            if (distSq > 0.0001f)
            {
                const float dist{ std::sqrt(distSq) };
                const float invDist{ 1.0f / dist };
                const DirectX::XMFLOAT2 lockDir{ dx * invDist, dz * invDist };

                // Align physical facing directly toward the target's capsule center
                inOutFacingDir = lockDir;
                FacingResolver::SnapFaceDirection(controller->GetOwner(), lockDir);

                float playerRadius{ 0.37f };
                if (const auto* pCapsule{ controller->GetOwner()->GetComponent<CapsuleColliderComponent>() })
                {
                    playerRadius = pCapsule->GetConfig().radius;
                }

                float enemyRadius{ 0.50f };
                if (const auto* eCapsule{ targetEnemy->GetOwner()->GetComponent<CapsuleColliderComponent>() })
                {
                    enemyRadius = eCapsule->GetConfig().radius;
                }

                // Optimal melee spacing: capsule radii sum + 35cm blade offset to cleave through capsule center
                const float sweetSpotDist{ playerRadius + enemyRadius + 0.35f };
                const float gap{ dist - sweetSpotDist };
                const float drag{ (std::max)(0.1f, motor->GetConfig().impulseDrag) };

                motor->HaltMomentum(1.0f);

                if (gap > 0.0f)
                {
                    // Analytic drag integration: v0 = gap * drag halts the capsule exactly at the sweet spot
                    const float requiredImpulse{ gap * drag };
                    const float appliedSpeed{ std::clamp(requiredImpulse, 0.0f, 30.0f) };
                    motor->AddImpulse(DirectX::XMFLOAT3{ lockDir.x * appliedSpeed, 0.0f, lockDir.y * appliedSpeed });
                }
                else
                {
                    // Multi-lunge support: already within sweet spot from prior lunge, apply a crisp forward step-in impulse for secondary strikes
                    const float stepInSpeed{ std::clamp(authoredPayload * 0.75f, 2.0f, 15.0f) };
                    motor->HaltMomentum(1.0f);
                    motor->AddImpulse(DirectX::XMFLOAT3{ lockDir.x * stepInSpeed, 0.0f, lockDir.y * stepInSpeed });
                }
                return;
            }
        }

        // Whiff / No Target: Maintain authored timeline impulse along current facing vector
        motor->HaltMomentum(1.0f);
        motor->AddImpulse(DirectX::XMFLOAT3{ inOutFacingDir.x * authoredPayload, 0.0f, inOutFacingDir.y * authoredPayload });
    }

    // Soft-Lock Target Selector for Aerial Plunge (Helmbreaker)
    // Searches for living enemies below the player regardless of enemy aggro or ring slots
    [[nodiscard]] AIControllerComponent* FindPlungeSoftLockTarget(
        const GameObject* player,
        const DirectX::XMFLOAT2& referenceDirXZ,
        const float maxHorizontalDist = 7.0f,
        const float maxDropHeight = 15.0f) noexcept
    {
        if (!player) return nullptr;

        const auto& enemies{ CombatDirector::Instance().GetEnemies() };
        if (enemies.empty()) return nullptr;

        const DirectX::XMFLOAT3 pPos{ player->GetPosition() };
        AIControllerComponent* bestEnemy{ nullptr };
        float bestScore{ -1.0f };

        // 70-degree cone (cos(70 deg) ~= 0.342f) for directional intent
        constexpr float minDot{ 0.342f };
        const float maxHorizDistSq{ maxHorizontalDist * maxHorizontalDist };

        for (auto* enemy : enemies)
        {
            if (!enemy || !enemy->GetOwner() || enemy->GetOwner()->IsDestroyed()) continue;
            if (enemy->GetCurrentState() == EnemyState::dead) continue;

            const auto* health{ enemy->GetOwner()->GetComponent<HealthComponent>() };
            if (health && health->IsDead()) continue;

            const DirectX::XMFLOAT3 ePos{ enemy->GetOwner()->GetPosition() };

            // Enemy must be below the player (or within 0.5m above foot level) and within drop reach
            const float heightDiff{ pPos.y - ePos.y };
            if (heightDiff < -0.5f || heightDiff > maxDropHeight) continue;

            const float dx{ ePos.x - pPos.x };
            const float dz{ ePos.z - pPos.z };
            const float distSq{ (dx * dx) + (dz * dz) };
            if (distSq > maxHorizDistSq) continue;

            // Direct overhead drop: if enemy is directly beneath player, assign top priority
            if (distSq < 0.25f)
            {
                const float score{ 10.0f - distSq };
                if (score > bestScore)
                {
                    bestScore = score;
                    bestEnemy = enemy;
                }
                continue;
            }

            const float dist{ std::sqrt(distSq) };
            const float invDist{ 1.0f / dist };
            const float dirX{ dx * invDist };
            const float dirZ{ dz * invDist };

            const float dot{ (referenceDirXZ.x * dirX) + (referenceDirXZ.y * dirZ) };
            if (dot < minDot) continue;

            // Weighted scoring: 60% directional alignment, 40% horizontal proximity
            const float normDist{ dist / maxHorizontalDist };
            const float score{ (dot * 0.60f) + ((1.0f - normDist) * 0.40f) };

            if (score > bestScore)
            {
                bestScore = score;
                bestEnemy = enemy;
            }
        }

        return bestEnemy;
    }

    // Resolves initial plunge facing: snaps to target beneath player if present
    [[nodiscard]] DirectX::XMFLOAT2 ResolvePlungeSoftLockOrFacingDirection(
        const GameObject* player,
        const DirectX::XMFLOAT2& rawWorldDirectionXZ,
        const float maxHorizontalDist = 7.0f) noexcept
    {
        DirectX::XMFLOAT2 dir{ FacingResolver::ResolveDirectionOrCurrentFacing(player, rawWorldDirectionXZ) };
        if (const auto* target{ FindPlungeSoftLockTarget(player, dir, maxHorizontalDist) })
        {
            const DirectX::XMFLOAT3 pPos{ player->GetPosition() };
            const DirectX::XMFLOAT3 ePos{ target->GetOwner()->GetPosition() };
            const float dx{ ePos.x - pPos.x };
            const float dz{ ePos.z - pPos.z };
            const float distSq{ (dx * dx) + (dz * dz) };
            if (distSq > 0.0001f)
            {
                const float invDist{ 1.0f / std::sqrt(distSq) };
                dir = { dx * invDist, dz * invDist };
            }
        }
        return dir;
    }

    // Solves ratio-matched horizontal impulse so diagonal drop naturally strikes target capsule center
    void ApplyPlungeLungeKinematics(
        PlayerControllerComponent* controller,
        CharacterMovementComponent* motor,
        DirectX::XMFLOAT2& inOutFacingDir,
        const float authoredPayload,
        const float downwardSpeed) noexcept
    {
        if (!controller || !controller->GetOwner() || !motor) return;

        auto* targetEnemy{ FindPlungeSoftLockTarget(controller->GetOwner(), inOutFacingDir) };

        if (targetEnemy && targetEnemy->GetOwner())
        {
            const DirectX::XMFLOAT3 pPos{ controller->GetOwner()->GetPosition() };
            const DirectX::XMFLOAT3 ePos{ targetEnemy->GetOwner()->GetPosition() };
            const float dx{ ePos.x - pPos.x };
            const float dz{ ePos.z - pPos.z };
            const float distSq{ (dx * dx) + (dz * dz) };

            const float heightDiff{ (std::max)(0.2f, pPos.y - ePos.y) };
            const float safeDownSpeed{ (std::max)(5.0f, downwardSpeed) };

            if (distSq > 0.0001f)
            {
                const float dist{ std::sqrt(distSq) };
                const float invDist{ 1.0f / dist };
                const DirectX::XMFLOAT2 lockDir{ dx * invDist, dz * invDist };

                inOutFacingDir = lockDir;
                FacingResolver::SnapFaceDirection(controller->GetOwner(), lockDir);

                float playerRadius{ 0.37f };
                if (const auto* pCapsule{ controller->GetOwner()->GetComponent<CapsuleColliderComponent>() })
                {
                    playerRadius = pCapsule->GetConfig().radius;
                }

                float enemyRadius{ 0.50f };
                if (const auto* eCapsule{ targetEnemy->GetOwner()->GetComponent<CapsuleColliderComponent>() })
                {
                    enemyRadius = eCapsule->GetConfig().radius;
                }

                // Sweet spot: radius sum + 15cm offset
                const float sweetSpotDist{ playerRadius + enemyRadius + 0.15f };
                const float gap{ dist - sweetSpotDist };

                motor->HaltMomentum(1.0f);

                if (gap > 0.0f)
                {
                    // Linear diagonal relation: V_xz = gap / (heightDiff / safeDownSpeed)
                    const float requiredSpeed{ gap * (safeDownSpeed / heightDiff) };
                    const float appliedSpeed{ std::clamp(requiredSpeed, 0.0f, 35.0f) };
                    motor->AddImpulse(DirectX::XMFLOAT3{ lockDir.x * appliedSpeed, 0.0f, lockDir.y * appliedSpeed });
                }
                return;
            }
        }

        // Whiff / No Target: Maintain authored timeline impulse along current facing vector
        motor->HaltMomentum(1.0f);
        motor->AddImpulse(DirectX::XMFLOAT3{ inOutFacingDir.x * authoredPayload, 0.0f, inOutFacingDir.y * authoredPayload });
    }
}

// GROUND & LOCOMOTION
void PlayerLocomotion::Enter(PlayerControllerComponent* controller)
{
    controller->ConsumePendingComboHit(); // Sanitize mailbox on returning to neutral
    auto* motor{ controller->GetMovement() };
    auto* anim{ controller->GetAnimation() };
    if (!motor || !anim) return;

    // Reset camera framing to neutral locomotion profile on returning to neutral
    if (auto* vcam{ OrbitCameraDriverComponent::GetActiveVirtualCamera() })
    {
        vcam->SetTargetProfile(CameraProfileType::locomotion);
    }

    auto& blackboard{ controller->getAnimBlackboard() };

    // SAFE RESET: Only replenish air actions if physically touching the ground
    if (motor->isGrounded())
    {
        blackboard.setFlag(AnimFlag::has_air_dashed, false);
        blackboard.setFlag(AnimFlag::has_air_attacked, false);
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

        // PREVENTIVE BUG: Sync the startup idle clip identically to the Combat stance
        m_wasCombatActive = blackboard.getFlag(AnimFlag::is_combat_active);
        
        // Keeps the transient selector offset primed for Starts/Stops
        int actionIdx = 0;
        if (m_wasCombatActive) actionIdx += 3;
        blackboard.actionIndex = actionIdx;

        anim->PlaySlot(m_wasCombatActive ? AnimSlot::Locomotion_Combat : AnimSlot::Locomotion);
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
        const float probeDistance{ capsule ? (capsule->GetConfig().stepOffset + 0.1f) : 0.4f };
        const bool isGroundDirectlyBelow{ capsule && capsule->HasGroundBelow(probeDistance) };

        m_fallTimer += dt;

        // Animation Debounce Bypass:
        // If there is no ground directly below, OR if the Kinematic Motor injected a definitive 
        // edge-drop ballistic velocity (< -1.5m/s), transition instantly.
        if (!isGroundDirectlyBelow || motor->GetVerticalVelocity() < -1.5f)
        {
            if (m_fallTimer > 0.10f || motor->GetVerticalVelocity() < -1.5f)
            {
                int actionIdx{ controller->getAnimBlackboard().getFlag(AnimFlag::is_combat_active) ? 5 : 2 };
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

    // Ground Actions (Dash / Attack / Buff)
    if (intent.bSkillTriggered)
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::SkillBuff));
        return;
    }

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
    if (intent.bHeavyAttackPressed)
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AttackCharged));
        return;
    }
    if (intent.bAttackPressed)
    {
        const int commandNormalIdx{ EvaluateCommandNormal(controller->GetOwner(), intent) };

        // High-Speed Flurry Intercept (Prioritized EXCEPT for explicitly requested Up-Attacks)
        // If the player presses Up + Attack, route cleanly to the launcher. 
        // Otherwise, intercept the primary attack button to execute the Flurry.
        if (controller->getAnimBlackboard().getFlag(AnimFlag::is_speed_buff_active) && commandNormalIdx != 0)
        {
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AttackSpeed));
            return;
        }

        // Sprint Attack takes highest priority gap-closing
        if (controller->getAnimBlackboard().groundSpeed > 2.5f && intent.bSprintHeld)
        {
            controller->SetPendingComboHit(-1); // Sprint Arbiter
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AttackContextual));
        }
        // Directional Command Normals (Up / Back)
        else if (commandNormalIdx != -1)
        {
            controller->SetPendingComboHit(commandNormalIdx);
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AttackDirectional));
        }
        // Neutral Ground Combo Poke
        else
        {
            controller->SetPendingComboHit(0);
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AttackPrimary));
        }
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

    const bool isSprinting{ intent.bSprintHeld && isActivelyMoving };
    motor->SetSprinting(isSprinting);
    motor->SetDesiredDirection(appliedDirection);

    // Dynamic lens push-in and FOV widen while sprinting
    if (auto* vcam{ OrbitCameraDriverComponent::GetActiveVirtualCamera() })
    {
        vcam->SetTargetProfile(isSprinting ? CameraProfileType::sprint : CameraProfileType::locomotion);
    }

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

    // Auto-Parkour Wall Entry (From Ground)
    if (intent.bSprintHeld && actualSpeed > 2.5f)
    {
        DirectX::XMFLOAT3 wNorm{};
        int wSide{ 0 };
        if (motor->DetectFlankingWall(wNorm, wSide))
        {
            const float inputDot{ (intent.worldMoveDirection.x * wNorm.x) + (intent.worldMoveDirection.y * wNorm.z) };
            const float yaw{ DirectX::XMConvertToRadians(controller->GetOwner()->GetRotation().y) };
            const DirectX::XMFLOAT3 fwd{ std::sin(yaw), 0.0f, std::cos(yaw) };

            // Mount wall if glancing along it and not actively pulling the stick away
            if (inputDot <= 0.25f && std::abs((fwd.x * wNorm.x) + (fwd.z * wNorm.z)) < 0.85f)
            {
                // Predictive Trajectory Clearance
                // Mounting from ground injects an upward parabolic impulse (5.5 m/s).
                // Ensure the wall extends continuously to the trajectory apex (h = v^2 / 2g) so the
                // character does not overshoot the wall height
                constexpr float mountLaunchVelocityY{ 5.5f };
                const float gravityMag{ std::abs(motor->GetConfig().gravity) };
                const float trajectoryApexHeight{ (gravityMag > 0.001f) ? ((mountLaunchVelocityY * mountLaunchVelocityY) / (2.0f * gravityMag)) : 1.54f };

                DirectX::XMFLOAT3 apexNorm{};
                int apexSide{ 0 };
                if (motor->DetectFlankingWall(apexNorm, apexSide, trajectoryApexHeight) && (apexSide == wSide))
                {
                    motor->SetVerticalVelocity(mountLaunchVelocityY); // Upward parabolic lift impulse
                    controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::ParkourWall));
                    return;
                }
            }
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

        // Seamlessly crossfade between relaxed and combat locomotion
        const bool isCombat = blackboard.getFlag(AnimFlag::is_combat_active);
        if (isCombat != m_wasCombatActive && m_startTimer <= 0.0f)
        {
            m_wasCombatActive = isCombat;
            blackboard.actionIndex = isCombat ? 3 : 0;
            anim->PlaySlot(isCombat ? AnimSlot::Locomotion_Combat : AnimSlot::Locomotion, true); // Force restart to trigger crossfade blend
        }

        // Fall into the 1D loop once the start timer expires or is aborted
        if (m_startTimer <= 0.0f)
        {
            anim->PlaySlot(isCombat ? AnimSlot::Locomotion_Combat : AnimSlot::Locomotion);
        }
    }
    m_wasActivelyMoving = isActivelyMoving;
}

void PlayerLocomotion::Exit(PlayerControllerComponent* controller)
{
    // Revert sprint zoom back to neutral locomotion framing when exiting locomotion
    if (auto* vcam{ OrbitCameraDriverComponent::GetActiveVirtualCamera() })
    {
        if (vcam->GetTargetProfile() == CameraProfileType::sprint)
        {
            vcam->SetTargetProfile(CameraProfileType::locomotion);
        }
    }
}

// PLAYER STOP (BRAKING & DECELERATION)
void PlayerStop::Enter(PlayerControllerComponent* controller)
{
    auto* motor{ controller->GetMovement() };
    auto* anim{ controller->GetAnimation() };
    if (!motor || !anim) return;

    // Cut engine propulsion. CharacterMovementComponent's impulseDrag will naturally slide the capsule to a halt.
    motor->SetDesiredDirection({ 0.0f, 0.0f });

    if (auto* vcam{ OrbitCameraDriverComponent::GetActiveVirtualCamera() })
    {
        vcam->SetTargetProfile(CameraProfileType::locomotion);
    }

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
            blackboard.actionIndex = blackboard.getFlag(AnimFlag::is_combat_active) ? 5 : 2; // Fall Loop
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
    if (intent.bHeavyAttackPressed)
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AttackCharged));
        return;
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
        if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Evade) ||
            ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Combo))
        {
            canCancel = true;
        }
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

    // Inject low ground-skimming camera vantage for the duration of the slide
    if (auto* vcam{ OrbitCameraDriverComponent::GetActiveVirtualCamera() })
    {
        vcam->SetTargetProfile(CameraProfileType::slide);
    }

    m_canCancelEvade = false;
    m_canCancelLocomotion = false;
    m_hasAuthoredEvadeCancel = false;
    m_phase = SlideSubPhase::Entry_Drop;

    if (const auto* activeNode{ anim->GetActiveAnimNode() })
    {
        for (const auto& ev : activeNode->events)
        {
            if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Evade))
            {
                m_hasAuthoredEvadeCancel = true;
                break;
            }
        }
    }

    // Squashing to the floor completely hides the weapon socket jump
    controller->getAnimBlackboard().setFlag(Engine::Animation::AnimFlag::is_combat_active, false);

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
        if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Evade))
        {
            m_canCancelEvade = true;
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Locomotion))
        {
            m_canCancelLocomotion = true;
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Combo))
        {
            if (!m_hasAuthoredEvadeCancel) m_canCancelEvade = true;
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Pose_HoldMarker) && m_phase == SlideSubPhase::Entry_Drop)
        {
            m_phase = SlideSubPhase::Sustain_Glide;
        }
    }

    auto* capsule = controller->GetOwner()->GetComponent<CapsuleColliderComponent>();
    const bool hasClearance = capsule ? capsule->HasCeilingClearance(m_standingHeight) : true;

    // JUMP / EDGE CANCELS 
    // Allowed absolutely anytime before the slow Exit_Recovery phase, but must respect the Entry Drop commitment
    if (m_canCancelEvade && intent.bJumpTriggered && motor->isGrounded() && m_phase != SlideSubPhase::Exit_Recovery && hasClearance)
    {
        if (capsule) capsule->ResizeFootAnchored(m_standingRadius, m_standingHeight);
        motor->Jump(6.5f);
        if (blackboard.groundSpeed > 2.5f)
        {
            const float boost{ blackboard.groundSpeed * motor->GetConfig().jumpForwardImpulse };
            DirectX::XMFLOAT2 faceDir{ FacingResolver::ResolveDirectionOrCurrentFacing(controller->GetOwner(), intent.worldMoveDirection) };
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
        blackboard.actionIndex = blackboard.getFlag(AnimFlag::is_combat_active) ? 5 : 2; // Fall Loop
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AirTraversal));
        return;
    }

    // Cancel Slide into Locomotion (Requires Locomotion permission or legacy release)
    const float inputSq{ (intent.moveVector.x * intent.moveVector.x) + (intent.moveVector.y * intent.moveVector.y) };
    const bool canExitToRun{ m_canCancelLocomotion || (!m_hasAuthoredEvadeCancel && m_canCancelEvade) };
    if (canExitToRun && !intent.bSlideHeld && blackboard.groundSpeed > 2.5f && m_phase != SlideSubPhase::Exit_Recovery && hasClearance)
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
    if (auto* vcam{ OrbitCameraDriverComponent::GetActiveVirtualCamera() })
    {
        if (vcam->GetTargetProfile() == CameraProfileType::slide)
        {
            vcam->SetTargetProfile(CameraProfileType::locomotion);
        }
    }

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
    controller->ConsumePendingComboHit(); // Sanitize mailbox on returning to neutral
    m_airTimer = 0.0f;
    m_isAcrobatic = false;
    m_canCancelAcrobaticEvade = true; // Unlocked by default for standard falling
    m_canCancelAcrobaticCombo = true;

    // Revert transient action camera profiles back to neutral locomotion
    if (auto* vcam{ OrbitCameraDriverComponent::GetActiveVirtualCamera() })
    {
        if (vcam->GetTargetProfile() != CameraProfileType::wall_run)
        {
            vcam->SetTargetProfile(CameraProfileType::locomotion);
        }
    }

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
            if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Evade))
            {
                m_canCancelAcrobaticEvade = true;
            }
            else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Combo))
            {
                m_canCancelAcrobaticCombo = true;
                m_canCancelAcrobaticEvade = true;
            }
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
        // Land-Jump / Bunny-Hop Intercept:
        // Preserves velocity and leaps immediately
        if (intent.bJumpTriggered && velocity.y > -28.0f)
        {
            constexpr float JUMP_FORCE{ 6.5f };
            motor->Jump(JUMP_FORCE);

            if (blackboard.groundSpeed > 2.5f)
            {
                const float boost{ blackboard.groundSpeed * motor->GetConfig().jumpForwardImpulse };
                motor->AddImpulse({ intent.worldMoveDirection.x * boost, 0.0f, intent.worldMoveDirection.y * boost });
            }

            int actionIdx{ blackboard.groundSpeed > 2.5f ? 1 : 0 };
            if (blackboard.getFlag(AnimFlag::is_combat_active)) actionIdx += 3;

            blackboard.actionIndex = actionIdx;
            blackboard.currentJumps = 1;
            m_airTimer = 0.0f;
            m_isAcrobatic = false;
            if (auto* anim{ controller->GetAnimation() })
            {
                anim->PlaySlot(AnimSlot::AirTraversal, true);
            }
            return;
        }

        const float inputSq{ (intent.moveVector.x * intent.moveVector.x) + (intent.moveVector.y * intent.moveVector.y) };
        const auto* capsule{ controller->GetOwner()->GetComponent<CapsuleColliderComponent>() };
        const float capsuleRadius{ capsule ? capsule->GetConfig().radius : 0.37f };

        const float hSpeedSq{ (velocity.x * velocity.x) + (velocity.z * velocity.z) };
        const DirectX::XMFLOAT2 velDir{
            hSpeedSq > 0.01f ? DirectX::XMFLOAT2{ velocity.x, velocity.z } : intent.worldMoveDirection
        };

        const float requiredSlideClearance{ (std::max)(3.5f, (blackboard.groundSpeed * 0.55f) + capsuleRadius + 0.5f) };
        const bool safeAlongVel{ capsule && capsule->HasGroundAhead(velDir, requiredSlideClearance) };
        const bool safeAlongIntent{ capsule && capsule->HasGroundAhead(intent.worldMoveDirection, requiredSlideClearance) };
        const bool hasSafeGroundForSlide{ safeAlongVel && safeAlongIntent };

        // Slide Intercept: If holding/tapping slide with high momentum and clearance exists
        if (hasSafeGroundForSlide && inputSq > 0.01f && blackboard.groundSpeed > 2.5f && velocity.y > -28.0f && (intent.bSlideHeld || intent.bSlideTriggered))
        {
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Slide));
            return;
        }

        // Hard landings always take absolute priority to stagger the player
        if (velocity.y <= -28.0f)
        {
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Landing));
            return;
        }

        // High-Momentum Dynamic Decision (Zone 1: Roll | Zone 2: Run / Locomotion | Zone 3: Skid Stop)
        if (inputSq > 0.01f && blackboard.groundSpeed > 2.5f)
        {
            const float speed{ blackboard.groundSpeed };
            // Roll requires full somersault clearance: speed * commit time (0.65s) + margin
            const float requiredRollClearance{ (std::max)(4.0f, (speed * 0.65f) + capsuleRadius + 1.2f) };
            // Locomotion running only requires foot planting space (1-2 steps ahead)
            const float requiredLocoClearance{ capsuleRadius + 0.35f };

            const bool safeRollVel{ capsule && capsule->HasGroundAhead(velDir, requiredRollClearance) };
            const bool safeRollIntent{ capsule && capsule->HasGroundAhead(intent.worldMoveDirection, requiredRollClearance) };
            const bool canSafelyRoll{ safeRollVel && safeRollIntent };

            if (canSafelyRoll)
            {
                // Zone 1: Ample runway -> execute full Landing Roll
                controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Landing));
                return;
            }

            const bool safeLocoVel{ capsule && capsule->HasGroundAhead(velDir, requiredLocoClearance) };
            const bool safeLocoIntent{ capsule && capsule->HasGroundAhead(intent.worldMoveDirection, requiredLocoClearance) };
            const bool canSafelyRun{ safeLocoVel && safeLocoIntent };

            if (canSafelyRun)
            {
                // Zone 2: Insufficient space for roll, but solid runway ahead
                controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
                return;
            }

            // Zone 3: Direct edge precipice (< 1 step of ground) -> Foot-skid brake at the edge
            motor->HaltMomentum(1.0f);
            controller->getAnimBlackboard().groundSpeed = 0.0f;
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Stop));
            return;
        }

        // Stationary / Slow landing
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Landing));
        return;
    }

    // Wall Run Entry Trigger
    if (intent.bSprintHeld && m_airTimer > 0.15f && (intent.moveVector.x != 0.0f || intent.moveVector.y != 0.0f))
    {
        DirectX::XMFLOAT3 wNorm{};
        int wSide{ 0 };
        if (motor->DetectFlankingWall(wNorm, wSide))
        {
            // Only mount if glancing along the wall plane, AND the user is NOT actively pulling the stick away
            const float inputDot{ (intent.worldMoveDirection.x * wNorm.x) + (intent.worldMoveDirection.y * wNorm.z) };

            const float yaw{ DirectX::XMConvertToRadians(controller->GetOwner()->GetRotation().y) };
            const DirectX::XMFLOAT3 fwd{ std::sin(yaw), 0.0f, std::cos(yaw) };

            // Anti-Spam Physics Gate:
            // Prevent infinite climbing on the same wall. If the player just wall-jumped, 
            // their velocity along the wall's normal will be strongly positive (e.g., +12m/s).
            // We physically reject the remount until outward momentum is killed (velDot <= 0.1f).
            const float velDot{ (velocity.x * wNorm.x) + (velocity.z * wNorm.z) };

            if (velDot <= 0.1f && inputDot <= 0.25f && std::abs((fwd.x * wNorm.x) + (fwd.z * wNorm.z)) < 0.85f)
            {
                controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::ParkourWall));
                return;
            }
        }
    }

    // THE DANGER ZONE LOCKOUT: 
    // If falling faster than -24 m/s (approaching the -28 m/s hard land), disable panic actions.
    const bool inDangerZone = (velocity.y < -24.0f);

    // Air Dash (Only allowed if not in danger zone, and has not dashed yet)
    // If performing an acrobatic double jump, enforce the animation cancel window
    if (intent.bDashTriggered && !inDangerZone && !blackboard.getFlag(AnimFlag::has_air_dashed) && (!m_isAcrobatic || m_canCancelAcrobaticEvade))
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::DashEvade));
        return;
    }

    // Aerial Light Attack (Standard Air Combo Trigger)
    // Locked behind has_air_attacked to prevent infinite map-traversal flying loops.
    if (intent.bAttackPressed && !inDangerZone && !blackboard.getFlag(AnimFlag::has_air_attacked) && (!m_isAcrobatic || m_canCancelAcrobaticCombo))
    {
        // Up-Attack natively handles air-to-air tracking; Flurry overrides standard attacks only.
        const int commandNormalIdx{ EvaluateCommandNormal(controller->GetOwner(), intent) };
        if (blackboard.getFlag(AnimFlag::is_speed_buff_active) && commandNormalIdx != 0)
        {
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AttackSpeed));
            return;
        }

        controller->SetPendingComboHit(0); // Explicitly start at Hit 1 (Index 0)
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AttackAerial));
        return;
    }

    // Plunge Attack Trigger
    if (intent.bHeavyAttackPressed && !inDangerZone)
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AttackPlunge));
        return;
    }

    // Double Jump Trigger (Requires 200ms debounce from previous jump, not in danger zone, max 2 jumps)
    if (intent.bJumpTriggered && !inDangerZone && blackboard.currentJumps < 2 && m_airTimer > 0.2f)
    {
        // Committing to an acrobatic mid-air flip silently sheathes the weapon
        blackboard.setFlag(AnimFlag::is_combat_active, false);

        blackboard.currentJumps++;
        motor->Jump(7.0f);

        m_isAcrobatic = true;
        m_canCancelAcrobaticEvade = false; // Lock out dash/attack until events fire
        m_canCancelAcrobaticCombo = false;
        blackboard.actionIndex = 0; // Forced neutral jump takeoff due to combat demotion

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
    m_canCancelCombo = false;
    m_canCancelEvade = false;
    m_canCancelLocomotion = false;
    m_hasAuthoredLocomotionCancel = false;
    m_isEdgeBraked = false;
    m_edgeDirection = { 0.0f, 0.0f };
    m_rollDirection = { 0.0f, 0.0f };

    auto* motor{ controller->GetMovement() };
    auto* anim{ controller->GetAnimation() };
    if (!motor || !anim) return;

    if (const auto* activeNode{ anim->GetActiveAnimNode() })
    {
        for (const auto& ev : activeNode->events)
        {
            if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Locomotion))
            {
                m_hasAuthoredLocomotionCancel = true;
                break;
            }
        }
    }

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
        motor->HaltMomentum(1.0f);
    }
    // Check predictive trajectory for edges/chasms before committing
    else if (holdingMove && controller->getAnimBlackboard().groundSpeed > 2.5f)
    {
        const auto* capsule{ controller->GetOwner()->GetComponent<CapsuleColliderComponent>() };
        const float speed{ controller->getAnimBlackboard().groundSpeed };
        const float capsuleRadius{ capsule ? capsule->GetConfig().radius : 0.37f };
        const float requiredRollClearance{ (std::max)(4.0f, (speed * 0.65f) + capsuleRadius + 1.2f) };
        const float requiredLocoClearance{ capsuleRadius + 0.35f };

        const DirectX::XMFLOAT3 currentVel{ motor->GetTotalVelocity() };
        const float hSpeedSq{ (currentVel.x * currentVel.x) + (currentVel.z * currentVel.z) };
        const DirectX::XMFLOAT2 velDir{
            hSpeedSq > 0.01f ? DirectX::XMFLOAT2{ currentVel.x, currentVel.z } : intent.worldMoveDirection
        };

        const bool safeRollVel{ capsule && capsule->HasGroundAhead(velDir, requiredRollClearance) };
        const bool safeRollIntent{ capsule && capsule->HasGroundAhead(intent.worldMoveDirection, requiredRollClearance) };
        const bool canSafelyRoll{ safeRollVel && safeRollIntent };

        if (canSafelyRoll)
        {
            // Zone 1: Safe for full somersault roll
            controller->getAnimBlackboard().setFlag(Engine::Animation::AnimFlag::is_combat_active, false);
            actionIdx = 1; // Roll Land
            m_rollDirection = intent.worldMoveDirection;

            motor->SetDesiredDirection(intent.worldMoveDirection);
            FacingResolver::SnapFaceDirection(controller->GetOwner(), intent.worldMoveDirection);

            if (auto* vcam{ OrbitCameraDriverComponent::GetActiveVirtualCamera() })
            {
                vcam->SetTargetProfile(CameraProfileType::roll_landing);
            }
        }
        else
        {
            if (auto* vcam{ OrbitCameraDriverComponent::GetActiveVirtualCamera() })
            {
                vcam->SetTargetProfile(CameraProfileType::locomotion);
            }

            const bool safeLocoVel{ capsule && capsule->HasGroundAhead(velDir, requiredLocoClearance) };
            const bool safeLocoIntent{ capsule && capsule->HasGroundAhead(intent.worldMoveDirection, requiredLocoClearance) };
            const bool canSafelyRun{ safeLocoVel && safeLocoIntent };

            if (canSafelyRun)
            {
                // Zone 2: Solid runway ahead -> hand off immediately to Locomotion without losing momentum
                controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
                return;
            }

            // Zone 3: Edge brink halt -> Foot-skid stop
            motor->HaltMomentum(1.0f);
            controller->getAnimBlackboard().groundSpeed = 0.0f;
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Stop));
            return;
        }
    }
    else
    {
        // Soft Land (Stationary jumps, slow walking, or no input)
        actionIdx = 0;
        motor->HaltMomentum(1.0f);
        controller->getAnimBlackboard().groundSpeed = 0.0f;

        if (auto* vcam{ OrbitCameraDriverComponent::GetActiveVirtualCamera() })
        {
            vcam->SetTargetProfile(CameraProfileType::locomotion);
        }
    }

    if (controller->getAnimBlackboard().getFlag(AnimFlag::is_combat_active)) actionIdx += 3;

    controller->getAnimBlackboard().actionIndex = actionIdx;
    anim->PlaySlot(AnimSlot::Landing, true);
    m_timer = anim->GetSlotDuration(AnimSlot::Landing);
}

void PlayerLanding::Update(PlayerControllerComponent* controller, float dt)
{
    m_timer -= dt;
    auto* motor{ controller->GetMovement() };
    auto* anim{ controller->GetAnimation() };

    for (const auto& ev : anim->GetFiredEvents())
    {
        if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Combo))
        {
            m_canCancelCombo = true;
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Evade))
        {
            m_canCancelEvade = true;
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Locomotion))
        {
            m_canCancelLocomotion = true;
        }
    }

    // Mid-Roll Dynamic Ledge Guard: If rolling and an edge suddenly appears within stopping distance
    const int currentAction{ controller->getAnimBlackboard().actionIndex };
    const bool isCurrentlyRolling{ (currentAction == 1 || currentAction == 4) };
    if (isCurrentlyRolling && motor)
    {
        const auto* capsule{ controller->GetOwner()->GetComponent<CapsuleColliderComponent>() };
        const float immediateCheckDist{ (capsule ? capsule->GetConfig().radius : 0.37f) + 0.85f };
        if (capsule && !capsule->HasGroundAhead(m_rollDirection, immediateCheckDist))
        {
            // Abort roll into emergency halt: Switch to PlayerStop (Locomotion_Stop)
            motor->HaltMomentum(1.0f);
            motor->SetDesiredDirection({ 0.0f, 0.0f });
            controller->getAnimBlackboard().groundSpeed = 0.0f;

            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Stop));
            return;
        }
    }

    const auto& intent{ controller->GetIntent() };
    const float inputSq{ (intent.moveVector.x * intent.moveVector.x) + (intent.moveVector.y * intent.moveVector.y) };
    const bool playerWantsToMove{ inputSq > 0.01f };

    // Backward-compatible fallback: Allow evasion and locomotion
    const bool canEvade{ m_canCancelEvade || (!m_hasAuthoredLocomotionCancel && m_canCancelCombo) };
    bool canMove{ m_canCancelLocomotion || (!m_hasAuthoredLocomotionCancel && m_canCancelCombo) };

    // Jump Cancel out of landing recovery (Land-Jump / Bunny-Hop)
    if (canEvade && intent.bJumpTriggered && motor && motor->isGrounded())
    {
        constexpr float JUMP_FORCE{ 6.5f };
        motor->Jump(JUMP_FORCE);

        if (controller->getAnimBlackboard().groundSpeed > 2.5f)
        {
            const float boost{ controller->getAnimBlackboard().groundSpeed * motor->GetConfig().jumpForwardImpulse };
            motor->AddImpulse({ intent.worldMoveDirection.x * boost, 0.0f, intent.worldMoveDirection.y * boost });
        }

        int actionIdx{ controller->getAnimBlackboard().groundSpeed > 2.5f ? 1 : 0 };
        if (controller->getAnimBlackboard().getFlag(AnimFlag::is_combat_active)) actionIdx += 3;

        controller->getAnimBlackboard().actionIndex = actionIdx;
        controller->getAnimBlackboard().currentJumps = 1;
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AirTraversal));
        return;
    }

    // Forbid early locomotion cancels directly towards the drop-off
    if (m_isEdgeBraked && canMove && playerWantsToMove)
    {
        const float dotEdge{ (intent.worldMoveDirection.x * m_edgeDirection.x) + (intent.worldMoveDirection.y * m_edgeDirection.y) };
        if (dotEdge > 0.1f)
        {
            // Player is steering off the cliff: hold them in the recovery pose
            canMove = false;
            if (motor)
            {
                motor->SetDesiredDirection({ 0.0f, 0.0f });
                motor->HaltMomentum(1.0f);
            }
        }
    }

    // Evasion out of landing recovery
    if (canEvade && intent.bDashTriggered)
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::DashEvade));
        return;
    }

    // Attack out of landing recovery
    if (m_canCancelCombo && intent.bAttackPressed)
    {
        if (controller->getAnimBlackboard().getFlag(AnimFlag::is_speed_buff_active))
        {
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AttackSpeed));
        }
        else
        {
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AttackPrimary));
        }
        return;
    }

    // Locomotion out of landing recovery
    const bool isAnimationDone{ anim && (anim->GetCurrentPhase() >= 0.999f || m_timer <= 0.0f) };
    if ((canMove && playerWantsToMove) || isAnimationDone)
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
    }
}

void PlayerLanding::Exit(PlayerControllerComponent* controller)
{
    m_isEdgeBraked = false;

    if (auto* vcam{ OrbitCameraDriverComponent::GetActiveVirtualCamera() })
    {
        if (vcam->GetTargetProfile() == CameraProfileType::roll_landing)
        {
            vcam->SetTargetProfile(CameraProfileType::locomotion);
        }
    }
}

void PlayerParkourWall::Enter(PlayerControllerComponent* controller)
{
    m_wallRunTimer = 0.0f;
    m_isParabolicMount = false;

    // Entering a parkour action signals environmental traversal over combat
    controller->getAnimBlackboard().setFlag(Engine::Animation::AnimFlag::is_combat_active, false);

    if (auto* motor{ controller->GetMovement() })
    {
        m_wasGravityEnabled = motor->GetConfig().useGravity;
        motor->GetConfig().useGravity = false;

        // Detect if we entered from an auto-ground vault which injected upward momentum
        if (motor->GetVerticalVelocity() > 0.0f)
        {
            m_isParabolicMount = true;
        }
        else
        {
            // Halt any downward or residual velocity for bulletproof standard mid-air mounts
            motor->SetVerticalVelocity(0.0f);
            m_isParabolicMount = false;
        }
    }

    // Cache parameters
    if (auto* motor{ controller->GetMovement() })
    {
        motor->DetectFlankingWall(m_wallNormal, m_wallSide);
    }

    if (auto* vcam{ OrbitCameraDriverComponent::GetActiveVirtualCamera() })
    {
        vcam->SetTargetProfile(CameraProfileType::wall_run, m_wallSide);
    }

    auto& bb{ controller->getAnimBlackboard() };

    // Replenish aerial combat resources upon establishing a physical wall-run
    bb.setFlag(AnimFlag::has_air_dashed, false);
    bb.setFlag(AnimFlag::has_air_attacked, false);

    // Graph Routing: Index 1 = Lean Left (Right Wall), Index 0 = Lean Right (Left Wall)
    bb.actionIndex = (m_wallSide == -1) ? 1 : 0;
    bb.setFlag(AnimFlag::is_wall_running, true);

    if (auto* anim{ controller->GetAnimation() })
    {
        anim->PlaySlot(AnimSlot::ParkourWall, true);
    }
}

void PlayerParkourWall::Update(PlayerControllerComponent* controller, float dt)
{
    m_wallRunTimer += dt;
    auto* motor{ controller->GetMovement() };
    const auto& intent{ controller->GetIntent() };
    if (!motor) return;

    // Deterministic forward tangent along the wall surface (orthogonal to m_wallNormal):
    // Left Wall  (m_wallSide == -1): Normal points Right (+X) -> Forward is (-nz,  nx)
    // Right Wall (m_wallSide ==  1): Normal points Left  (-X) -> Forward is ( nz, -nx)
    const DirectX::XMFLOAT2 wallForward{
        (m_wallSide == -1) ? -m_wallNormal.z : m_wallNormal.z,
        (m_wallSide == -1) ? m_wallNormal.x : -m_wallNormal.x
    };

    // Wall Jump Cancel: Camera-independent launch vector
    if (intent.bJumpTriggered)
    {
        motor->Jump(7.5f); // Upward vertical kick

        // Fixed outward kick away from wall + forward momentum along the wall tangent
        const float boost{ motor->GetConfig().sprintSpeed * 0.8f };
        motor->AddImpulse({ (m_wallNormal.x * 12.0f) + (wallForward.x * boost),
                            0.0f,
                            (m_wallNormal.z * 12.0f) + (wallForward.y * boost) });

        auto& blackboard{ controller->getAnimBlackboard() };
        blackboard.currentJumps = 1; // Mark aerial jump usage

        // Takeoff Animation:
        // Node 6: Jump_Start_L (Wall on Right, m_wallSide == 1 -> Leaps Left)
        // Node 7: Jump_Start_R (Wall on Left,  m_wallSide == -1 -> Leaps Right)
        blackboard.actionIndex = (m_wallSide == 1) ? 6 : 7;

        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AirTraversal));
        return;
    }

    // Validate continued wall proximity
    DirectX::XMFLOAT3 currentNormal{};
    int currentSide{};
    const bool stillOnWall{ motor->DetectFlankingWall(currentNormal, currentSide) };
    const float inputSq{ (intent.moveVector.x * intent.moveVector.x) + (intent.moveVector.y * intent.moveVector.y) };

    // Natural Exits: Ran out of wall geometry, completely released input, or released Sprint
    if (!intent.bSprintHeld || inputSq < 0.01f || !stillOnWall)
    {
        auto& blackboard{ controller->getAnimBlackboard() };
        blackboard.actionIndex = blackboard.getFlag(AnimFlag::is_combat_active) ? 5 : 2; // Fall Loop
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AirTraversal));
        return;
    }

    m_wallNormal = currentNormal; // Track curving geometry updates
    m_wallSide = currentSide;

    // Drive the Kinematic Motor forward along the wall tangent
    motor->SetSprinting(true);
    motor->SetDesiredDirection(wallForward);

    // Parabolic lift decay & Strict Height Lock
    float currentVelY{ motor->GetVerticalVelocity() };
    if (m_isParabolicMount)
    {
        // Apply synthetic gravity to smooth out the initial upward arc
        currentVelY += motor->GetConfig().gravity * dt;
        if (currentVelY <= 0.0f)
        {
            currentVelY = 0.0f; // Lock flat at the apex
            m_isParabolicMount = false; // Transition to height lock
        }
    }
    else
    {
        // Strictly lock vertical velocity to 0 to prevent fatigue drop
        currentVelY = 0.0f;
    }
    motor->SetVerticalVelocity(currentVelY);

    // Anti-Drift: Lock capsule flush against collision mesh (15.0 overcomes PhysX contact offsets)
    motor->AddImpulse({ -m_wallNormal.x * 15.0f * dt, 0.0f, -m_wallNormal.z * 15.0f * dt });

    // Visual Steering along Tangent
    FacingResolver::SmoothFaceDirection(controller->GetOwner(), wallForward, 720.0f, dt);
}

void PlayerParkourWall::Exit(PlayerControllerComponent* controller)
{
    if (auto* vcam{ OrbitCameraDriverComponent::GetActiveVirtualCamera() })
    {
        vcam->SetTargetProfile(CameraProfileType::locomotion);
    }

    if (auto* motor{ controller->GetMovement() })
    {
        motor->GetConfig().useGravity = m_wasGravityEnabled;
    }
    controller->getAnimBlackboard().setFlag(AnimFlag::is_wall_running, false);
}

void PlayerDashEvade::Enter(PlayerControllerComponent* controller)
{
    m_canCancelCombo = false;
    m_canCancelEvade = false;
    m_canCancelLocomotion = false;
    m_hasAuthoredLocomotionCancel = false;
    const auto& intent{ controller->GetIntent() };

    m_dashDir = FacingResolver::ResolveDirectionOrCurrentFacing(controller->GetOwner(), intent.worldMoveDirection);
    FacingResolver::SnapFaceDirection(controller->GetOwner(), m_dashDir);

    if (auto* vcam{ OrbitCameraDriverComponent::GetActiveVirtualCamera() })
    {
        vcam->SetTargetProfile(CameraProfileType::dash);
    }

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

        if (const auto* activeNode{ anim->GetActiveAnimNode() })
        {
            for (const auto& ev : activeNode->events)
            {
                if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Locomotion))
                {
                    m_hasAuthoredLocomotionCancel = true;
                    break;
                }
            }
        }
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
        if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Combo))
        {
            m_canCancelCombo = true;
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Evade))
        {
            m_canCancelEvade = true;
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Locomotion))
        {
            m_canCancelLocomotion = true;
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Movement_Halt))
        {
            if (auto* motor{ controller->GetMovement() })
            {
                motor->HaltMomentum(ev.payload);
            }
        }
    }

    const auto& intent{ controller->GetIntent() };
    const bool canEvade{ m_canCancelEvade || (!m_hasAuthoredLocomotionCancel && m_canCancelCombo) };
    const bool canMove{ m_canCancelLocomotion || (!m_hasAuthoredLocomotionCancel && m_canCancelCombo) };

    // Dash Jump: Jumping out of a dash transitions immediately to air traversal while keeping momentum
    if (canEvade && intent.bJumpTriggered && controller->GetMovement()->isGrounded())
    {
        controller->GetMovement()->Jump(6.5f);
        controller->getAnimBlackboard().actionIndex = 0; // Jump Takeoff
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AirTraversal));
        return;
    }

    const float inputSq{ (intent.moveVector.x * intent.moveVector.x) + (intent.moveVector.y * intent.moveVector.y) };
    const bool playerWantsToMove{ inputSq > 0.01f };

    // Dash Attack Cancel
    if (m_canCancelCombo && intent.bAttackPressed)
    {
        if (controller->getAnimBlackboard().getFlag(AnimFlag::is_speed_buff_active))
        {
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AttackSpeed));
            return;
        }

        if (controller->GetMovement()->isGrounded())
        {
            controller->SetPendingComboHit(-1);
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AttackContextual));
        }
        else
        {
            controller->SetPendingComboHit(4); // Air Dash Attack
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AttackAerial));
        }
        return;
    }

    // Movement Cancel (ONLY ALLOWED ON GROUND)
    if (canMove && playerWantsToMove && controller->GetMovement()->isGrounded())
    {
        // If the player evasively dashes and immediately holds Sprint to run away,
        // we silently demote the combat stance. They will emerge from the dash straight into non combat fast running.
        if (intent.bSprintHeld)
        {
            controller->getAnimBlackboard().setFlag(Engine::Animation::AnimFlag::is_combat_active, false);
        }

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
            controller->getAnimBlackboard().actionIndex = controller->getAnimBlackboard().getFlag(AnimFlag::is_combat_active) ? 5 : 2; // Fall Loop
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
    if (auto* vcam{ OrbitCameraDriverComponent::GetActiveVirtualCamera() })
    {
        if (vcam->GetTargetProfile() == CameraProfileType::dash)
        {
            vcam->SetTargetProfile(CameraProfileType::locomotion);
        }
    }

    if (auto* motor{ controller->GetMovement() })
    {
        motor->ClearKinematicOverride();
    }
}

// COMBAT (GROUND)
void PlayerAttackPrimary::Enter(PlayerControllerComponent* controller)
{
    // Ensure camera reverts to neutral combat framing for ground attack combos
    if (auto* vcam{ OrbitCameraDriverComponent::GetActiveVirtualCamera() })
    {
        vcam->SetTargetProfile(CameraProfileType::locomotion);
    }

    auto& blackboard{ controller->getAnimBlackboard() };

    // Advance the macro-track on every new state entry to keep visuals fresh (0 -> 1 -> 2)
    m_trackIndex = (m_trackIndex + 1) % 3;

    // Consume logical step token (e.g., 1 for Hit 2 from Sprint Attack, 0 for neutral jab)
    const int pendingHit{ controller->ConsumePendingComboHit() };
    m_stepIndex = (pendingHit > 0 && pendingHit < 4) ? pendingHit : 0;

    // Resolve flat O(1) array index mapping to the 12-node selector
    m_activeNode = (m_trackIndex * 4) + m_stepIndex;

    blackboard.setFlag(Engine::Animation::AnimFlag::is_combat_active, true);

    const auto& intent = controller->GetIntent();
    m_lungeDirection = ResolveSoftLockOrFacingDirection(controller->GetOwner(), intent.worldMoveDirection);

    if (auto* motor = controller->GetMovement())
    {
        motor->SetDesiredDirection({ 0.0f, 0.0f });
    }

    PlayCurrentAttack(controller);
}

void PlayerAttackPrimary::PlayCurrentAttack(PlayerControllerComponent* controller) noexcept
{
    m_canCancelCombo = false;
    m_canCancelEvade = false;
    m_canCancelLocomotion = false;
    m_hasAuthoredEvadeCancel = false;
    m_cancelDeferFrames = 0;
    m_bufferedAttack = {};

    controller->getAnimBlackboard().actionIndex = m_activeNode;

    FacingResolver::SnapFaceDirection(controller->GetOwner(), m_lungeDirection);

    if (auto* anim{ controller->GetAnimation() })
    {
        anim->PlaySlot(AnimSlot::Attack_Primary, true);
        m_exitTimer = anim->GetSlotDuration(AnimSlot::Attack_Primary);

        if (const auto* activeNode{ anim->GetActiveAnimNode() })
        {
            for (const auto& ev : activeNode->events)
            {
                if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Evade))
                {
                    m_hasAuthoredEvadeCancel = true;
                    break;
                }
            }
        }
    }
}

void PlayerAttackPrimary::Update(PlayerControllerComponent* controller, float dt)
{
    const auto* anim{ controller->GetAnimation() };
    if (!anim || !anim->IsHitStopped())
    {
        m_exitTimer -= dt;
    }

    if (m_bufferedAttack.timer > 0.0f) m_bufferedAttack.timer -= dt;

    const auto& intent = controller->GetIntent();

    // Live hardware poll for directional intent (requires fresh flick to prevent holding-forward accidental triggers)
    const int liveDirectionalIntent{ EvaluateCommandNormal(controller->GetOwner(), intent, true) };

    // Monotonic Buffer Upgrade (Sticky Intent)
    if (intent.bAttackPressed)
    {
        m_bufferedAttack.timer = 0.25f; // Always refresh the buffer lifetime on press
        // Never downgrade a latched directional intent back to neutral (-1) from sloppy mashing
        if (liveDirectionalIntent != -1 || m_bufferedAttack.targetCommandNormal == -1)
        {
            m_bufferedAttack.targetCommandNormal = liveDirectionalIntent;
        }
    }
    else if (m_bufferedAttack.timer > 0.0f && liveDirectionalIntent != -1)
    {
        // Promote neutral buffer to directional if stick is pushed mid-swing
        m_bufferedAttack.targetCommandNormal = liveDirectionalIntent;
    }

    for (const auto& ev : controller->GetAnimation()->GetFiredEvents())
    {
        if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Combo))
        {
            m_canCancelCombo = true;
            m_cancelDeferFrames = 4;
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Evade))
        {
            m_canCancelEvade = true;
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Locomotion))
        {
            m_canCancelLocomotion = true;
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Lunge_Impulse))
        {
            if (auto* motor{ controller->GetMovement() })
            {
                ApplyGroundLungeKinematics(controller, motor, m_lungeDirection, ev.payload);
            }
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Movement_Halt))
        {
            if (auto* motor{ controller->GetMovement() })
            {
                motor->HaltMomentum(ev.payload);
            }
        }
    }

    auto* motor{ controller->GetMovement() };
    const bool canEvade{ m_canCancelEvade || (!m_hasAuthoredEvadeCancel && m_canCancelCombo) };

    // Evasion Cancels (Dash, Jump, Slide)
    if (canEvade)
    {
        auto& blackboard{ controller->getAnimBlackboard() };
        if (intent.bDashTriggered)
        {
            if (!motor || motor->isGrounded() || !blackboard.getFlag(AnimFlag::has_air_dashed))
            {
                controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::DashEvade));
                return;
            }
        }
        if (intent.bJumpTriggered && motor && motor->isGrounded())
        {
            motor->Jump(6.5f);
            blackboard.actionIndex = 0;
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AirTraversal));
            return;
        }
        if ((intent.bSlideTriggered || intent.bSlideHeld) && motor && motor->isGrounded() && blackboard.groundSpeed > 2.5f)
        {
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Slide));
            return;
        }
    }

    // Combo Chaining (Ignores held movement direction)
    if (m_canCancelCombo && m_bufferedAttack.timer > 0.0f)
    {
        if (m_cancelDeferFrames > 0) m_cancelDeferFrames--;

        int activeCommand{ liveDirectionalIntent };
        if (activeCommand == -1) activeCommand = m_bufferedAttack.targetCommandNormal;

        if (activeCommand != -1)
        {
            controller->SetPendingComboHit(activeCommand);
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AttackDirectional));
            return;
        }

        if (m_cancelDeferFrames == 0 && m_stepIndex < 3)
        {
            m_stepIndex++;
            m_activeNode = (m_trackIndex * 4) + m_stepIndex;
            m_lungeDirection = ResolveSoftLockOrFacingDirection(controller->GetOwner(), intent.worldMoveDirection);
            PlayCurrentAttack(controller);
            return;
        }
    }

    // Locomotion Cancel (Requires explicit Cancel_Locomotion event)
    const float inputSq{ (intent.moveVector.x * intent.moveVector.x) + (intent.moveVector.y * intent.moveVector.y) };
    if (m_canCancelLocomotion && inputSq > 0.01f && motor && motor->isGrounded())
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
        return;
    }

    // Natural Exit: 100% playback gate
    const bool isAnimationDone{ anim && (anim->GetCurrentPhase() >= 0.999f || m_exitTimer <= -0.2f) };
    if (isAnimationDone)
    {
        if (motor && motor->isGrounded())
        {
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
        }
        else
        {
            controller->getAnimBlackboard().actionIndex = controller->getAnimBlackboard().getFlag(AnimFlag::is_combat_active) ? 5 : 2;
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AirTraversal));
        }
    }
}

void PlayerAttackPrimary::Exit(PlayerControllerComponent* controller)
{
    ClosePlayerHitbox(controller);
}

// Placeholders for Expanded Combat
void PlayerAttackContextual::Enter(PlayerControllerComponent* controller)
{
    // Ensure camera pulls back from sprint zoom into standard combat framing
    if (auto* vcam{ OrbitCameraDriverComponent::GetActiveVirtualCamera() })
    {
        vcam->SetTargetProfile(CameraProfileType::locomotion);
    }

    auto& blackboard{ controller->getAnimBlackboard() };
    blackboard.setFlag(Engine::Animation::AnimFlag::is_combat_active, true);

    // Arbiter: -1 implies dynamic generation (Sprint or Ground Dash attack request)
    int activeNode{ controller->ConsumePendingComboHit() };
    if (activeNode == -1)
    {
        activeNode = m_runAttackToggle;
        m_runAttackToggle ^= 1; // Strict DOD Ping-Pong: 0 -> 1 -> 0 -> 1
    }

    blackboard.actionIndex = activeNode;

    const auto& intent = controller->GetIntent();
    m_lungeDirection = ResolveSoftLockOrFacingDirection(controller->GetOwner(), intent.worldMoveDirection);

    if (auto* motor = controller->GetMovement())
    {
        motor->SetDesiredDirection({ 0.0f, 0.0f });
    }

    PlayCurrentAttack(controller);
}

void PlayerAttackContextual::PlayCurrentAttack(PlayerControllerComponent* controller) noexcept
{
    m_canCancelCombo = false;
    m_canCancelEvade = false;
    m_canCancelLocomotion = false;
    m_hasAuthoredEvadeCancel = false;
    m_cancelDeferFrames = 0;
    m_bufferedAttack = {};

    FacingResolver::SnapFaceDirection(controller->GetOwner(), m_lungeDirection);

    if (auto* anim{ controller->GetAnimation() })
    {
        anim->PlaySlot(AnimSlot::Attack_Contextual, true);
        m_exitTimer = anim->GetSlotDuration(AnimSlot::Attack_Contextual);

        if (const auto* activeNode{ anim->GetActiveAnimNode() })
        {
            for (const auto& ev : activeNode->events)
            {
                if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Evade))
                {
                    m_hasAuthoredEvadeCancel = true;
                    break;
                }
            }
        }
    }
}

void PlayerAttackContextual::Update(PlayerControllerComponent* controller, float dt)
{
    const auto* anim{ controller->GetAnimation() };
    if (!anim || !anim->IsHitStopped())
    {
        m_exitTimer -= dt;
    }
    if (m_bufferedAttack.timer > 0.0f) m_bufferedAttack.timer -= dt;

    const auto& intent{ controller->GetIntent() };
    const int liveDirectionalIntent{ EvaluateCommandNormal(controller->GetOwner(), intent, true) };

    if (intent.bAttackPressed)
    {
        m_bufferedAttack.timer = 0.25f;
        if (liveDirectionalIntent != -1 || m_bufferedAttack.targetCommandNormal == -1)
        {
            m_bufferedAttack.targetCommandNormal = liveDirectionalIntent;
        }
    }
    else if (m_bufferedAttack.timer > 0.0f && liveDirectionalIntent != -1)
    {
        m_bufferedAttack.targetCommandNormal = liveDirectionalIntent;
    }

    for (const auto& ev : controller->GetAnimation()->GetFiredEvents())
    {
        if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Combo))
        {
            m_canCancelCombo = true;
            m_cancelDeferFrames = 4;
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Evade))
        {
            m_canCancelEvade = true;
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Locomotion))
        {
            m_canCancelLocomotion = true;
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Lunge_Impulse))
        {
            if (auto* motor{ controller->GetMovement() })
            {
                ApplyGroundLungeKinematics(controller, motor, m_lungeDirection, ev.payload);
            }
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Movement_Halt))
        {
            if (auto* motor{ controller->GetMovement() })
            {
                motor->HaltMomentum(ev.payload);
            }
        }
    }

    auto* motor{ controller->GetMovement() };
    const bool canEvade{ m_canCancelEvade || (!m_hasAuthoredEvadeCancel && m_canCancelCombo) };

    if (canEvade && intent.bDashTriggered)
    {
        if (!motor || motor->isGrounded() || !controller->getAnimBlackboard().getFlag(AnimFlag::has_air_dashed))
        {
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::DashEvade));
            return;
        }
    }

    if (m_canCancelCombo && m_bufferedAttack.timer > 0.0f && motor && motor->isGrounded())
    {
        if (m_cancelDeferFrames > 0) m_cancelDeferFrames--;

        int activeCommand{ liveDirectionalIntent };
        if (activeCommand == -1) activeCommand = m_bufferedAttack.targetCommandNormal;

        if (activeCommand != -1)
        {
            controller->SetPendingComboHit(activeCommand);
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AttackDirectional));
            return;
        }

        if (m_cancelDeferFrames == 0)
        {
            controller->SetPendingComboHit(1);
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AttackPrimary));
            return;
        }
    }

    const float inputSq{ (intent.moveVector.x * intent.moveVector.x) + (intent.moveVector.y * intent.moveVector.y) };
    if (m_canCancelLocomotion && inputSq > 0.01f && motor && motor->isGrounded())
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
        return;
    }

    const bool isAnimationDone{ anim && (anim->GetCurrentPhase() >= 0.999f || m_exitTimer <= -0.2f) };
    if (isAnimationDone)
    {
        if (motor && motor->isGrounded())
        {
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
        }
        else
        {
            controller->getAnimBlackboard().actionIndex = controller->getAnimBlackboard().getFlag(AnimFlag::is_combat_active) ? 5 : 2;
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AirTraversal));
        }
    }
}

void PlayerAttackContextual::Exit(PlayerControllerComponent* controller)
{
    ClosePlayerHitbox(controller);
}

void PlayerAttackDirectional::Enter(PlayerControllerComponent* controller)
{
    auto& blackboard{ controller->getAnimBlackboard() };
    blackboard.setFlag(Engine::Animation::AnimFlag::is_combat_active, true);

    m_activeNode = controller->ConsumePendingComboHit();
    if (m_activeNode < 0) m_activeNode = 0;
    blackboard.actionIndex = m_activeNode;

    if (auto* motor = controller->GetMovement()) motor->SetDesiredDirection({ 0.0f, 0.0f });
    if (auto* anim = controller->GetAnimation())
    {
        anim->PlaySlot(Engine::Animation::AnimSlot::Attack_Directional, true);
        m_exitTimer = anim->GetSlotDuration(Engine::Animation::AnimSlot::Attack_Directional);
    }

    const auto& intent{ controller->GetIntent() };

    // Directional Snapping:
    // Back attack (Node 1) snaps to the raw stick input direction.
    // Up attack (Node 0) preserves the character's current physical facing to avoid snapping to the camera forward vector.
    if (m_activeNode == 0)
    {
        m_lungeDirection = FacingResolver::ResolveDirectionOrCurrentFacing(controller->GetOwner(), { 0.0f, 0.0f });
    }
    else
    {
        m_lungeDirection = FacingResolver::ResolveDirectionOrCurrentFacing(controller->GetOwner(), intent.worldMoveDirection);
    }
    FacingResolver::SnapFaceDirection(controller->GetOwner(), m_lungeDirection);

    m_canCancelCombo = false;
    m_canCancelEvade = false;
    m_canCancelLocomotion = false;
    m_hasAuthoredEvadeCancel = false;
    m_bufferedAttack = {};

    if (const auto* anim{ controller->GetAnimation() })
    {
        if (const auto* activeNode{ anim->GetActiveAnimNode() })
        {
            for (const auto& ev : activeNode->events)
            {
                if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Evade))
                {
                    m_hasAuthoredEvadeCancel = true;
                    break;
                }
            }
        }
    }
}

void PlayerAttackDirectional::Update(PlayerControllerComponent* controller, float dt)
{
    const auto* anim{ controller->GetAnimation() };
    if (!anim || !anim->IsHitStopped())
    {
        m_exitTimer -= dt;
    }
    if (m_bufferedAttack.timer > 0.0f) m_bufferedAttack.timer -= dt;

    const auto& intent{ controller->GetIntent() };
    const int liveDirectionalIntent{ EvaluateCommandNormal(controller->GetOwner(), intent, true) };

    if (intent.bAttackPressed)
    {
        m_bufferedAttack.timer = 0.25f;
        if (liveDirectionalIntent != -1 || m_bufferedAttack.targetCommandNormal == -1)
        {
            m_bufferedAttack.targetCommandNormal = liveDirectionalIntent;
        }
    }
    else if (m_bufferedAttack.timer > 0.0f && liveDirectionalIntent != -1)
    {
        m_bufferedAttack.targetCommandNormal = liveDirectionalIntent;
    }

    auto* motor{ controller->GetMovement() };

    for (const auto& ev : controller->GetAnimation()->GetFiredEvents())
    {
        if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Combo))
        {
            m_canCancelCombo = true;
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Evade))
        {
            m_canCancelEvade = true;
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Locomotion))
        {
            m_canCancelLocomotion = true;
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Lunge_Impulse))
        {
            if (motor)
            {
                motor->AddImpulse(DirectX::XMFLOAT3{
                    m_lungeDirection.x * ev.payload, 0.0f, m_lungeDirection.y * ev.payload
                    });
            }
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Lunge_Vertical))
        {
            if (motor) motor->SetVerticalVelocity(ev.payload);
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Movement_Halt))
        {
            if (motor) motor->HaltMomentum(ev.payload);
        }
    }

    const bool canEvade{ m_canCancelEvade || (!m_hasAuthoredEvadeCancel && m_canCancelCombo) };

    // Evasion Cancel
    if (canEvade && intent.bDashTriggered)
    {
        if (motor && (motor->isGrounded() || !controller->getAnimBlackboard().getFlag(AnimFlag::has_air_dashed)))
        {
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::DashEvade));
            return;
        }
    }

    // Combo Chaining
    if (m_canCancelCombo && m_bufferedAttack.timer > 0.0f)
    {
        int activeCommand{ liveDirectionalIntent };
        if (activeCommand == -1) activeCommand = m_bufferedAttack.targetCommandNormal;

        const bool isBackAttackBlocked{ (activeCommand == 1) && (m_activeNode == 0) };
        if (activeCommand != -1 && activeCommand != m_activeNode && !isBackAttackBlocked)
        {
            controller->SetPendingComboHit(activeCommand);
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AttackDirectional));
            return;
        }

        auto& blackboard{ controller->getAnimBlackboard() };

        if (!motor || motor->isGrounded())
        {
            if (m_activeNode == 1)
            {
                controller->SetPendingComboHit(1);
                controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AttackPrimary));
                return;
            }
        }
        else
        {
            if (m_activeNode == 0)
            {
                if (blackboard.getFlag(AnimFlag::is_speed_buff_active))
                {
                    controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AttackSpeed));
                }
                else
                {
                    controller->SetPendingComboHit(0);
                    controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AttackAerial));
                }
                return;
            }
        }
    }

    // Locomotion Cancel
    const float inputSq{ (intent.moveVector.x * intent.moveVector.x) + (intent.moveVector.y * intent.moveVector.y) };
    if (m_canCancelLocomotion && inputSq > 0.01f && motor && motor->isGrounded())
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
        return;
    }

    // Natural Exit: 100% playback gate
    const bool isAnimationDone{ anim && (anim->GetCurrentPhase() >= 0.999f || m_exitTimer <= -0.2f) };
    if (isAnimationDone)
    {
        if (motor && motor->isGrounded())
        {
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
        }
        else
        {
            controller->getAnimBlackboard().actionIndex = controller->getAnimBlackboard().getFlag(AnimFlag::is_combat_active) ? 5 : 2;
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AirTraversal));
        }
    }
}

void PlayerAttackDirectional::Exit(PlayerControllerComponent* controller)
{
    ClosePlayerHitbox(controller);
}

void PlayerAttackCharged::Enter(PlayerControllerComponent* controller)
{
    auto& blackboard = controller->getAnimBlackboard();
    blackboard.setFlag(Engine::Animation::AnimFlag::is_combat_active, true);
    blackboard.setFlag(Engine::Animation::AnimFlag::is_charging, false);

    m_phase = ChargeSubPhase::Anticipation;
    m_chargeTimer = 0.0f;
    m_chargeRatio = 0.40f;
    m_canCancelCombo = false;
    m_canCancelEvade = false;
    m_canCancelLocomotion = false;
    m_hasAuthoredEvadeCancel = false;
    m_earlyRelease = false;

    const auto& intent{ controller->GetIntent() };
    m_lungeDirection = ResolveSoftLockOrFacingDirection(controller->GetOwner(), intent.worldMoveDirection);
    FacingResolver::SnapFaceDirection(controller->GetOwner(), m_lungeDirection);

    if (auto* motor{ controller->GetMovement() }) motor->SetDesiredDirection({ 0.0f, 0.0f });
    if (auto* anim{ controller->GetAnimation() })
    {
        anim->SetPlaybackSpeed(1.0f);
        anim->PlaySlot(Engine::Animation::AnimSlot::Attack_Charged, true);

        if (const auto* activeNode{ anim->GetActiveAnimNode() })
        {
            for (const auto& ev : activeNode->events)
            {
                if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Evade))
                {
                    m_hasAuthoredEvadeCancel = true;
                    break;
                }
            }
        }
    }
}

void PlayerAttackCharged::Update(PlayerControllerComponent* controller, float dt)
{
    auto* anim{ controller->GetAnimation() };
    auto* motor{ controller->GetMovement() };
    if (!anim || !motor) return;

    const auto& intent{ controller->GetIntent() };

    if (m_phase == ChargeSubPhase::Anticipation && !intent.bHeavyAttackHeld)
    {
        m_earlyRelease = true;
    }
    else if (m_phase == ChargeSubPhase::Sustain_Hold)
    {
        m_chargeTimer += dt;
        controller->getAnimBlackboard().chargeTimer = m_chargeTimer;

        if (m_chargeTimer >= 1.0f) m_chargeRatio = 1.0f;
        else if (m_chargeTimer >= 0.5f) m_chargeRatio = 0.75f;

        if (!intent.bHeavyAttackHeld || m_chargeTimer >= 1.0f)
        {
            m_phase = ChargeSubPhase::Release_Lunge;
            anim->SetPlaybackSpeed(1.0f);
            controller->getAnimBlackboard().setFlag(Engine::Animation::AnimFlag::is_charging, false);
        }
    }

    for (const auto& ev : anim->GetFiredEvents())
    {
        if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Pose_HoldMarker))
        {
            if (m_phase == ChargeSubPhase::Anticipation)
            {
                if (m_earlyRelease)
                {
                    m_phase = ChargeSubPhase::Release_Lunge;
                }
                else
                {
                    m_phase = ChargeSubPhase::Sustain_Hold;
                    anim->SetPlaybackSpeed(0.0f);
                    controller->getAnimBlackboard().setFlag(Engine::Animation::AnimFlag::is_charging, true);
                }
            }
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Lunge_Impulse))
        {
            const float finalImpulse = ev.payload * m_chargeRatio;
            ApplyGroundLungeKinematics(controller, motor, m_lungeDirection, finalImpulse);
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Movement_Halt))
        {
            motor->HaltMomentum(ev.payload);
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Combo))
        {
            m_canCancelCombo = true;
            m_phase = ChargeSubPhase::Recovery;
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Evade))
        {
            m_canCancelEvade = true;
            m_phase = ChargeSubPhase::Recovery;
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Locomotion))
        {
            m_canCancelLocomotion = true;
            m_phase = ChargeSubPhase::Recovery;
        }
    }

    const float inputSq{ (intent.moveVector.x * intent.moveVector.x) + (intent.moveVector.y * intent.moveVector.y) };
    if ((m_phase == ChargeSubPhase::Anticipation || m_phase == ChargeSubPhase::Sustain_Hold) && inputSq > 0.01f)
    {
        FacingResolver::SmoothFaceDirection(controller->GetOwner(), intent.worldMoveDirection, 60.0f, dt);
        const float yawRad{ DirectX::XMConvertToRadians(controller->GetOwner()->GetRotation().y) };
        m_lungeDirection = { std::sin(yawRad), std::cos(yawRad) };
    }

    const bool canEvade{ m_canCancelEvade || (!m_hasAuthoredEvadeCancel && m_canCancelCombo) };

    if (canEvade && intent.bDashTriggered && (motor->isGrounded() || !controller->getAnimBlackboard().getFlag(AnimFlag::has_air_dashed)))
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::DashEvade));
        return;
    }

    if (m_canCancelCombo)
    {
        if (intent.bAttackPressed)
        {
            if (controller->getAnimBlackboard().getFlag(AnimFlag::is_speed_buff_active))
            {
                controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AttackSpeed));
            }
            else
            {
                controller->SetPendingComboHit(0);
                controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AttackPrimary));
            }
            return;
        }

        if (intent.bHeavyAttackPressed)
        {
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AttackCharged));
            return;
        }
    }

    if (m_canCancelLocomotion && inputSq > 0.01f && motor->isGrounded())
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
        return;
    }

    const bool isAnimationDone{ anim && (anim->GetCurrentPhase() >= 0.999f || anim->GetCurrentTimer() >= anim->GetSlotDuration(Engine::Animation::AnimSlot::Attack_Charged) - 0.05f) };
    if (isAnimationDone)
    {
        if (motor->isGrounded())
        {
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
        }
        else
        {
            controller->getAnimBlackboard().actionIndex = controller->getAnimBlackboard().getFlag(Engine::Animation::AnimFlag::is_combat_active) ? 5 : 2;
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AirTraversal));
        }
    }
}

void PlayerAttackCharged::Exit(PlayerControllerComponent* controller)
{
    // Fail-safe: Always unfreeze playback and reset charge flags if interrupted (e.g. taking damage)
    if (auto* anim = controller->GetAnimation()) anim->SetPlaybackSpeed(1.0f);

    auto& blackboard = controller->getAnimBlackboard();
    blackboard.setFlag(Engine::Animation::AnimFlag::is_charging, false);
    blackboard.chargeTimer = 0.0f;

    ClosePlayerHitbox(controller);
}

void PlayerAttackAerial::Enter(PlayerControllerComponent* controller)
{
    auto& blackboard = controller->getAnimBlackboard();

    // Force combat stance active and consume the air-attack privilege for this jump
    blackboard.setFlag(Engine::Animation::AnimFlag::is_combat_active, true);
    blackboard.setFlag(Engine::Animation::AnimFlag::has_air_attacked, true);

    m_comboIndex = controller->ConsumePendingComboHit();
    if (m_comboIndex < 0) m_comboIndex = 0;

    const auto& intent = controller->GetIntent();
    m_lungeDirection = FacingResolver::ResolveDirectionOrCurrentFacing(controller->GetOwner(), intent.worldMoveDirection);

    if (auto* motor = controller->GetMovement())
    {
        // Snapshot baseline physics, then completely suspend gravity and vertical momentum 
        // to freeze the character on the current Y-plane.
        m_wasGravityEnabled = motor->GetConfig().useGravity;
        motor->GetConfig().useGravity = false;
        motor->SetVerticalVelocity(0.0f);
        motor->SetDesiredDirection({ 0.0f, 0.0f });
    }

    PlayCurrentAttack(controller);
}

void PlayerAttackAerial::PlayCurrentAttack(PlayerControllerComponent* controller) noexcept
{
    m_canCancelCombo = false;
    m_canCancelEvade = false;
    m_hasAuthoredEvadeCancel = false;
    m_bufferedAttack = {};

    controller->getAnimBlackboard().actionIndex = m_comboIndex;

    // Halt lingering momentum from prior attack so character does not drift into victim during windup
    if (auto* motor{ controller->GetMovement() })
    {
        motor->HaltMomentum(1.0f);
    }

    const DirectX::XMFLOAT3 pPos{ controller->GetOwner()->GetPosition() };
    auto* targetEnemy{ CombatDirector::Instance().FindNearestAirborneEnemy(pPos, 4.0f) };
    if (targetEnemy && targetEnemy->GetOwner())
    {
        const DirectX::XMFLOAT3 ePos{ targetEnemy->GetOwner()->GetPosition() };
        const float dx{ ePos.x - pPos.x };
        const float dz{ ePos.z - pPos.z };
        const float distSq{ (dx * dx) + (dz * dz) };
        if (distSq > 0.0001f)
        {
            const float invDist{ 1.0f / std::sqrt(distSq) };
            m_lungeDirection = { dx * invDist, dz * invDist };
        }
    }

    FacingResolver::SnapFaceDirection(controller->GetOwner(), m_lungeDirection);

    if (auto* anim{ controller->GetAnimation() })
    {
        anim->PlaySlot(AnimSlot::Attack_Aerial, true);
        m_exitTimer = anim->GetSlotDuration(AnimSlot::Attack_Aerial);

        if (const auto* activeNode{ anim->GetActiveAnimNode() })
        {
            for (const auto& ev : activeNode->events)
            {
                if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Evade))
                {
                    m_hasAuthoredEvadeCancel = true;
                    break;
                }
            }
        }
    }

    if (targetEnemy)
    {
        // Maintain FloatHold suspension during attack startup and execution across all nodes.
        // Node 0 & 1 hold through the full duration; Node 2 covers the spin windup before impact.
        const float holdDuration{ (m_comboIndex == 2) ? ((std::max)(0.85f, m_exitTimer + 0.35f)) : ((std::max)(1.2f, m_exitTimer + 0.6f)) };
        targetEnemy->NotifyAirJuggle(0.0f, false, holdDuration);
    }
}

void PlayerAttackAerial::Update(PlayerControllerComponent* controller, float dt)
{
    auto* motor{ controller->GetMovement() };
    if (!motor) return;

    if (motor->isGrounded())
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
        return;
    }

    const auto* anim{ controller->GetAnimation() };
    if (!anim || !anim->IsHitStopped())
    {
        m_exitTimer -= dt;
    }
    if (m_bufferedAttack.timer > 0.0f) m_bufferedAttack.timer -= dt;

    const auto& intent{ controller->GetIntent() };
    const int liveDirectionalIntent{ EvaluateCommandNormal(controller->GetOwner(), intent, true) };

    if (intent.bAttackPressed)
    {
        m_bufferedAttack.timer = 0.25f;
        if (liveDirectionalIntent != -1 || m_bufferedAttack.targetCommandNormal == -1)
        {
            m_bufferedAttack.targetCommandNormal = liveDirectionalIntent;
        }
    }
    else if (m_bufferedAttack.timer > 0.0f && liveDirectionalIntent != -1)
    {
        m_bufferedAttack.targetCommandNormal = liveDirectionalIntent;
    }

    for (const auto& ev : controller->GetAnimation()->GetFiredEvents())
    {
        if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Combo))
        {
            m_canCancelCombo = true;
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Evade))
        {
            m_canCancelEvade = true;
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Lunge_Impulse))
        {
            ApplyAirLungeKinematics(controller, motor, m_lungeDirection, ev.payload, dt);
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Movement_Halt))
        {
            motor->HaltMomentum(ev.payload);
        }
    }

    const bool canEvade{ m_canCancelEvade || (!m_hasAuthoredEvadeCancel && m_canCancelCombo) };

    if (canEvade && intent.bDashTriggered)
    {
        if (!controller->getAnimBlackboard().getFlag(AnimFlag::has_air_dashed))
        {
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::DashEvade));
            return;
        }
    }

    if (canEvade && intent.bHeavyAttackPressed)
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AttackPlunge));
        return;
    }

    if (m_canCancelCombo && m_bufferedAttack.timer > 0.0f)
    {
        if (m_comboIndex < 2)
        {
            m_comboIndex++;
            m_lungeDirection = FacingResolver::ResolveDirectionOrCurrentFacing(controller->GetOwner(), intent.worldMoveDirection);
            PlayCurrentAttack(controller);
            return;
        }
    }

    const bool isAnimationDone{ anim && (anim->GetCurrentPhase() >= 0.999f || m_exitTimer <= -0.2f) };
    if (isAnimationDone)
    {
        controller->getAnimBlackboard().actionIndex = controller->getAnimBlackboard().getFlag(AnimFlag::is_combat_active) ? 5 : 2;
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AirTraversal));
    }
}

void PlayerAttackAerial::Exit(PlayerControllerComponent* controller)
{
    // Always restore the baseline physics state securely when leaving the aerial combo
    if (auto* motor = controller->GetMovement())
    {
        motor->GetConfig().useGravity = m_wasGravityEnabled;
    }

    // When ending aerial combo without chaining (Node 0 or 1 finished with no continuation),
    // deterministically release the juggled victim from FloatHold into FallLoop in lockstep with the player
    if (controller && controller->GetOwner())
    {
        const DirectX::XMFLOAT3 pPos{ controller->GetOwner()->GetPosition() };
        if (auto* targetEnemy{ CombatDirector::Instance().FindNearestAirborneEnemy(pPos, 4.0f) })
        {
            targetEnemy->NotifyAirDrop();
        }
    }

    ClosePlayerHitbox(controller);
}

void PlayerAttackPlunge::Enter(PlayerControllerComponent* controller)
{
    auto& blackboard{ controller->getAnimBlackboard() };
    blackboard.setFlag(Engine::Animation::AnimFlag::is_combat_active, true);

    auto* motor{ controller->GetMovement() };
    auto* anim{ controller->GetAnimation() };
    const auto& intent{ controller->GetIntent() };

    m_canCancelCombo = false;
    m_canCancelEvade = false;
    m_canCancelLocomotion = false;
    m_hasAuthoredEvadeCancel = false;
    m_phase = PlungeSubPhase::Start;

    m_lungeDirection = ResolvePlungeSoftLockOrFacingDirection(controller->GetOwner(), intent.worldMoveDirection);
    FacingResolver::SnapFaceDirection(controller->GetOwner(), m_lungeDirection);

    if (motor)
    {
        m_wasGravityEnabled = motor->GetConfig().useGravity;
        motor->GetConfig().useGravity = false;
        motor->SetVerticalVelocity(0.0f);
        motor->SetDesiredDirection({ 0.0f, 0.0f });
        motor->SetFrictionMultiplier(0.0f);
    }

    blackboard.actionIndex = (m_variation * 3) + 0;

    if (anim)
    {
        anim->PlaySlot(Engine::Animation::AnimSlot::Attack_Plunge, true);
        m_stateTimer = anim->GetSlotDuration(Engine::Animation::AnimSlot::Attack_Plunge);

        if (const auto* activeNode{ anim->GetActiveAnimNode() })
        {
            for (const auto& ev : activeNode->events)
            {
                if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Evade))
                {
                    m_hasAuthoredEvadeCancel = true;
                    break;
                }
            }
        }
    }
}

void PlayerAttackPlunge::Update(PlayerControllerComponent* controller, float dt)
{
    m_stateTimer -= dt;
    auto* motor{ controller->GetMovement() };
    auto* anim{ controller->GetAnimation() };
    auto& blackboard{ controller->getAnimBlackboard() };
    const auto& intent{ controller->GetIntent() };

    if (!motor || !anim) return;

    for (const auto& ev : anim->GetFiredEvents())
    {
        if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Combo))
        {
            m_canCancelCombo = true;
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Evade))
        {
            m_canCancelEvade = true;
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Locomotion))
        {
            m_canCancelLocomotion = true;
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Lunge_Vertical))
        {
            motor->SetVerticalVelocity(ev.payload);
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Lunge_Impulse))
        {
            float downSpeed{ std::abs(motor->GetVerticalVelocity()) };
            if (downSpeed < 1.0f)
            {
                for (const auto& otherEv : anim->GetFiredEvents())
                {
                    if (otherEv.eventId == static_cast<std::uint32_t>(CombatEventId::Lunge_Vertical))
                    {
                        downSpeed = std::abs(otherEv.payload);
                        break;
                    }
                }
            }
            if (downSpeed < 1.0f) downSpeed = 23.0f;

            ApplyPlungeLungeKinematics(controller, motor, m_lungeDirection, ev.payload, downSpeed);
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Movement_Halt))
        {
            motor->HaltMomentum(ev.payload);
        }
    }

    if (m_phase == PlungeSubPhase::End)
    {
        const bool canEvade{ m_canCancelEvade || (!m_hasAuthoredEvadeCancel && m_canCancelCombo) };
        if (canEvade && intent.bDashTriggered)
        {
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::DashEvade));
            return;
        }
        if (m_canCancelCombo && intent.bAttackPressed)
        {
            controller->SetPendingComboHit(0);
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AttackPrimary));
            return;
        }

        const float inputSq{ (intent.moveVector.x * intent.moveVector.x) + (intent.moveVector.y * intent.moveVector.y) };
        if (m_canCancelLocomotion && inputSq > 0.01f)
        {
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
            return;
        }
    }

    if (m_phase == PlungeSubPhase::Start)
    {
        if (m_stateTimer <= 0.05f)
        {
            m_phase = PlungeSubPhase::Loop;
            blackboard.actionIndex = (m_variation * 3) + 1;
            anim->PlaySlot(Engine::Animation::AnimSlot::Attack_Plunge, true);
        }
    }
    else if (m_phase == PlungeSubPhase::Loop)
    {
        if (motor->isGrounded())
        {
            m_phase = PlungeSubPhase::End;
            blackboard.actionIndex = (m_variation * 3) + 2;
            anim->PlaySlot(Engine::Animation::AnimSlot::Attack_Plunge, true);
            m_stateTimer = anim->GetSlotDuration(Engine::Animation::AnimSlot::Attack_Plunge);

            motor->SetFrictionMultiplier(1.0f);
            motor->HaltMomentum(1.0f);

            // Ground impact: slam nearby target into Target knockdown
            if (auto* targetEnemy{ FindPlungeSoftLockTarget(controller->GetOwner(), m_lungeDirection, 4.0f) })
            {
                targetEnemy->NotifyPlungeSlam();
            }
        }
    }
    else if (m_phase == PlungeSubPhase::End)
    {
        const bool isAnimationDone{ anim && (anim->GetCurrentPhase() >= 0.999f || m_stateTimer <= 0.0f) };
        if (isAnimationDone)
        {
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
        }
    }
}

void PlayerAttackPlunge::Exit(PlayerControllerComponent* controller)
{
    // Always restore the baseline physics state securely
    if (auto* motor{ controller->GetMovement() })
    {
        motor->GetConfig().useGravity = m_wasGravityEnabled;
        motor->SetFrictionMultiplier(1.0f); // Fail-safe friction restore
    }

    // Round-Robin Deterministic Selection (0 -> 1 -> 2 -> 0)
    // Advanced upon exit so the entire 3-phase attack sequence locks onto the same variation index.
    m_variation = (m_variation + 1) % 3;

    ClosePlayerHitbox(controller);
}

// DEFENSE & REACTION
void PlayerParryCounter::Enter(PlayerControllerComponent* controller)
{
    controller->getAnimBlackboard().setFlag(Engine::Animation::AnimFlag::is_combat_active, true);
    if (auto* motor = controller->GetMovement()) motor->SetDesiredDirection({ 0.0f, 0.0f });
    if (auto* anim = controller->GetAnimation()) anim->PlaySlot(Engine::Animation::AnimSlot::Parry_Counter, true);
}
void PlayerParryCounter::Update(PlayerControllerComponent* controller, float)
{
    if (auto* anim = controller->GetAnimation()) {
        if (anim->GetCurrentTimer() >= anim->GetSlotDuration(Engine::Animation::AnimSlot::Parry_Counter) - 0.05f) {
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
        }
    }
}
void PlayerParryCounter::Exit(PlayerControllerComponent*) {}

void PlayerHitReact::Enter(PlayerControllerComponent* controller)
{
    if (!controller)
    {
        return;
    }

    auto* motor{ controller->GetMovement() };
    auto* anim{ controller->GetAnimation() };
    auto& blackboard{ controller->getAnimBlackboard() };
    const PlayerHitReactionData& hitData{ controller->GetLastHitReaction() };

    m_canCancelEvade = false;
    m_canCancelLocomotion = false;
    m_stateTimer = 0.0f;

    if (motor)
    {
        motor->SetDesiredDirection({ 0.0f, 0.0f });
        motor->ClearKinematicOverride();
        // Normal gravity in case we were interrupted out of WallRun, Plunge, or AirCombo
        motor->GetConfig().useGravity = true;
        motor->SetGravityScale(1.0f);
    }

    const bool isCombat{ blackboard.getFlag(AnimFlag::is_combat_active) };
    const int dirIdx{ static_cast<int>(hitData.directionIndex & 0x03u) };
    const bool isAirborne{ motor && !motor->isGrounded() };

    if (hitData.isLethal)
    {
        // Terminal Death (HP == 0): Play Knockdown clip once, clamp on floor, disable input & dynamic combat collisions
        m_phase = HitReactPhase::Dead;
        controller->SetInvincible(true);
        controller->SetInputEnabled(false);

        if (motor)
        {
            motor->HaltMomentum(0.5f);
        }

        if (auto* owner{ controller->GetOwner() })
        {
            if (auto* capsule{ owner->GetComponent<CapsuleColliderComponent>() })
            {
                capsule->SetCollisionLayer(CollisionLayer::Player, CollisionLayer::WorldStatic);
            }
        }

        const int knockdownNode{ isCombat ? 13 : 12 }; // 12: Hit_Large_Knockdown, 13: Hit_Combat_Large_Knockdown
        blackboard.actionIndex = knockdownNode;
        if (anim)
        {
            anim->PlaySlot(AnimSlot::HitReact, true, knockdownNode);
            m_stateTimer = anim->GetSlotDuration(AnimSlot::HitReact);
        }
        return;
    }

    if (isAirborne)
    {
        // Airborne Hit: Play directional Air Hit (8..11, 1:1 F/B/L/R with NO L/R inversion), then land normally
        m_phase = HitReactPhase::AirFlinch;
        controller->SetInvincible(false);

        const int airHitNode{ 8 + dirIdx }; // 8: F, 9: B, 10: L, 11: R
        blackboard.actionIndex = airHitNode;
        if (anim)
        {
            anim->PlaySlot(AnimSlot::HitReact, true, airHitNode);
            m_stateTimer = anim->GetSlotDuration(AnimSlot::HitReact);
        }
        return;
    }

    if (hitData.isHeavyKnockdown)
    {
        // Hard Knockdown (Alive): Play Knockdown clip with full wake-up protection, followed by Get_Up
        m_phase = HitReactPhase::Knockdown;
        controller->SetInvincible(true);

        const int knockdownNode{ isCombat ? 13 : 12 }; // 12: Hit_Large_Knockdown, 13: Hit_Combat_Large_Knockdown
        blackboard.actionIndex = knockdownNode;
        if (anim)
        {
            anim->PlaySlot(AnimSlot::HitReact, true, knockdownNode);
            m_stateTimer = anim->GetSlotDuration(AnimSlot::HitReact);
        }
        return;
    }

    // Standard Grounded 4-Way Hit Large (Holster: 0..3, Combat: 4..7)
    m_phase = HitReactPhase::GroundedFlinch;
    controller->SetInvincible(false);

    const int groundedNode{ (isCombat ? 4 : 0) + dirIdx };
    blackboard.actionIndex = groundedNode;
    if (anim)
    {
        anim->PlaySlot(AnimSlot::HitReact, true, groundedNode);
        m_stateTimer = anim->GetSlotDuration(AnimSlot::HitReact);
    }

    // Frontal stagger faces attacker; back and lateral hits preserve orientation so flinches do not flip 180 degrees
    if (dirIdx == 0 && controller->GetOwner())
    {
        const DirectX::XMFLOAT3 currentPos{ Engine::Combat::GetWorldPosition(controller->GetOwner()) };
        const DirectX::XMFLOAT2 toAttacker{
            hitData.attackerPos.x - currentPos.x,
            hitData.attackerPos.z - currentPos.z
        };
        if (((toAttacker.x * toAttacker.x) + (toAttacker.y * toAttacker.y)) > 0.01f)
        {
            FacingResolver::SnapFaceDirection(controller->GetOwner(), toAttacker);
        }
    }
}

void PlayerHitReact::TransitionToGetUp(PlayerControllerComponent* controller) noexcept
{
    if (!controller)
    {
        return;
    }

    m_phase = HitReactPhase::GetUp;
    m_canCancelEvade = false;
    m_canCancelLocomotion = false;
    controller->SetInvincible(true); // Maintain wake-up invulnerability through standing recovery

    if (auto* motor{ controller->GetMovement() })
    {
        motor->HaltMomentum(1.0f);
        motor->SetDesiredDirection({ 0.0f, 0.0f });
    }

    auto& blackboard{ controller->getAnimBlackboard() };
    const bool isCombat{ blackboard.getFlag(AnimFlag::is_combat_active) };
    const int getUpNode{ isCombat ? 15 : 14 }; // 14: Get_Up, 15: Get_up_Combat
    blackboard.actionIndex = getUpNode;

    if (auto* anim{ controller->GetAnimation() })
    {
        anim->PlaySlot(AnimSlot::HitReact, true, getUpNode);
        m_stateTimer = anim->GetSlotDuration(AnimSlot::HitReact);
    }
}

void PlayerHitReact::Update(PlayerControllerComponent* controller, float dt)
{
    if (!controller)
    {
        return;
    }

    auto* anim{ controller->GetAnimation() };
    auto* motor{ controller->GetMovement() };
    auto& blackboard{ controller->getAnimBlackboard() };

    if (motor)
    {
        const DirectX::XMFLOAT3 vel{ motor->GetTotalVelocity() };
        blackboard.groundSpeed = std::sqrt((vel.x * vel.x) + (vel.z * vel.z));
        blackboard.verticalVelocity = vel.y;
        blackboard.setFlag(AnimFlag::is_grounded, motor->isGrounded());
    }

    if (!anim || !anim->IsHitStopped())
    {
        m_stateTimer -= dt;
    }

    const bool isAnimDone{ !anim || anim->GetCurrentPhase() >= 0.99f || m_stateTimer <= 0.0f };

    switch (m_phase)
    {
    case HitReactPhase::Dead:
        // Terminal state: remain lying on floor; never transition to GetUp or Locomotion
        if (motor)
        {
            motor->SetDesiredDirection({ 0.0f, 0.0f });
        }
        break;

    case HitReactPhase::GroundedFlinch:
        // If knocked off a ledge mid-stagger, fall normally
        if (motor && !motor->isGrounded() && motor->GetVerticalVelocity() < -1.5f)
        {
            blackboard.actionIndex = blackboard.getFlag(AnimFlag::is_combat_active) ? 5 : 2;
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AirTraversal));
            return;
        }

        if (isAnimDone)
        {
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
        }
        break;

    case HitReactPhase::AirFlinch:
        // Land as usual if feet touch the ground during air stagger
        if (motor && motor->isGrounded() && motor->GetVerticalVelocity() <= 0.05f)
        {
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Landing));
            return;
        }

        // Once the air hit flinch finishes in mid-air, hand off cleanly to normal AirTraversal falling
        if (isAnimDone)
        {
            blackboard.actionIndex = blackboard.getFlag(AnimFlag::is_combat_active) ? 5 : 2; // Jump_Combat_Loop / Jump_Loop
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AirTraversal));
        }
        break;

    case HitReactPhase::Knockdown:
        if (isAnimDone && (!motor || motor->isGrounded()))
        {
            TransitionToGetUp(controller);
        }
        break;

    case HitReactPhase::GetUp:
        if (anim)
        {
            for (const auto& ev : anim->GetFiredEvents())
            {
                if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Evade))
                {
                    m_canCancelEvade = true;
                }
                else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Locomotion))
                {
                    m_canCancelLocomotion = true;
                }
            }
        }

        {
            const auto& intent{ controller->GetIntent() };
            const float inputSq{ (intent.moveVector.x * intent.moveVector.x) + (intent.moveVector.y * intent.moveVector.y) };

            if (m_canCancelEvade && intent.bDashTriggered)
            {
                controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::DashEvade));
                return;
            }

            if (m_canCancelLocomotion && inputSq > 0.01f)
            {
                controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
                return;
            }
        }

        if (isAnimDone)
        {
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
        }
        break;

    default:
        break;
    }
}

void PlayerHitReact::Exit(PlayerControllerComponent* controller)
{
    if (!controller)
    {
        return;
    }

    // Clear wake-up invulnerability unless player is dead
    if (m_phase != HitReactPhase::Dead)
    {
        controller->SetInvincible(false);
    }

    // Replenish poise gauge back to max upon recovering from hit reaction 
    if (auto* owner{ controller->GetOwner() })
    {
        if (auto* health{ owner->GetComponent<HealthComponent>() })
        {
            health->ResetPoise();
        }
    }
}

// SKILLS & BUFFS
void PlayerSkillBuff::Enter(PlayerControllerComponent* controller)
{
    auto& blackboard{ controller->getAnimBlackboard() };
    blackboard.setFlag(AnimFlag::is_combat_active, true);

    // Set 10-second prototype duration
    blackboard.skillBuffTimer = 10.0f;

    // Toggle buff globally via shared pod memory 
    blackboard.setFlag(AnimFlag::is_speed_buff_active, true);

    if (auto* motor{ controller->GetMovement() })
    {
        motor->SetDesiredDirection({ 0.0f, 0.0f });
        motor->HaltMomentum(1.0f);
    }

    if (auto* anim{ controller->GetAnimation() })
    {
        anim->PlaySlot(AnimSlot::SkillBuff, true);
        m_timer = anim->GetSlotDuration(AnimSlot::SkillBuff);
    }
}

void PlayerSkillBuff::Update(PlayerControllerComponent* controller, float dt)
{
    m_timer -= dt;
    if (m_timer <= 0.0f)
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
    }
}

void PlayerSkillBuff::Exit(PlayerControllerComponent*) {}

void PlayerAttackSpeed::Enter(PlayerControllerComponent* controller)
{
    auto& blackboard{ controller->getAnimBlackboard() };
    blackboard.setFlag(AnimFlag::is_combat_active, true);

    auto* motor{ controller->GetMovement() };
    m_isAerial = motor && !motor->isGrounded();
    m_phase = SpeedAttackPhase::Start;
    m_canCancelCombo = false;
    m_canCancelEvade = false;
    m_canCancelLocomotion = false;
    m_hasAuthoredEvadeCancel = false;
    m_loopTimer = 0.0f;
    m_mashGraceTimer = 0.0f;

    const auto& intent{ controller->GetIntent() };
    m_facingDir = m_isAerial
        ? FacingResolver::ResolveDirectionOrCurrentFacing(controller->GetOwner(), intent.worldMoveDirection)
        : ResolveSoftLockOrFacingDirection(controller->GetOwner(), intent.worldMoveDirection);
    FacingResolver::SnapFaceDirection(controller->GetOwner(), m_facingDir);

    if (motor)
    {
        m_wasGravityEnabled = motor->GetConfig().useGravity;
        if (m_isAerial)
        {
            motor->GetConfig().useGravity = false;
            motor->SetVerticalVelocity(0.0f);
            blackboard.setFlag(AnimFlag::has_air_attacked, true);
        }
        motor->SetDesiredDirection({ 0.0f, 0.0f });
    }

    blackboard.actionIndex = 0;
    if (auto* anim{ controller->GetAnimation() })
    {
        anim->PlaySlot(m_isAerial ? AnimSlot::Attack_Speed_Aerial : AnimSlot::Attack_Speed_Ground, true);
        m_stateTimer = anim->GetSlotDuration(m_isAerial ? AnimSlot::Attack_Speed_Aerial : AnimSlot::Attack_Speed_Ground);

        if (const auto* activeNode{ anim->GetActiveAnimNode() })
        {
            for (const auto& ev : activeNode->events)
            {
                if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Evade))
                {
                    m_hasAuthoredEvadeCancel = true;
                    break;
                }
            }
        }
    }
}

void PlayerAttackSpeed::Update(PlayerControllerComponent* controller, float dt)
{
    m_stateTimer -= dt;
    if (m_mashGraceTimer > 0.0f) m_mashGraceTimer -= dt;

    auto* anim{ controller->GetAnimation() };
    auto* motor{ controller->GetMovement() };
    auto& blackboard{ controller->getAnimBlackboard() };
    const auto& intent{ controller->GetIntent() };

    if (intent.bAttackPressed) m_mashGraceTimer = 0.5f;

    for (const auto& ev : anim->GetFiredEvents())
    {
        if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Combo))
        {
            m_canCancelCombo = true;
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Evade))
        {
            m_canCancelEvade = true;
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Locomotion))
        {
            m_canCancelLocomotion = true;
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Lunge_Impulse) && motor)
        {
            if (m_isAerial)
            {
                ApplyAirLungeKinematics(controller, motor, m_facingDir, ev.payload, dt);
            }
            else
            {
                ApplyGroundLungeKinematics(controller, motor, m_facingDir, ev.payload);
            }
        }
    }

    if (m_isAerial && motor && motor->isGrounded())
    {
        controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
        return;
    }

    if (m_phase == SpeedAttackPhase::Start)
    {
        if (m_canCancelCombo || m_stateTimer <= 0.05f)
        {
            m_phase = SpeedAttackPhase::Loop;
            m_canCancelCombo = false;
            m_canCancelEvade = false;
            m_canCancelLocomotion = false;
            blackboard.actionIndex = 1;
            anim->PlaySlot(m_isAerial ? AnimSlot::Attack_Speed_Aerial : AnimSlot::Attack_Speed_Ground, true);
        }
    }
    else if (m_phase == SpeedAttackPhase::Loop)
    {
        m_loopTimer += dt;

        const bool isSustaining = intent.bAttackHeld || (m_mashGraceTimer > 0.0f);
        if (m_loopTimer > 3.0f || (m_loopTimer > 0.4f && !isSustaining))
        {
            m_phase = SpeedAttackPhase::End;
            m_canCancelCombo = false;
            m_canCancelEvade = false;
            m_canCancelLocomotion = false;
            blackboard.actionIndex = 2;
            anim->PlaySlot(m_isAerial ? AnimSlot::Attack_Speed_Aerial : AnimSlot::Attack_Speed_Ground, true);
            m_stateTimer = anim->GetSlotDuration(m_isAerial ? AnimSlot::Attack_Speed_Aerial : AnimSlot::Attack_Speed_Ground);
        }
    }
    else if (m_phase == SpeedAttackPhase::End)
    {
        const bool canEvade{ m_canCancelEvade || (!m_hasAuthoredEvadeCancel && m_canCancelCombo) };
        if (canEvade && intent.bDashTriggered)
        {
            if (motor->isGrounded() || !blackboard.getFlag(AnimFlag::has_air_dashed))
            {
                controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::DashEvade));
                return;
            }
        }

        const float inputSq{ (intent.moveVector.x * intent.moveVector.x) + (intent.moveVector.y * intent.moveVector.y) };
        if (m_canCancelLocomotion && inputSq > 0.01f && motor && motor->isGrounded())
        {
            controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
            return;
        }

        const bool isAnimationDone{ anim && (anim->GetCurrentPhase() >= 0.999f || m_stateTimer <= 0.0f) };
        if (isAnimationDone)
        {
            if (motor && motor->isGrounded())
            {
                controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::Locomotion));
            }
            else
            {
                blackboard.actionIndex = blackboard.getFlag(AnimFlag::is_combat_active) ? 5 : 2;
                controller->GetStateMachine()->ChangeState(controller, controller->GetState(PlayerStateType::AirTraversal));
            }
        }
    }
}

void PlayerAttackSpeed::Exit(PlayerControllerComponent* controller)
{
    if (auto* motor{ controller->GetMovement() })
    {
        motor->GetConfig().useGravity = m_wasGravityEnabled;
    }

    ClosePlayerHitbox(controller);
}