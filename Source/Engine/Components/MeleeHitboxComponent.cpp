#include <algorithm>
#include <cmath>
#include <imgui.h>
#include "System/PhysicsManager.h"
#include "System/ShapeRenderer.h"
#include "AIControllerComponent.h"
#include "AnimationComponent.h"
#include "ComponentRegistry.h"
#include "EditorManager.h"
#include "GameObject.h"
#include "HealthComponent.h"
#include "MeleeHitboxComponent.h"
#include "PlayerControllerComponent.h"

MeleeHitboxComponent::MeleeHitboxComponent(const MeleeHitboxConfig& config) noexcept
    : m_config{ config }
{}

void MeleeHitboxComponent::ResolveAnimationSource() noexcept
{
    m_animation = nullptr;
    // Ascend hierarchy to find root character actor (Player or Enemy)
    for (GameObject* curr = m_owner; curr != nullptr; curr = curr->GetParent())
    {
        if (auto* anim = curr->GetComponent<AnimationComponent>())
        {
            m_animation = anim;
            // Auto-orient default Enemy target mask toward Player when attached to an AI character
            if (curr->GetComponent<AIControllerComponent>() && m_config.targetLayer == CollisionLayer::Enemy)
            {
                m_config.targetLayer = CollisionLayer::Player;
            }
            break;
        }
    }
}

void MeleeHitboxComponent::OnAttach(GameObject* owner) noexcept
{
    IComponent::OnAttach(owner);
    ResolveAnimationSource();
}

void MeleeHitboxComponent::OpenHitbox(const float damageMultiplier) noexcept
{
    m_isActive = true;
    m_activeDamageMultiplier = (damageMultiplier > 0.001f) ? damageMultiplier : 1.0f;
    m_victimCount = 0;
    m_hitVictims.fill(nullptr);
}

void MeleeHitboxComponent::CloseHitbox() noexcept
{
    m_isActive = false;
    m_isParrySweepOnly = false;
    m_activeDamageMultiplier = 1.0f;
    m_victimCount = 0;
    m_hitVictims.fill(nullptr);
    m_activeWindowIndex = -1;
}

void MeleeHitboxComponent::ExecuteCleaveSweep() noexcept
{
    if (!m_owner) return;

    auto* scene{ PhysicsManager::Instance().GetScene() };
    if (!scene) return;

    // Isolate the owner's unscaled world frame to prevent weapon mesh scale from skewing the hitbox
    const Transform& t{ m_owner->transform };
    const DirectX::XMFLOAT4X4 worldF4{ t.GetWorldMatrix() };
    const DirectX::XMMATRIX objWorld{ DirectX::XMLoadFloat4x4(&worldF4) };

    DirectX::XMVECTOR vScale, vRotQuat, vTrans;
    if (!DirectX::XMMatrixDecompose(&vScale, &vRotQuat, &vTrans, objWorld))
    {
        return;
    }

    const DirectX::XMMATRIX socketWorldUnscaled{
        DirectX::XMMatrixRotationQuaternion(vRotQuat) *
        DirectX::XMMatrixTranslationFromVector(vTrans)
    };

    const DirectX::XMMATRIX locRot{ DirectX::XMMatrixRotationRollPitchYaw(
        DirectX::XMConvertToRadians(m_config.localRotation.x),
        DirectX::XMConvertToRadians(m_config.localRotation.y),
        DirectX::XMConvertToRadians(m_config.localRotation.z))
    };

    const DirectX::XMMATRIX locTrans{ DirectX::XMMatrixTranslation(
        m_config.localOffset.x,
        m_config.localOffset.y,
        m_config.localOffset.z)
    };

    const DirectX::XMMATRIX hitboxWorld{ locRot * locTrans * socketWorldUnscaled };

    DirectX::XMVECTOR vFinalScale, vFinalRotQuat, vFinalTrans;
    DirectX::XMMatrixDecompose(&vFinalScale, &vFinalRotQuat, &vFinalTrans, hitboxWorld);

    DirectX::XMFLOAT3 centerPos{};
    DirectX::XMFLOAT4 rotQuat{};
    DirectX::XMStoreFloat3(&centerPos, vFinalTrans);
    DirectX::XMStoreFloat4(&rotQuat, vFinalRotQuat);

    // Setup PhysX overlap sweep geometry 
    const physx::PxCapsuleGeometry capsuleGeom{
        (std::max)(0.01f, m_config.radius),
        (std::max)(0.01f, m_config.height * 0.5f)
    };

    physx::PxTransform pxPose{};
    pxPose.p = physx::PxVec3{ centerPos.x, centerPos.y, centerPos.z };

    // In PhysX, capsule axis lies along X. Rotate X into Y by 90 deg around Z to match ShapeRenderer/DirectX
    const physx::PxQuat qAlignY{ physx::PxHalfPi, physx::PxVec3{ 0.0f, 0.0f, 1.0f } };
    const physx::PxQuat qWorld{ rotQuat.x, rotQuat.y, rotQuat.z, rotQuat.w };
    pxPose.q = qWorld * qAlignY;

    physx::PxOverlapBufferN<32> overlapBuffer{};
    physx::PxQueryFilterData filterData{};
    // eNO_BLOCK: 最初のヒットでクエリを中断せず、全重複アクターを配列に漏れなく収集
    filterData.flags = physx::PxQueryFlag::eDYNAMIC | physx::PxQueryFlag::eNO_BLOCK;

    // Resolve the attacking actor's root entity (Player or Enemy) to prevent self-inflicted hits
    const GameObject* attackerRoot{ m_owner };
    while (attackerRoot && attackerRoot->GetParent())
    {
        if (attackerRoot->GetComponent<AnimationComponent>())
        {
            break;
        }
        attackerRoot = attackerRoot->GetParent();
    }

    const bool isAttackerAi{ attackerRoot != nullptr && attackerRoot->GetComponent<AIControllerComponent>() != nullptr };
    const std::uint32_t effectiveTargetLayer{ (isAttackerAi && m_config.targetLayer == CollisionLayer::Enemy)
        ? static_cast<std::uint32_t>(CollisionLayer::Player)
        : m_config.targetLayer };

    // Multi-Hit Overlap Query (Elden Ring 準拠のカプセル間接触走査)
    if (scene->overlap(capsuleGeom, pxPose, overlapBuffer, filterData, nullptr))
    {
        const physx::PxU32 hitCount{ overlapBuffer.getNbAnyHits() };

        for (physx::PxU32 i{ 0 }; i < hitCount; ++i)
        {
            const physx::PxOverlapHit& hit{ overlapBuffer.getAnyHit(i) };
            if (!hit.actor) continue;

            // userData を actor または shape から安全に取得
            void* rawUserData = hit.actor->userData;
            if (!rawUserData && hit.shape)
            {
                rawUserData = hit.shape->userData;
            }
            if (!rawUserData) continue;

            auto* victimObj{ static_cast<GameObject*>(rawUserData) };
            if (!victimObj) continue;

            // Exclude the weapon socket, immediate parent, and the attacker's root character hierarchy
            if (victimObj == m_owner || victimObj == attackerRoot) continue;
            if (m_owner && victimObj == m_owner->GetParent()) continue;

            auto* victimAi{ victimObj->GetComponent<AIControllerComponent>() };

            // Hard Faction Isolation Invariant: Enemies never friendly-fire or knock back other enemies
            if (isAttackerAi && victimAi != nullptr) continue;

            // Strict layer mask gate: isolates Player vs. Enemy targets
            auto* victimCapsule{ victimObj->GetComponent<CapsuleColliderComponent>() };
            const bool matchesTargetLayer{ victimCapsule && (victimCapsule->GetConfig().layer & effectiveTargetLayer) != 0 };
            if (!matchesTargetLayer) continue;

            // Ignore dead corpses and ground-recovery / wake-up invulnerability frames
            auto* victimPlayer{ victimObj->GetComponent<PlayerControllerComponent>() };
            if (const auto* victimHealth{ victimObj->GetComponent<HealthComponent>() }; victimHealth && victimHealth->IsDead()) continue;
            if (victimAi && victimAi->IsInvincible()) continue;
            if (victimPlayer && victimPlayer->IsInvincible()) continue;

            // 重複排除チェック: 1回のスイング内で同一敵カプセルが多段ヒットするのを防止
            bool alreadyHit{ false };
            for (std::size_t v{ 0 }; v < m_victimCount; ++v)
            {
                if (m_hitVictims[v] == victimObj)
                {
                    alreadyHit = true;
                    break;
                }
            }
            if (alreadyHit) continue;

            // Record victim into swing history
            if (m_victimCount < s_maxVictimsPerSwing)
            {
                m_hitVictims[m_victimCount++] = victimObj;
            }

            // Determine active attack classification from Player's animation slot and node
            Engine::Animation::AnimSlot attackSlot{ Engine::Animation::AnimSlot::None };
            std::size_t attackNodeIdx{ 0 };
            const AnimNode* activeNode{ m_animation ? m_animation->GetActiveAnimNode() : nullptr };
            if (m_animation && m_animation->GetCurrentStateIndex() < m_animation->GetStates().size())
            {
                const auto& activeAnimState{ m_animation->GetStates()[m_animation->GetCurrentStateIndex()] };
                attackSlot = activeAnimState.slot;
                attackNodeIdx = m_animation->GetCurrentNodeIndex();
            }

            const bool isLauncher{ attackSlot == Engine::Animation::AnimSlot::Attack_Directional && attackNodeIdx == 0 };
            const bool isAirCombo{ attackSlot == Engine::Animation::AnimSlot::Attack_Aerial || attackSlot == Engine::Animation::AnimSlot::Attack_Speed_Aerial };
            const bool isPlunge{ attackSlot == Engine::Animation::AnimSlot::Attack_Plunge };
            // Node 2 represents the Finisher hit in both Attack_Aerial and Attack_Speed_Aerial
            const bool isAirFinisher{ isAirCombo && (attackNodeIdx == 2) };

            DirectX::XMFLOAT3 impulse{ 0.0f, 0.0f, 0.0f };
            float calculatedLaunchVelocityY{ 10.5f };

            if (isLauncher)
            {
                // Dynamic Kinematic Prediction: Calculate exact launch height from timeline events
                const float attackerYawRad{ DirectX::XMConvertToRadians(attackerRoot ? attackerRoot->transform.rotation.y : m_owner->transform.rotation.y) };
                const DirectX::XMFLOAT2 fwd{ std::sin(attackerYawRad), std::cos(attackerYawRad) };

                float playerLiftV_Y{ 11.0f };
                float liftNormTime{ 0.2595f };
                float cancelNormTime{ 0.8659f };
                float lungeImpulseVal{ 0.0f };

                if (activeNode)
                {
                    for (const auto& ev : activeNode->events)
                    {
                        if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Lunge_Vertical))
                        {
                            playerLiftV_Y = ev.payload;
                            liftNormTime = ev.normalizedTime;
                        }
                        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Cancel_Combo))
                        {
                            cancelNormTime = ev.normalizedTime;
                        }
                        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Lunge_Impulse))
                        {
                            lungeImpulseVal = ev.payload;
                        }
                    }
                }

                const float stateDuration{ m_animation->GetSlotDuration(attackSlot) };
                const float effectiveDuration{ (stateDuration > 0.001f) ? stateDuration : 1.25f };
                const float currentPhase{ m_animation->GetCurrentPhase() };
                const float remainingNorm{ (cancelNormTime > currentPhase) ? (cancelNormTime - currentPhase) : (cancelNormTime - liftNormTime) };
                const float timeToCancel{ (std::max)(0.1f, remainingNorm * effectiveDuration) };

                auto* playerMotor{ attackerRoot ? attackerRoot->GetComponent<CharacterMovementComponent>() : nullptr };
                const float grav{ playerMotor ? std::abs(playerMotor->GetConfig().gravity) : 9.81f };
                const float currentVelY{ playerMotor ? playerMotor->GetVerticalVelocity() : playerLiftV_Y };

                // Predict player rise height at cancel window: y(t) = v0*t - 0.5*g*t^2
                const float predictedRise{ (currentVelY * timeToCancel) - (0.5f * grav * timeToCancel * timeToCancel) };
                constexpr float sweetSpotOffsetY{ 0.35f };
                const float targetApexHeight{ ((predictedRise > 1.0f) ? predictedRise : 5.5f) + sweetSpotOffsetY };

                // Solve required launch velocity to reach apex under natural gravity: v = sqrt(2 * g * H)
                calculatedLaunchVelocityY = std::sqrt(2.0f * grav * targetApexHeight);

                // Horizontal alignment: carry enemy forward to maintain sweet-spot spacing (1.3m)
                float playerForwardTravel{ 0.0f };
                if (playerMotor && lungeImpulseVal > 0.001f)
                {
                    const float drag{ (std::max)(0.1f, playerMotor->GetConfig().impulseDrag) };
                    playerForwardTravel = (lungeImpulseVal / drag) * (1.0f - std::exp(-drag * timeToCancel));
                }

                const DirectX::XMFLOAT3 pPos{ attackerRoot ? attackerRoot->GetPosition() : DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f } };
                const DirectX::XMFLOAT3 ePos{ victimObj->GetPosition() };
                const float currentDistFwd{ ((ePos.x - pPos.x) * fwd.x) + ((ePos.z - pPos.z) * fwd.y) };
                constexpr float sweetSpotDist{ 1.3f };
                const float targetForwardDisplacement{ (playerForwardTravel + sweetSpotDist) - currentDistFwd };

                auto* enemyMotor{ victimObj->GetComponent<CharacterMovementComponent>() };
                const float enemyDrag{ enemyMotor ? (std::max)(0.1f, enemyMotor->GetConfig().impulseDrag) : 3.5f };
                const float forwardImpulse{ std::clamp(targetForwardDisplacement * enemyDrag, 0.0f, 25.0f) };

                impulse.x = fwd.x * forwardImpulse;
                impulse.y = 0.0f;
                impulse.z = fwd.y * forwardImpulse;
            }
            else if (isAirCombo)
            {
                // Air Juggle: Zero radial knockback to prevent victim from flying away
                impulse = { 0.0f, 0.0f, 0.0f };
            }
            else if (isPlunge)
            {
                // Downward Spike: Handled directly by NotifySpike
                impulse = { 0.0f, 0.0f, 0.0f };
            }
            else
            {
                // Standard Ground Knockback: compute outward radial vector away from attacker
                const DirectX::XMFLOAT4X4 vWorld{ victimObj->transform.GetWorldMatrix() };
                const DirectX::XMVECTOR vVictimPos{ DirectX::XMVectorSet(vWorld._41, vWorld._42, vWorld._43, 0.0f) };
                const DirectX::XMFLOAT4X4 attackerWorld{ attackerRoot ? attackerRoot->transform.GetWorldMatrix() : worldF4 };
                const DirectX::XMVECTOR vAttackerPos{ DirectX::XMVectorSet(attackerWorld._41, attackerWorld._42, attackerWorld._43, 0.0f) };

                DirectX::XMVECTOR vKnockDir{ DirectX::XMVectorSubtract(vVictimPos, vAttackerPos) };
                vKnockDir = DirectX::XMVectorSetY(vKnockDir, 0.0f);

                if (DirectX::XMVector3Equal(vKnockDir, DirectX::XMVectorZero()) || DirectX::XMVector3LengthSq(vKnockDir).m128_f32[0] < 0.0001f)
                {
                    const float yawRad{ DirectX::XMConvertToRadians(attackerRoot ? attackerRoot->transform.rotation.y : m_owner->transform.rotation.y) };
                    vKnockDir = DirectX::XMVectorSet(std::sin(yawRad), 0.0f, std::cos(yawRad), 0.0f);
                }
                vKnockDir = DirectX::XMVector3Normalize(vKnockDir);

                const float finalKnockback{ m_config.knockbackImpulse * m_activeDamageMultiplier };
                DirectX::XMStoreFloat3(&impulse, DirectX::XMVectorScale(vKnockDir, finalKnockback));
                // Clamp vertical impulse to 0 for player ground hits and AI get-up recovery so feet stay planted
                impulse.y = (victimPlayer || (victimAi && victimAi->IsGetUp())) ? 0.0f : 2.5f;
            }

            // Parry Interception: Check if player's active Parry_Window intersects this swing before applying damage
            if (victimPlayer)
            {
                const DirectX::XMFLOAT4X4 attackerWorld{ attackerRoot ? attackerRoot->transform.GetWorldMatrix() : worldF4 };
                const DirectX::XMFLOAT3 attackerPos{ attackerWorld._41, attackerWorld._42, attackerWorld._43 };

                if (victimPlayer->TryConsumeParry(attackerPos, m_currentSlashDir))
                {
                    if (attackerRoot)
                    {
                        if (auto* attackerAi{ attackerRoot->GetComponent<AIControllerComponent>() })
                        {
                            const DirectX::XMFLOAT4X4 vWorld{ victimObj->transform.GetWorldMatrix() };
                            const DirectX::XMFLOAT3 playerPos{ vWorld._41, vWorld._42, vWorld._43 };
                            // Pass 0.0f hit-stop so the parried enemy transitions straight into slow-motion stagger
                            attackerAi->NotifyParried(playerPos, m_config.poiseDamage, 0.0f);
                        }
                    }
                    CloseHitbox();
                    return;
                }

                // If sweeping during an early enemy Parry_Window before Hitbox_Window opens, never deal damage
                if (m_isParrySweepOnly)
                {
                    continue;
                }
            }

            // Deliver damage, poise break evaluation, and hit-stop to victim
            bool poiseBrokeOrDead{ false };
            if (auto* health{ victimObj->GetComponent<HealthComponent>() })
            {
                poiseBrokeOrDead = health->ApplyDamage(
                    m_config.baseDamage * m_activeDamageMultiplier,
                    m_config.poiseDamage * m_activeDamageMultiplier,
                    impulse,
                    m_config.hitStopDuration
                );
            }

            // Freeze victim animation during impact shock
            if (auto* victimAnim{ victimObj->GetComponent<AnimationComponent>() })
            {
                victimAnim->ApplyHitStop(m_config.hitStopDuration);
            }

            // State and juggle transitions
            if (victimAi)
            {
                if (isLauncher)
                {
                    victimAi->NotifyLaunch(calculatedLaunchVelocityY);
                }
                else if (isAirCombo)
                {
                    if (victimAi->IsGetUp())
                    {
                        victimAi->NotifyHitReaction(m_currentSlashDir);
                    }
                    else
                    {
                        float holdDuration{ 0.55f };
                        if (isAirFinisher && activeNode)
                        {
                            const float stateDuration{ m_animation->GetSlotDuration(attackSlot) };
                            const float currentPhase{ m_animation->GetCurrentPhase() };
                            if (stateDuration > 0.001f && currentPhase < 1.0f)
                            {
                                holdDuration = std::clamp((1.0f - currentPhase) * stateDuration, 0.30f, 0.80f);
                            }
                        }
                        else if (activeNode)
                        {
                            // Regular air hits: Hold enemy through the full attack animation and cancel window.
                            // Release is handled deterministically by PlayerAttackAerial::Exit when the attack concludes.
                            const float stateDuration{ m_animation->GetSlotDuration(attackSlot) };
                            const float currentPhase{ m_animation->GetCurrentPhase() };
                            const float remainingTime{ (stateDuration > 0.001f && currentPhase < 1.0f) ? ((1.0f - currentPhase) * stateDuration) : 0.85f };
                            holdDuration = (std::max)(1.2f, remainingTime + 0.6f);
                        }
                        victimAi->NotifyAirJuggle(m_config.hitStopDuration, isAirFinisher, holdDuration, m_currentSlashDir);
                    }
                }
                else if (isPlunge)
                {
                    victimAi->NotifySpike(-22.0f);
                }
                else if (poiseBrokeOrDead)
                {
                    victimAi->NotifyHitReaction(m_currentSlashDir);
                }
            }
            else if (victimPlayer)
            {
                const DirectX::XMFLOAT4X4 attackerWorld{ attackerRoot ? attackerRoot->transform.GetWorldMatrix() : worldF4 };
                const DirectX::XMFLOAT3 attackerPos{ attackerWorld._41, attackerWorld._42, attackerWorld._43 };

                // Trigger Knockdown only on AI combo finisher (3rd strike), ambush running slash, or high-poise heavy attacks (>= 75)
                const bool isHeavyKnockdown{
                    (isAttackerAi && attackSlot == Engine::Animation::AnimSlot::Attack_Primary && attackNodeIdx >= 2) ||
                    (isAttackerAi && attackSlot == Engine::Animation::AnimSlot::Attack_Contextual) ||
                    ((m_config.poiseDamage * m_activeDamageMultiplier) >= 75.0f)
                };

                victimPlayer->NotifyHitReaction(attackerPos, m_currentSlashDir, isHeavyKnockdown);
            }

            // Freeze attacker's weapon animation directly (75% resistance ratio) for heavy tactile drag
            if (m_animation)
            {
                m_animation->ApplyHitStop(m_config.hitStopDuration * 0.75f);
            }
        }
    }
}

void MeleeHitboxComponent::Update(const float /*dt*/)
{
    if (!m_animation)
    {
        ResolveAnimationSource();
    }

    // Kinematic blade trajectory tracking: compute instantaneous velocity delta across frames
    if (m_owner)
    {
        const Transform& t{ m_owner->transform };
        const DirectX::XMFLOAT4X4 worldF4{ t.GetWorldMatrix() };
        const DirectX::XMMATRIX objWorld{ DirectX::XMLoadFloat4x4(&worldF4) };

        DirectX::XMVECTOR vScale, vRotQuat, vTrans;
        if (DirectX::XMMatrixDecompose(&vScale, &vRotQuat, &vTrans, objWorld))
        {
            const DirectX::XMMATRIX socketWorldUnscaled{
                DirectX::XMMatrixRotationQuaternion(vRotQuat) *
                DirectX::XMMatrixTranslationFromVector(vTrans)
            };

            const DirectX::XMMATRIX locRot{ DirectX::XMMatrixRotationRollPitchYaw(
                DirectX::XMConvertToRadians(m_config.localRotation.x),
                DirectX::XMConvertToRadians(m_config.localRotation.y),
                DirectX::XMConvertToRadians(m_config.localRotation.z))
            };

            const DirectX::XMMATRIX locTrans{ DirectX::XMMatrixTranslation(
                m_config.localOffset.x,
                m_config.localOffset.y,
                m_config.localOffset.z)
            };

            const DirectX::XMMATRIX hitboxWorld{ locRot * locTrans * socketWorldUnscaled };
            DirectX::XMVECTOR vFinalScale, vFinalRotQuat, vFinalTrans;
            DirectX::XMMatrixDecompose(&vFinalScale, &vFinalRotQuat, &vFinalTrans, hitboxWorld);

            DirectX::XMFLOAT3 currentPos{};
            DirectX::XMStoreFloat3(&currentPos, vFinalTrans);

            if (m_hasPrevHitboxPos)
            {
                m_currentSlashDir = {
                    currentPos.x - m_prevHitboxPos.x,
                    currentPos.y - m_prevHitboxPos.y,
                    currentPos.z - m_prevHitboxPos.z
                };
            }
            m_prevHitboxPos = currentPos;
            m_hasPrevHitboxPos = true;
        }
    }

    if (m_animation)
    {
        const AnimNode* activeNode{ m_animation->GetActiveAnimNode() };

        // Node switch or animation interrupt: reset active tracking and ensure hitbox closes
        if (activeNode != m_lastEvaluatedNode)
        {
            m_lastEvaluatedNode = activeNode;
            if (m_isActive)
            {
                CloseHitbox();
            }
            m_activeWindowIndex = -1;
        }

        if (activeNode)
        {
            const float currentPhase{ m_animation->GetCurrentPhase() };
            int matchedWindowIndex{ -1 };
            float matchedDamageMultiplier{ 1.0f };
            bool matchedParryOnly{ false };

            // Scan node events for active Hitbox_Window intervals first (authoritative damage + parry contact)
            for (std::size_t i{ 0 }; i < activeNode->events.size(); ++i)
            {
                const auto& ev{ activeNode->events[i] };
                if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Hitbox_Window))
                {
                    const float windowStart{ ev.normalizedTime };
                    const float windowEnd{ ev.isRange ? ev.normalizedEndTime : (ev.normalizedTime + 0.1f) };
                    if (currentPhase >= windowStart && currentPhase <= windowEnd)
                    {
                        matchedWindowIndex = static_cast<int>(i);
                        matchedDamageMultiplier = ev.payload;
                        matchedParryOnly = false;
                        break;
                    }
                }
            }

            // Optional early enemy Parry_Window: allows parry sweep before damage frames open if authored
            if (matchedWindowIndex == -1)
            {
                for (std::size_t i{ 0 }; i < activeNode->events.size(); ++i)
                {
                    const auto& ev{ activeNode->events[i] };
                    if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Parry_Window))
                    {
                        const float windowStart{ ev.normalizedTime };
                        const float windowEnd{ ev.isRange ? ev.normalizedEndTime : (ev.normalizedTime + 0.15f) };
                        if (currentPhase >= windowStart && currentPhase <= windowEnd)
                        {
                            matchedWindowIndex = static_cast<int>(i);
                            matchedDamageMultiplier = 1.0f;
                            matchedParryOnly = true;
                            break;
                        }
                    }
                }
            }

            if (matchedWindowIndex != -1)
            {
                // Entering a new window: activate hitbox and reset victim deduplication list
                if (m_activeWindowIndex != matchedWindowIndex)
                {
                    m_activeWindowIndex = matchedWindowIndex;
                    OpenHitbox(matchedDamageMultiplier);
                }
                m_isParrySweepOnly = matchedParryOnly;
            }
            else
            {
                // Exited window interval: deactivate hitbox
                if (m_activeWindowIndex != -1)
                {
                    m_activeWindowIndex = -1;
                    CloseHitbox();
                }
            }
        }
        else if (m_isActive)
        {
            CloseHitbox();
        }
    }

    // Only query PhysX when hitbox is active
    if (m_isActive)
    {
        ExecuteCleaveSweep();
    }
}

void MeleeHitboxComponent::DrawGizmo(const GizmoContext& ctx) noexcept
{
    if (!(ctx.categoryMask & static_cast<std::uint32_t>(GizmoCategory::Hitboxes)) || !ctx.shapes || !m_owner)
    {
        return;
    }

    const EditorMode mode{ EditorManager::Instance().GetEditorMode() };
    // In Play Mode, only render the gizmo during active swing frames to keep the screen clear of clutter
    if (mode == EditorMode::Play && !m_isActive)
    {
        return;
    }

    const DirectX::XMFLOAT4 capsuleColor = m_isActive
        ? DirectX::XMFLOAT4{ 1.0f, 0.15f, 0.15f, 0.95f }  // Active Red (Swinging Damage Window)
    : DirectX::XMFLOAT4{ 1.0f, 0.85f, 0.15f, 0.45f }; // Armed Yellow (Edit Mode Calibration Preview)

    const Transform& t{ m_owner->transform };
    const DirectX::XMFLOAT4X4 worldF4{ t.GetWorldMatrix() };
    const DirectX::XMMATRIX objWorld{ DirectX::XMLoadFloat4x4(&worldF4) };

    DirectX::XMVECTOR vScale, vRotQuat, vTrans;
    if (!DirectX::XMMatrixDecompose(&vScale, &vRotQuat, &vTrans, objWorld))
    {
        return;
    }

    const DirectX::XMMATRIX socketWorldUnscaled{
        DirectX::XMMatrixRotationQuaternion(vRotQuat) *
        DirectX::XMMatrixTranslationFromVector(vTrans)
    };

    const DirectX::XMMATRIX locRot{ DirectX::XMMatrixRotationRollPitchYaw(
        DirectX::XMConvertToRadians(m_config.localRotation.x),
        DirectX::XMConvertToRadians(m_config.localRotation.y),
        DirectX::XMConvertToRadians(m_config.localRotation.z))
    };

    const DirectX::XMMATRIX locTrans{ DirectX::XMMatrixTranslation(
        m_config.localOffset.x,
        m_config.localOffset.y,
        m_config.localOffset.z)
    };

    const DirectX::XMMATRIX hitboxWorld{ locRot * locTrans * socketWorldUnscaled };

    DirectX::XMFLOAT4X4 gizmoTransform{};
    DirectX::XMStoreFloat4x4(&gizmoTransform, hitboxWorld);

    ctx.shapes->DrawCapsule(gizmoTransform, m_config.radius, m_config.height, capsuleColor);
}

void MeleeHitboxComponent::DrawInspector()
{
    ImGui::TextDisabled("Blade Capsule Hitbox (Elden Ring Standard)");
    ImGui::Separator();

    ImGui::DragFloat("Radius", &m_config.radius, 0.01f, 0.05f, 5.0f, "%.2f m");
    ImGui::DragFloat("Height", &m_config.height, 0.02f, 0.1f, 10.0f, "%.2f m");
    ImGui::DragFloat3("Local Offset", &m_config.localOffset.x, 0.02f, -10.0f, 10.0f, "%.2f m");
    ImGui::DragFloat3("Local Rotation", &m_config.localRotation.x, 1.0f, -180.0f, 180.0f, "%.1f deg");

    ImGui::Spacing();
    ImGui::TextDisabled("COMBAT IMPACT TUNING");

    static constexpr const char* s_targetLayerNames[] = { "None", "WorldDynamic", "Player", "Enemy" };
    int targetLayerIdx{ 3 }; // Default Enemy
    if (m_config.targetLayer == CollisionLayer::None) targetLayerIdx = 0;
    else if (m_config.targetLayer == CollisionLayer::WorldDynamic) targetLayerIdx = 1;
    else if (m_config.targetLayer == CollisionLayer::Player) targetLayerIdx = 2;
    else if (m_config.targetLayer == CollisionLayer::Enemy) targetLayerIdx = 3;

    if (ImGui::Combo("Target Layer", &targetLayerIdx, s_targetLayerNames, 4))
    {
        switch (targetLayerIdx)
        {
        case 0: m_config.targetLayer = CollisionLayer::None; break;
        case 1: m_config.targetLayer = CollisionLayer::WorldDynamic; break;
        case 2: m_config.targetLayer = CollisionLayer::Player; break;
        case 3: m_config.targetLayer = CollisionLayer::Enemy; break;
        default: break;
        }
    }

    ImGui::DragFloat("Base Damage", &m_config.baseDamage, 1.0f, 0.0f, 1000.0f, "%.0f");
    ImGui::DragFloat("Poise Damage", &m_config.poiseDamage, 1.0f, 0.0f, 500.0f, "%.0f");
    ImGui::DragFloat("Knockback Impulse", &m_config.knockbackImpulse, 0.5f, 0.0f, 100.0f, "%.1f m/s");
    ImGui::DragFloat("Hit-Stop Duration", &m_config.hitStopDuration, 0.01f, 0.0f, 0.5f, "%.2f s");

    ImGui::Spacing();
    ImGui::TextDisabled("LIVE STATUS");
    ImGui::BeginDisabled();
    bool activeCheck{ m_isActive };
    ImGui::Checkbox("Hitbox Active", &activeCheck);
    ImGui::Text("Active Multiplier: %.2fx", m_activeDamageMultiplier);
    ImGui::Text("Victims Cleaved This Swing: %zu", m_victimCount);
    ImGui::EndDisabled();

    ImGui::Spacing();
    if (m_isActive)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(220, 40, 40, 255));
        if (ImGui::Button("Close Hitbox (Force Stop)##Manual", ImVec2(-1.0f, 26.0f)))
        {
            CloseHitbox();
        }
        ImGui::PopStyleColor();
    }
    else
    {
        if (ImGui::Button("Open Hitbox (Manual Swing Test)##Manual", ImVec2(-1.0f, 26.0f)))
        {
            OpenHitbox(1.0f);
        }
    }
}

void MeleeHitboxComponent::Serialize(nlohmann::json& outJson) const
{
    outJson["Radius"] = m_config.radius;
    outJson["Height"] = m_config.height;
    outJson["OffsetX"] = m_config.localOffset.x;
    outJson["OffsetY"] = m_config.localOffset.y;
    outJson["OffsetZ"] = m_config.localOffset.z;
    outJson["RotX"] = m_config.localRotation.x;
    outJson["RotY"] = m_config.localRotation.y;
    outJson["RotZ"] = m_config.localRotation.z;
    outJson["BaseDamage"] = m_config.baseDamage;
    outJson["PoiseDamage"] = m_config.poiseDamage;
    outJson["KnockbackImpulse"] = m_config.knockbackImpulse;
    outJson["HitStopDuration"] = m_config.hitStopDuration;
    outJson["TargetLayer"] = m_config.targetLayer;
}

void MeleeHitboxComponent::Deserialize(const nlohmann::json& inJson)
{
    m_config.radius = inJson.value("Radius", 0.35f);
    m_config.height = inJson.value("Height", 1.4f);
    m_config.localOffset.x = inJson.value("OffsetX", 0.0f);
    m_config.localOffset.y = inJson.value("OffsetY", 0.6f);
    m_config.localOffset.z = inJson.value("OffsetZ", 0.0f);
    m_config.localRotation.x = inJson.value("RotX", 0.0f);
    m_config.localRotation.y = inJson.value("RotY", 0.0f);
    m_config.localRotation.z = inJson.value("RotZ", 0.0f);
    m_config.baseDamage = inJson.value("BaseDamage", 35.0f);
    m_config.poiseDamage = inJson.value("PoiseDamage", 45.0f);
    m_config.knockbackImpulse = inJson.value("KnockbackImpulse", 18.0f);
    m_config.hitStopDuration = inJson.value("HitStopDuration", 0.06f);
    m_config.targetLayer = inJson.value("TargetLayer", CollisionLayer::Enemy);
}

REGISTER_COMPONENT(MeleeHitboxComponent)