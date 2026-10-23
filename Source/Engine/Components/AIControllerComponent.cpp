#include <algorithm>
#include <cmath>
#include <imgui.h>
#include "System/CollisionLayer.h"
#include "System/PhysicsManager.h"
#include "System/ShapeRenderer.h"
#include "AIControllerComponent.h"
#include "CapsuleColliderComponent.h"
#include "CharacterMovementComponent.h"
#include "ComponentRegistry.h"
#include "EditorManager.h"
#include "GameObject.h"
#include "HealthComponent.h"
#include "PlayerControllerComponent.h"
#include "Random.h"

namespace
{
    // Extracts authoritative world position from any GameObject regardless of hierarchy nesting
    [[nodiscard]] DirectX::XMFLOAT3 GetWorldPosition(const GameObject* go) noexcept
    {
        if (!go) return { 0.0f, 0.0f, 0.0f };
        const DirectX::XMFLOAT4X4 wMat{ go->transform.GetWorldMatrix() };
        return { wMat._41, wMat._42, wMat._43 };
    }
}

AIControllerComponent::AIControllerComponent(const AIControllerConfig& config) noexcept
    : m_config{ config }
{}

void AIControllerComponent::OnAttach(GameObject* owner) noexcept
{
    IComponent::OnAttach(owner);

    if (m_owner)
    {
        m_capsule = m_owner->GetComponent<CapsuleColliderComponent>();
        m_movement = m_owner->GetComponent<CharacterMovementComponent>();
        m_health = m_owner->GetComponent<HealthComponent>();

        // Cache initial spawn position in world coordinates for leash and wander constraints
        m_blackboard.spawnPosition = GetWorldPosition(m_owner);
        m_blackboard.wanderDestination = m_blackboard.spawnPosition;

        if (m_movement)
        {
            m_initialGravity = m_movement->GetConfig().useGravity;
        }

        FindCoverProps();

        // Configure initial state aligned to archetype
        switch (m_config.archetype)
        {
        case EnemyArchetype::grunt:
            ChangeState(EnemyState::idle);
            break;
        case EnemyArchetype::ground_ambush:
            ChangeState(EnemyState::ambush_dormant);
            break;
        case EnemyArchetype::wall_lurker:
            ChangeState(EnemyState::wall_hang);
            break;
        default:
            ChangeState(EnemyState::idle);
            break;
        }
    }
}

void AIControllerComponent::OnEnable() noexcept
{
    if (m_owner)
    {
        m_blackboard.spawnPosition = GetWorldPosition(m_owner);
    }
}

void AIControllerComponent::OnDisable() noexcept
{
    StopMovement();
}

void AIControllerComponent::ChangeState(const EnemyState newState) noexcept
{
    if (m_currentState == newState)
    {
        return;
    }

    // State exit logic
    if (m_currentState == EnemyState::wall_hang && m_movement)
    {
        // Restore standard gravity when detaching from wall perch
        m_movement->GetConfig().useGravity = m_initialGravity;
    }

    m_currentState = newState;
    m_blackboard.stateTimer = 0.0f;

    // State entry logic
    switch (m_currentState)
    {
    case EnemyState::wall_hang:
        if (m_movement)
        {
            // Suspend gravity and halt momentum to cling securely to wall
            m_movement->GetConfig().useGravity = false;
            m_movement->HaltMomentum(1.0f);
        }
        break;

    case EnemyState::wall_drop:
        if (m_movement)
        {
            m_movement->GetConfig().useGravity = true;
            // Inject negative downward impulse for a punchy drop
            m_movement->SetVerticalVelocity(-8.0f);
        }
        break;

    case EnemyState::ambush_burst:
        if (m_movement && m_blackboard.target)
        {
            // Inject high-speed burst impulse directly toward target in world space
            const DirectX::XMFLOAT3 currentPos{ GetWorldPosition(m_owner) };
            const DirectX::XMFLOAT3 targetPos{ GetWorldPosition(m_blackboard.target) };

            const DirectX::XMVECTOR vCurrent{ DirectX::XMLoadFloat3(&currentPos) };
            const DirectX::XMVECTOR vTarget{ DirectX::XMLoadFloat3(&targetPos) };
            DirectX::XMVECTOR vDir{ DirectX::XMVectorSubtract(vTarget, vCurrent) };
            vDir = DirectX::XMVectorSetY(vDir, 0.0f); // Ground plane only
            vDir = DirectX::XMVector3Normalize(vDir);

            DirectX::XMFLOAT3 launchDir{};
            DirectX::XMStoreFloat3(&launchDir, DirectX::XMVectorScale(vDir, 16.0f));

            // Instantly snap orientation toward player so burst lunges directly into combat
            const float targetYawDeg{ DirectX::XMConvertToDegrees(std::atan2(launchDir.x, launchDir.z)) };
            m_owner->transform.rotation.y = targetYawDeg;

            m_movement->AddImpulse(launchDir);
        }
        break;

    case EnemyState::idle:
    case EnemyState::attack_windup:
    case EnemyState::attack_active:
    case EnemyState::hit_react:
    case EnemyState::dead:
        StopMovement();
        break;

    default:
        break;
    }
}

void AIControllerComponent::AcquirePlayerTarget() noexcept
{
    if (m_blackboard.target)
    {
        return; // Already acquired
    }

    // Search siblings in scene root for PlayerControllerComponent
    if (!m_owner || !m_owner->GetParent())
    {
        return;
    }

    for (const auto& sibling : m_owner->GetParent()->GetChildren())
    {
        if (sibling && sibling->GetComponent<PlayerControllerComponent>())
        {
            m_blackboard.target = sibling.get();
            break;
        }
    }
}

void AIControllerComponent::UpdatePerception(const float dt) noexcept
{
    m_blackboard.perceptionTimer += dt;
    if (m_blackboard.perceptionTimer < m_config.perceptionInterval)
    {
        return;
    }
    m_blackboard.perceptionTimer = 0.0f;

    if (!m_blackboard.target)
    {
        AcquirePlayerTarget();
        if (!m_blackboard.target)
        {
            m_blackboard.setFlag(SensoryFlag::has_line_of_sight, false);
            return;
        }
    }

    const DirectX::XMFLOAT3 myPos{ GetWorldPosition(m_owner) };
    const DirectX::XMFLOAT3 targetPos{ GetWorldPosition(m_blackboard.target) };
    m_blackboard.targetPosition = targetPos;

    const DirectX::XMVECTOR vMyPos{ DirectX::XMLoadFloat3(&myPos) };
    const DirectX::XMVECTOR vTargetPos{ DirectX::XMLoadFloat3(&targetPos) };
    const DirectX::XMVECTOR vDiff{ DirectX::XMVectorSubtract(vTargetPos, vMyPos) };

    // Compute squared distance (Tier 1 SIMD without std::sqrt)
    const DirectX::XMVECTOR vDistSq{ DirectX::XMVector3LengthSq(vDiff) };
    DirectX::XMStoreFloat(&m_blackboard.targetDistanceSq, vDistSq);

    // AAA Territory Invariant: Check if player has escaped beyond the leash boundary
    const DirectX::XMVECTOR vSpawn{ DirectX::XMLoadFloat3(&m_blackboard.spawnPosition) };
    const DirectX::XMVECTOR vTargetFromSpawn{ DirectX::XMVectorSubtract(vTargetPos, vSpawn) };
    float targetDistFromSpawnSq{ 0.0f };
    DirectX::XMStoreFloat(&targetDistFromSpawnSq, DirectX::XMVector3LengthSq(vTargetFromSpawn));
    const float leashRangeSq{ m_config.leashRange * m_config.leashRange };
    m_blackboard.setFlag(SensoryFlag::is_target_beyond_leash, targetDistFromSpawnSq > leashRangeSq);

    // Evaluate Field of View (FOV) via dot product against owner forward vector
    const float yawRad{ DirectX::XMConvertToRadians(m_owner->transform.rotation.y) };
    const DirectX::XMVECTOR vForward{ DirectX::XMVectorSet(std::sin(yawRad), 0.0f, std::cos(yawRad), 0.0f) };
    DirectX::XMVECTOR vDirFlat{ DirectX::XMVectorSetY(vDiff, 0.0f) };
    vDirFlat = DirectX::XMVector3Normalize(vDirFlat);

    float dotVal{ 0.0f };
    DirectX::XMStoreFloat(&dotVal, DirectX::XMVector3Dot(vForward, vDirFlat));

    const float cosHalfFov{ std::cos(DirectX::XMConvertToRadians(m_config.sightFovDeg * 0.5f)) };
    const bool withinSightRange{ m_blackboard.targetDistanceSq <= (m_config.sightRange * m_config.sightRange) };
    m_blackboard.setFlag(SensoryFlag::is_target_in_fov, withinSightRange && (dotVal >= cosHalfFov));

    // PhysX raycast to verify unobstructed line of sight against static environment
    auto* scene{ PhysicsManager::Instance().GetScene() };
    if (!scene)
    {
        return;
    }

    const float eyeHeight{ m_capsule ? (m_capsule->GetTotalHalfHeight() * 1.5f) : 1.6f };
    const physx::PxVec3 rayOrigin{ myPos.x, myPos.y + eyeHeight, myPos.z };

    DirectX::XMFLOAT3 dirNorm{};
    DirectX::XMStoreFloat3(&dirNorm, DirectX::XMVector3Normalize(vDiff));
    const physx::PxVec3 rayDir{ dirNorm.x, dirNorm.y, dirNorm.z };

    const float distance{ std::sqrt(m_blackboard.targetDistanceSq) };

    physx::PxRaycastBuffer hitBuffer{};
    physx::PxQueryFilterData filterData{};
    filterData.data.word0 = CollisionLayer::WorldStatic;
    filterData.flags = physx::PxQueryFlag::eSTATIC;

    const bool isBlocked{ scene->raycast(rayOrigin, rayDir, distance, hitBuffer, physx::PxHitFlag::eDEFAULT, filterData) };

    // Line-of-sight is purely geometric (unobstructed raycast), allowing wide cover detection across territory
    m_blackboard.setFlag(SensoryFlag::has_line_of_sight, !isBlocked);

    // Dynamic Blind-Spot Occlusion Update (Runs whenever dormant, even if player is across the arena)
    if (m_config.archetype == EnemyArchetype::ground_ambush && m_currentState == EnemyState::ambush_dormant)
    {
        UpdateAmbushCover(dt);
    }
}

void AIControllerComponent::SteerTowards(const DirectX::XMFLOAT3& targetWorldPos, const float dt) noexcept
{
    if (!m_movement || !m_owner)
    {
        return;
    }

    const DirectX::XMFLOAT3 currentPos{ GetWorldPosition(m_owner) };
    const DirectX::XMVECTOR vCurrent{ DirectX::XMLoadFloat3(&currentPos) };
    const DirectX::XMVECTOR vTarget{ DirectX::XMLoadFloat3(&targetWorldPos) };

    DirectX::XMVECTOR vDir{ DirectX::XMVectorSubtract(vTarget, vCurrent) };
    vDir = DirectX::XMVectorSetY(vDir, 0.0f);

    // Evaluate 3D length so movement along the Z-axis is not treated as zero distance
    const DirectX::XMVECTOR vLengthSq{ DirectX::XMVector3LengthSq(vDir) };
    float lenSq{ 0.0f };
    DirectX::XMStoreFloat(&lenSq, vLengthSq);

    if (lenSq > 0.001f)
    {
        vDir = DirectX::XMVector3Normalize(vDir);
        DirectX::XMFLOAT3 normDir{};
        DirectX::XMStoreFloat3(&normDir, vDir);

        // Smoothly rotate the owner toward target direction (Shortest arc, rate-limited, zero snapping)
        const float targetYawDeg{ DirectX::XMConvertToDegrees(std::atan2(normDir.x, normDir.z)) };
        float deltaYaw{ targetYawDeg - m_owner->transform.rotation.y };

        while (deltaYaw > 180.0f) deltaYaw -= 360.0f;
        while (deltaYaw < -180.0f) deltaYaw += 360.0f;

        const float maxStep{ m_config.turnSpeedDeg * dt };
        const float step{ std::clamp(deltaYaw, -maxStep, maxStep) };
        m_owner->transform.rotation.y += step;

        while (m_owner->transform.rotation.y > 360.0f) m_owner->transform.rotation.y -= 360.0f;
        while (m_owner->transform.rotation.y < -360.0f) m_owner->transform.rotation.y += 360.0f;

        // Drive the motor
        m_movement->SetDesiredDirection({ normDir.x, normDir.z });
    }
    else
    {
        StopMovement();
    }
}

void AIControllerComponent::StopMovement() noexcept
{
    if (m_movement)
    {
        m_movement->SetDesiredDirection({ 0.0f, 0.0f });
    }
}

void AIControllerComponent::NotifyHitReaction() noexcept
{
    if (m_currentState == EnemyState::dead)
    {
        return;
    }
    ChangeState(EnemyState::hit_react);
}

void AIControllerComponent::TickGrunt(const float dt) noexcept
{
    const bool hasLos{ m_blackboard.getFlag(SensoryFlag::has_line_of_sight) };
    const bool inFov{ m_blackboard.getFlag(SensoryFlag::is_target_in_fov) };
    const float attackRangeSq{ m_config.attackRange * m_config.attackRange };

    // AAA Combat Gate: Block chase transitions if leashed, de-aggroed, or player fled past boundary
    const bool isLeashed{ m_blackboard.getFlag(SensoryFlag::is_leashed) };
    const bool isDeaggroActive{ m_blackboard.deaggroCooldownTimer > 0.0f };
    const bool isTargetPastLeash{ m_blackboard.getFlag(SensoryFlag::is_target_beyond_leash) };
    const bool canAggro{ !isLeashed && !isDeaggroActive && !isTargetPastLeash };

    switch (m_currentState)
    {
    case EnemyState::idle:
        StopMovement();
        if (canAggro && hasLos && inFov)
        {
            ChangeState(EnemyState::chase);
            break;
        }

        m_blackboard.stateTimer += dt;
        if (m_blackboard.stateTimer >= m_config.idleDuration)
        {
            // Pick random wander destination within wanderRadius around spawn position
            const float angle{ Random::Get<float>(0.0f, DirectX::XM_2PI) };
            const float maxDist{ (std::max)(0.5f, m_config.wanderRadius) };
            const float distance{ Random::Get<float>(0.5f, maxDist) };

            m_blackboard.wanderDestination = {
                m_blackboard.spawnPosition.x + (std::sin(angle) * distance),
                m_blackboard.spawnPosition.y,
                m_blackboard.spawnPosition.z + (std::cos(angle) * distance)
            };

            ChangeState(EnemyState::patrol);
        }
        break;

    case EnemyState::patrol:
        if (canAggro && hasLos && inFov)
        {
            ChangeState(EnemyState::chase);
            break;
        }

        m_blackboard.stateTimer += dt;
        SteerTowards(m_blackboard.wanderDestination, dt);

        // Check if reached destination or timed out
        {
            const DirectX::XMFLOAT3 currentPos{ m_owner->transform.position };
            const float dx{ m_blackboard.wanderDestination.x - currentPos.x };
            const float dz{ m_blackboard.wanderDestination.z - currentPos.z };
            const float distSq{ (dx * dx) + (dz * dz) };

            constexpr float arrivalThresholdSq{ 0.4f * 0.4f }; // Within 40 cm
            if (distSq <= arrivalThresholdSq || m_blackboard.stateTimer >= m_config.patrolTimeout)
            {
                ChangeState(EnemyState::idle);
            }
        }
        break;

    case EnemyState::chase:
        if (!hasLos)
        {
            // Lost sight; stand-down in idle
            ChangeState(EnemyState::idle);
            break;
        }

        if (m_blackboard.targetDistanceSq <= attackRangeSq)
        {
            if (m_blackboard.attackCooldownTimer <= 0.0f)
            {
                ChangeState(EnemyState::attack_windup);
            }
            else
            {
                StopMovement(); // In range, awaiting cooldown
            }
        }
        else
        {
            SteerTowards(m_blackboard.targetPosition, dt);
        }
        break;

    case EnemyState::attack_windup:
        m_blackboard.stateTimer += dt;
        StopMovement();
        if (m_blackboard.stateTimer >= m_config.attackWindupDuration)
        {
            ChangeState(EnemyState::attack_active);
        }
        break;

    case EnemyState::attack_active:
        m_blackboard.stateTimer += dt;
        StopMovement();
        if (m_blackboard.stateTimer >= m_config.attackActiveDuration)
        {
            m_blackboard.attackCooldownTimer = m_config.attackCooldownDuration;
            ChangeState(EnemyState::chase);
        }
        break;

    case EnemyState::hit_react:
        m_blackboard.stateTimer += dt;
        StopMovement();
        if (m_blackboard.stateTimer >= m_config.hitReactDuration)
        {
            ChangeState(EnemyState::chase);
        }
        break;

    default:
        break;
    }
}

void AIControllerComponent::TickGroundAmbush(const float dt) noexcept
{
    const float ambushRangeSq{ m_config.ambushTriggerRadius * m_config.ambushTriggerRadius };
    const float territoryRangeSq{ m_config.leashRange * m_config.leashRange };

    switch (m_currentState)
    {
    case EnemyState::ambush_dormant:
    {
        if (!m_blackboard.target)
        {
            StopMovement();
            break;
        }

        const DirectX::XMFLOAT3 currentPos{ GetWorldPosition(m_owner) };
        const DirectX::XMFLOAT3 playerPos{ GetWorldPosition(m_blackboard.target) };
        const bool hasLos{ m_blackboard.getFlag(SensoryFlag::has_line_of_sight) };
        const bool insideTerritory{ m_blackboard.targetDistanceSq <= territoryRangeSq };

        // Evaluate Player's facing orientation relative to the enemy
        const float playerYawRad{ DirectX::XMConvertToRadians(m_blackboard.target->transform.rotation.y) };
        const DirectX::XMVECTOR vPlayerFwd{ DirectX::XMVectorSet(std::sin(playerYawRad), 0.0f, std::cos(playerYawRad), 0.0f) };

        DirectX::XMVECTOR vToEnemy{ DirectX::XMVectorSet(currentPos.x - playerPos.x, 0.0f, currentPos.z - playerPos.z, 0.0f) };
        vToEnemy = DirectX::XMVector3Normalize(vToEnemy);

        float playerLookDot{ 0.0f };
        DirectX::XMStoreFloat(&playerLookDot, DirectX::XMVector3Dot(vPlayerFwd, vToEnemy));

        // TRIGGER 1: Proximity Trap (Player steps into the pillar gap/corridor)
        const bool playerInProximity{ m_blackboard.targetDistanceSq <= ambushRangeSq };

        // TRIGGER 2: Cover Blown / Spotted (Player looks directly at the hiding enemy with clear LOS)
        const bool coverBlown{ hasLos && insideTerritory && (playerLookDot > 0.35f) };

        // TRIGGER 3: Backstab Stalk (Player bypassed the pillar line and turned their back with clear LOS)
        const bool backstabOpportunity{ hasLos && insideTerritory && (playerLookDot < -0.1f) };

        if (playerInProximity || coverBlown || backstabOpportunity)
        {
            ChangeState(EnemyState::ambush_burst);
            break;
        }

        // Blind-Spot Repositioning: Stealthily creep behind the active cover obstacle
        const float dx{ m_blackboard.wanderDestination.x - currentPos.x };
        const float dz{ m_blackboard.wanderDestination.z - currentPos.z };
        const float distToHideSq{ (dx * dx) + (dz * dz) };

        // Threshold of 35 cm: creep toward hide point; once there, stop and orient toward flank
        if (distToHideSq > (0.35f * 0.35f))
        {
            SteerTowards(m_blackboard.wanderDestination, dt);
        }
        else
        {
            StopMovement();

            // Face toward anticipated player approach angle so enemy is primed to burst
            const float dxT{ playerPos.x - currentPos.x };
            const float dzT{ playerPos.z - currentPos.z };
            if ((dxT * dxT) + (dzT * dzT) > 0.01f)
            {
                const float targetYawDeg{ DirectX::XMConvertToDegrees(std::atan2(dxT, dzT)) };
                float deltaYaw{ targetYawDeg - m_owner->transform.rotation.y };
                while (deltaYaw > 180.0f) deltaYaw -= 360.0f;
                while (deltaYaw < -180.0f) deltaYaw -= 360.0f;
                m_owner->transform.rotation.y += std::clamp(deltaYaw, -m_config.turnSpeedDeg * dt, m_config.turnSpeedDeg * dt);
            }
        }
        break;
    }

    case EnemyState::ambush_burst:
        m_blackboard.stateTimer += dt;
        // Allow the launch impulse to carry the enemy forward out from cover before transitioning to chase
        if (m_blackboard.stateTimer >= 0.4f)
        {
            ChangeState(EnemyState::chase);
        }
        break;

        // AAA Invariant: Once combat begins, delegate ALL states (including Idle/Patrol/Leash) to TickGrunt so the AI never freezes
    case EnemyState::idle:
    case EnemyState::patrol:
    case EnemyState::chase:
    case EnemyState::attack_windup:
    case EnemyState::attack_active:
    case EnemyState::hit_react:
        TickGrunt(dt);
        break;

    default:
        break;
    }
}

void AIControllerComponent::TickWallLurker(const float dt) noexcept
{
    switch (m_currentState)
    {
    case EnemyState::wall_hang:
    {
        // Evaluate horizontal ground distance footprint directly below wall lurker
        const DirectX::XMFLOAT3 myPos{ m_owner->transform.position };
        const DirectX::XMFLOAT3 playerPos{ m_blackboard.targetPosition };

        const float dx{ playerPos.x - myPos.x };
        const float dz{ playerPos.z - myPos.z };
        const float horizontalDistSq{ (dx * dx) + (dz * dz) };

        const float dropRadiusSq{ m_config.wallDropTriggerRadius * m_config.wallDropTriggerRadius };

        // When player walks underneath or trips footprint, release from wall
        if (horizontalDistSq <= dropRadiusSq)
        {
            ChangeState(EnemyState::wall_drop);
        }
        break;
    }

    case EnemyState::wall_drop:
        // Free-fall until the capsule controller strikes ground geometry
        if (m_capsule && m_capsule->IsGrounded())
        {
            // Landed cleanly; immediately start chasing the surprised player
            ChangeState(EnemyState::chase);
        }
        break;

    case EnemyState::chase:
    case EnemyState::attack_windup:
    case EnemyState::attack_active:
    case EnemyState::hit_react:
        TickGrunt(dt);
        break;

    default:
        break;
    }
}

void AIControllerComponent::Update(const float dt)
{
    if (!m_owner)
    {
        return;
    }

    const EditorMode mode{ EditorManager::Instance().GetEditorMode() };
    const bool isPlayMode{ mode == EditorMode::Play };

    // EDIT MODE: Synchronize spawn anchor in real-time as the designer moves the entity
    if (mode == EditorMode::Edit)
    {
        m_blackboard.spawnPosition = GetWorldPosition(m_owner);
        m_wasPlayMode = false;
        return;
    }

    if (!isPlayMode)
    {
        m_wasPlayMode = false;
        return;
    }

    // PLAY MODE ENTRY: Cleanly reset state and initialize starting anchor
    if (!m_wasPlayMode)
    {
        m_wasPlayMode = true;
        m_blackboard.spawnPosition = GetWorldPosition(m_owner);
        m_blackboard.wanderDestination = m_blackboard.spawnPosition;
        m_blackboard.target = nullptr;
        m_blackboard.resetTimers();
        m_blackboard.flags.reset();

        if (m_movement)
        {
            m_initialGravity = m_movement->GetConfig().useGravity;
        }

        switch (m_config.archetype)
        {
        case EnemyArchetype::grunt:
            ChangeState(EnemyState::idle);
            // Prime stateTimer to idleDuration so the grunt directly picks a wander target and moves on frame 1
            m_blackboard.stateTimer = m_config.idleDuration;
            break;
        case EnemyArchetype::ground_ambush:
            FindCoverProps();
            ChangeState(EnemyState::ambush_dormant);
            break;
        case EnemyArchetype::wall_lurker:
            ChangeState(EnemyState::wall_hang);
            break;
        default:
            ChangeState(EnemyState::idle);
            break;
        }
    }

    // Halt logic if dead or hit-stopped
    if (m_health && (m_health->IsDead() || m_health->IsHitStopped()))
    {
        if (m_health->IsDead() && m_currentState != EnemyState::dead)
        {
            ChangeState(EnemyState::dead);
        }
        return;
    }

    // AAA Dual-Threshold Tether & Boundary Stand-Down Evaluation
    const DirectX::XMFLOAT3 currentPos{ GetWorldPosition(m_owner) };
    const DirectX::XMVECTOR vCurrent{ DirectX::XMLoadFloat3(&currentPos) };
    const DirectX::XMVECTOR vSpawn{ DirectX::XMLoadFloat3(&m_blackboard.spawnPosition) };
    const float leashRangeSq{ m_config.leashRange * m_config.leashRange };

    float distFromSpawnSq{ 0.0f };
    DirectX::XMStoreFloat(&distFromSpawnSq, DirectX::XMVector3LengthSq(DirectX::XMVectorSubtract(vCurrent, vSpawn)));

    // Inner hysteresis boundary: re-arm aggro only once returned inside (leashRange - leashHysteresis)
    const float innerLeash{ (std::max)(m_config.wanderRadius, m_config.leashRange - m_config.leashHysteresis) };
    const float innerLeashSq{ innerLeash * innerLeash };

    // 1. Boundary Breach: Enemy reaches the outer leash perimeter while chasing
    if (distFromSpawnSq >= leashRangeSq && m_currentState != EnemyState::wall_hang)
    {
        m_blackboard.setFlag(SensoryFlag::is_leashed, true);

        // AAA Behavior: Stop immediately, maintain current facing direction, and enter Idle
        if (m_currentState == EnemyState::chase ||
            m_currentState == EnemyState::attack_windup ||
            m_currentState == EnemyState::attack_active)
        {
            ChangeState(EnemyState::idle);
            StopMovement();
            m_blackboard.deaggroCooldownTimer = m_config.deaggroDuration;
        }
    }
    // 2. Hysteresis Recovery: Clear leashed status once safely inside inner boundary
    else if (distFromSpawnSq <= innerLeashSq)
    {
        m_blackboard.setFlag(SensoryFlag::is_leashed, false);
    }

    // Amortized perception update
    UpdatePerception(dt);

    if (m_blackboard.attackCooldownTimer > 0.0f)
    {
        m_blackboard.attackCooldownTimer -= dt;
    }

    if (m_blackboard.deaggroCooldownTimer > 0.0f)
    {
        m_blackboard.deaggroCooldownTimer -= dt;
    }

    // Dispatch to archetype behaviors
    switch (m_config.archetype)
    {
    case EnemyArchetype::grunt:
        TickGrunt(dt);
        break;
    case EnemyArchetype::ground_ambush:
        TickGroundAmbush(dt);
        break;
    case EnemyArchetype::wall_lurker:
        TickWallLurker(dt);
        break;
    default:
        break;
    }
}

namespace
{
    // Recursively gathers scene GameObjects representing Hide_Building pillar props
    void CollectCoverPropsRecursive(GameObject* current, std::vector<GameObject*>& outProps) noexcept
    {
        if (!current) return;

        const std::string_view name{ current->GetName() };
        if (name.starts_with("Hide_Building"))
        {
            // If it has no children, it is a standalone pillar; if it has children, add them
            if (current->GetChildren().empty())
            {
                outProps.push_back(current);
            }
            else
            {
                for (const auto& child : current->GetChildren())
                {
                    if (child)
                    {
                        outProps.push_back(child.get());
                    }
                }
                return;
            }
        }

        for (const auto& child : current->GetChildren())
        {
            CollectCoverPropsRecursive(child.get(), outProps);
        }
    }
}

void AIControllerComponent::FindCoverProps() noexcept
{
    m_cachedCoverProps.clear();
    m_currentCoverObstacle = nullptr;
    if (!m_owner) return;

    // Traverse to Scene Root to scan all available props in Act_4
    GameObject* root{ m_owner };
    while (root->GetParent() != nullptr)
    {
        root = root->GetParent();
    }

    CollectCoverPropsRecursive(root, m_cachedCoverProps);
}

void AIControllerComponent::UpdateAmbushCover(const float dt) noexcept
{
    if (m_currentState != EnemyState::ambush_dormant || !m_blackboard.target)
    {
        return;
    }

    if (m_cachedCoverProps.empty())
    {
        FindCoverProps();
        if (m_cachedCoverProps.empty())
        {
            return;
        }
    }

    const DirectX::XMFLOAT3 playerPos{ GetWorldPosition(m_blackboard.target) };
    const DirectX::XMFLOAT3 currentPos{ GetWorldPosition(m_owner) };
    const DirectX::XMFLOAT3 spawnPos{ m_blackboard.spawnPosition };

    GameObject* bestObstacle{ m_currentCoverObstacle };
    float bestScore{ 999999.0f };

    // Select candidate pillar within territory using authoritative world positions
    const float maxSearchDistSq{ m_config.leashRange * m_config.leashRange };

    for (auto* prop : m_cachedCoverProps)
    {
        if (!prop || prop->IsDestroyed()) continue;

        const DirectX::XMFLOAT3 propPos{ GetWorldPosition(prop) };
        const float dxSpawn{ propPos.x - spawnPos.x };
        const float dzSpawn{ propPos.z - spawnPos.z };
        const float distToSpawnSq{ (dxSpawn * dxSpawn) + (dzSpawn * dzSpawn) };

        if (distToSpawnSq > maxSearchDistSq) continue;

        const float dxPlayer{ propPos.x - playerPos.x };
        const float dzPlayer{ propPos.z - playerPos.z };
        const float distToPlayerSq{ (dxPlayer * dxPlayer) + (dzPlayer * dzPlayer) };

        if (distToPlayerSq < 0.01f) continue;

        const float invDist{ 1.0f / std::sqrt(distToPlayerSq) };
        const float dirX{ dxPlayer * invDist };
        const float dirZ{ dzPlayer * invDist };

        // Geometric shadow position: directly behind the pillar opposite the player in world space
        const DirectX::XMFLOAT3 hidePos{
            propPos.x + (dirX * m_config.ambushStandoffDistance),
            spawnPos.y,
            propPos.z + (dirZ * m_config.ambushStandoffDistance)
        };

        const float dxEnemy{ hidePos.x - currentPos.x };
        const float dzEnemy{ hidePos.z - currentPos.z };
        float score{ (dxEnemy * dxEnemy) + (dzEnemy * dzEnemy) };

        // Hysteresis bias: favor staying with current pillar to prevent ping-ponging
        if (prop == m_currentCoverObstacle)
        {
            score -= 4.0f;
        }

        if (score < bestScore)
        {
            bestScore = score;
            bestObstacle = prop;
        }
    }

    if (bestObstacle)
    {
        m_currentCoverObstacle = bestObstacle;
        const DirectX::XMFLOAT3 propPos{ GetWorldPosition(bestObstacle) };

        const float dxPlayer{ propPos.x - playerPos.x };
        const float dzPlayer{ propPos.z - playerPos.z };
        const float distToPlayer{ std::sqrt((dxPlayer * dxPlayer) + (dzPlayer * dzPlayer)) };

        if (distToPlayer > 0.01f)
        {
            const float invDist{ 1.0f / distToPlayer };
            const float dirX{ dxPlayer * invDist };
            const float dirZ{ dzPlayer * invDist };

            m_blackboard.wanderDestination = {
                propPos.x + (dirX * m_config.ambushStandoffDistance),
                spawnPos.y,
                propPos.z + (dirZ * m_config.ambushStandoffDistance)
            };
        }
    }
}

namespace
{
    // Precomputed unit-circle lookup table on the XZ plane (Zero runtime trigonometry per frame)
    [[nodiscard]] const std::array<DirectX::XMFLOAT2, 24>& GetPrecomputedUnitCircle() noexcept
    {
        static const auto s_table{ []() noexcept {
            std::array<DirectX::XMFLOAT2, 24> table{};
            constexpr float step{ DirectX::XM_2PI / 24.0f };
            for (std::size_t i{ 0 }; i < 24; ++i)
            {
                const float theta{ step * static_cast<float>(i) };
                table[i] = { std::sin(theta), std::cos(theta) };
            }
            return table;
        }() };
        return s_table;
    }

    // Helper: Draws a horizontal wire circle centered at world coordinate using precomputed LUT
    void DrawWireCircle(
        ShapeRenderer* shapes,
        const DirectX::XMFLOAT3& center,
        const float radius,
        const DirectX::XMFLOAT4& color) noexcept
    {
        if (!shapes || radius <= 0.001f) return;

        const auto& lut{ GetPrecomputedUnitCircle() };
        for (std::size_t i{ 0 }; i < lut.size(); ++i)
        {
            const std::size_t nextIdx{ (i + 1) % lut.size() };
            const DirectX::XMFLOAT3 p0{ center.x + (lut[i].x * radius), center.y, center.z + (lut[i].y * radius) };
            const DirectX::XMFLOAT3 p1{ center.x + (lut[nextIdx].x * radius), center.y, center.z + (lut[nextIdx].y * radius) };
            shapes->DrawLine(p0, p1, color);
        }
    }

    // Helper: Draws a curved wire arc for vision sector visualization
    void DrawWireArc(
        ShapeRenderer* shapes,
        const DirectX::XMFLOAT3& center,
        const float radius,
        const float startAngleRad,
        const float endAngleRad,
        const int subdivisions,
        const DirectX::XMFLOAT4& color) noexcept
    {
        if (!shapes || subdivisions < 2 || radius <= 0.001f) return;

        const float step{ (endAngleRad - startAngleRad) / static_cast<float>(subdivisions) };
        for (int i{ 0 }; i < subdivisions; ++i)
        {
            const float a0{ startAngleRad + (step * static_cast<float>(i)) };
            const float a1{ startAngleRad + (step * static_cast<float>(i + 1)) };

            const DirectX::XMFLOAT3 p0{ center.x + (std::sin(a0) * radius), center.y, center.z + (std::cos(a0) * radius) };
            const DirectX::XMFLOAT3 p1{ center.x + (std::sin(a1) * radius), center.y, center.z + (std::cos(a1) * radius) };
            shapes->DrawLine(p0, p1, color);
        }
    }

    // AAA DOOM / RE Engine style: Maps operational state directly to a distinct debug visual color
    [[nodiscard]] constexpr DirectX::XMFLOAT4 GetStateDebugColor(const EnemyState state) noexcept
    {
        switch (state)
        {
        case EnemyState::idle:
        case EnemyState::patrol:
            return { 0.2f, 0.9f, 0.3f, 0.9f }; // Calm Green
        case EnemyState::chase:
            return { 1.0f, 0.8f, 0.1f, 0.95f }; // Alert Amber
        case EnemyState::attack_windup:
        case EnemyState::attack_active:
            return { 1.0f, 0.15f, 0.15f, 1.0f }; // Danger Red
        case EnemyState::hit_react:
            return { 0.9f, 0.2f, 0.9f, 0.95f }; // Stunned Magenta
        case EnemyState::ambush_dormant:
        case EnemyState::ambush_burst:
            return { 0.9f, 0.45f, 0.1f, 0.85f }; // Camo Orange
        case EnemyState::wall_hang:
        case EnemyState::wall_drop:
            return { 0.2f, 0.8f, 1.0f, 0.9f }; // High-Perch Cyan
        case EnemyState::dead:
        default:
            return { 0.4f, 0.4f, 0.4f, 0.5f }; // Inactive Grey
        }
    }
}

void AIControllerComponent::DrawGizmo(const GizmoContext& ctx) noexcept
{
    // Fast-fail if Navigation Gizmos are toggled off in the editor toolbar
    if (!(ctx.categoryMask & static_cast<std::uint32_t>(GizmoCategory::Navigation)))
    {
        return;
    }

    if (!m_owner || !ctx.shapes)
    {
        return;
    }

    if (EditorManager::Instance().GetEditorMode() == EditorMode::Edit)
    {
        m_blackboard.spawnPosition = GetWorldPosition(m_owner);
    }

    const DirectX::XMFLOAT3 pos{ GetWorldPosition(m_owner) };
    const float halfHeight{ m_capsule ? m_capsule->GetTotalHalfHeight() : 1.0f };
    const float eyeHeight{ halfHeight * 1.5f };
    const DirectX::XMFLOAT3 eyePos{ pos.x, pos.y + eyeHeight, pos.z };

    // State Indicator Overhead Marker (DOOM style)
    // Renders a small floating indicator above the capsule representing real-time cognitive state
    const DirectX::XMFLOAT4 stateColor{ GetStateDebugColor(m_currentState) };
    const DirectX::XMFLOAT3 markerPos{ pos.x, pos.y + (halfHeight * 2.0f) + 0.35f, pos.z };
    ctx.shapes->DrawBox(markerPos, { 0.0f, 0.0f, 0.0f }, { 0.15f, 0.15f, 0.15f }, stateColor);

    // Vision Cone & Peripheral Arc
    constexpr DirectX::XMFLOAT4 fovColor{ 1.0f, 0.9f, 0.2f, 0.85f };
    const float yawRad{ DirectX::XMConvertToRadians(m_owner->transform.rotation.y) };
    const float halfFovRad{ DirectX::XMConvertToRadians(m_config.sightFovDeg * 0.5f) };
    const float leftAngle{ yawRad - halfFovRad };
    const float rightAngle{ yawRad + halfFovRad };

    const DirectX::XMFLOAT3 leftRayEnd{
        eyePos.x + (std::sin(leftAngle) * m_config.sightRange),
        eyePos.y,
        eyePos.z + (std::cos(leftAngle) * m_config.sightRange)
    };
    const DirectX::XMFLOAT3 rightRayEnd{
        eyePos.x + (std::sin(rightAngle) * m_config.sightRange),
        eyePos.y,
        eyePos.z + (std::cos(rightAngle) * m_config.sightRange)
    };

    ctx.shapes->DrawLine(eyePos, leftRayEnd, fovColor);
    ctx.shapes->DrawLine(eyePos, rightRayEnd, fovColor);
    DrawWireArc(ctx.shapes, eyePos, m_config.sightRange, leftAngle, rightAngle, 12, fovColor);

    // Combat Attack Spacing Ring (Physical Melee Trigger Boundary)
    constexpr DirectX::XMFLOAT4 attackRingColor{ 1.0f, 0.2f, 0.2f, 0.9f };
    DrawWireCircle(ctx.shapes, { pos.x, pos.y + 0.05f, pos.z }, m_config.attackRange, attackRingColor);

    // Wander Area & Spawn Anchor
    constexpr DirectX::XMFLOAT4 wanderColor{ 0.2f, 0.6f, 1.0f, 0.5f };
    DrawWireCircle(ctx.shapes, { m_blackboard.spawnPosition.x, m_blackboard.spawnPosition.y + 0.04f, m_blackboard.spawnPosition.z }, m_config.wanderRadius, wanderColor);

    // Spawn Anchor Crosshair
    constexpr float crossSize{ 0.35f };
    ctx.shapes->DrawLine(
        { m_blackboard.spawnPosition.x - crossSize, m_blackboard.spawnPosition.y + 0.04f, m_blackboard.spawnPosition.z },
        { m_blackboard.spawnPosition.x + crossSize, m_blackboard.spawnPosition.y + 0.04f, m_blackboard.spawnPosition.z },
        wanderColor);
    ctx.shapes->DrawLine(
        { m_blackboard.spawnPosition.x, m_blackboard.spawnPosition.y + 0.04f, m_blackboard.spawnPosition.z - crossSize },
        { m_blackboard.spawnPosition.x, m_blackboard.spawnPosition.y + 0.04f, m_blackboard.spawnPosition.z + crossSize },
        wanderColor);

    // Active Patrol Destination Vector
    if (m_currentState == EnemyState::patrol)
    {
        constexpr DirectX::XMFLOAT4 patrolLineColor{ 0.2f, 1.0f, 0.4f, 0.75f };
        ctx.shapes->DrawLine(pos, m_blackboard.wanderDestination, patrolLineColor);
        ctx.shapes->DrawBox(m_blackboard.wanderDestination, { 0.0f, 0.0f, 0.0f }, { 0.2f, 0.2f, 0.2f }, patrolLineColor);
    }

    // Leash Disengage Perimeter (Outer Boundary)
    constexpr DirectX::XMFLOAT4 leashColor{ 0.5f, 0.5f, 0.6f, 0.35f };
    DrawWireCircle(ctx.shapes, { m_blackboard.spawnPosition.x, m_blackboard.spawnPosition.y + 0.02f, m_blackboard.spawnPosition.z }, m_config.leashRange, leashColor);

    // Inner Re-Arm Hysteresis Perimeter
    const float innerLeashRadius{ (std::max)(m_config.wanderRadius, m_config.leashRange - m_config.leashHysteresis) };
    constexpr DirectX::XMFLOAT4 innerLeashColor{ 0.2f, 0.7f, 0.5f, 0.25f };
    DrawWireCircle(ctx.shapes, { m_blackboard.spawnPosition.x, m_blackboard.spawnPosition.y + 0.02f, m_blackboard.spawnPosition.z }, innerLeashRadius, innerLeashColor);

    // Archetype-Specific Trigger Footprints
    if (m_config.archetype == EnemyArchetype::ground_ambush)
    {
        constexpr DirectX::XMFLOAT4 ambushColor{ 1.0f, 0.45f, 0.1f, 0.75f };
        DrawWireCircle(ctx.shapes, { pos.x, pos.y + 0.06f, pos.z }, m_config.ambushTriggerRadius, ambushColor);

        // Visual telemetry for active cover prop and dynamic blind-spot shadow point
        if (m_currentCoverObstacle)
        {
            constexpr DirectX::XMFLOAT4 coverLineColor{ 0.1f, 0.85f, 0.95f, 0.85f };
            const DirectX::XMFLOAT3 propPos{ GetWorldPosition(m_currentCoverObstacle) };

            // Line from pillar to shadow hide point
            ctx.shapes->DrawLine(propPos, m_blackboard.wanderDestination, coverLineColor);
            ctx.shapes->DrawBox(m_blackboard.wanderDestination, { 0.0f, 0.0f, 0.0f }, { 0.25f, 0.25f, 0.25f }, coverLineColor);

            // Pillar base boundary
            DrawWireCircle(ctx.shapes, { propPos.x, propPos.y + 0.05f, propPos.z }, 1.0f, coverLineColor);
        }
    }
    else if (m_config.archetype == EnemyArchetype::wall_lurker)
    {
        constexpr DirectX::XMFLOAT4 dropColor{ 0.85f, 0.25f, 0.95f, 0.85f };
        // Vertical guideline connecting perched wall anchor down to floor level
        ctx.shapes->DrawLine(pos, { pos.x, pos.y - 12.0f, pos.z }, dropColor);

        // Ground drop trigger footprint directly beneath the perch
        DrawWireCircle(ctx.shapes, { pos.x, pos.y - 11.9f, pos.z }, m_config.wallDropTriggerRadius, dropColor);
    }

    // Sensory Line-of-Sight Ray to Acquired Target
    if (m_blackboard.target)
    {
        const bool hasLos{ m_blackboard.getFlag(SensoryFlag::has_line_of_sight) };
        const DirectX::XMFLOAT4 losColor{ hasLos ? DirectX::XMFLOAT4{ 0.1f, 1.0f, 0.2f, 0.9f } : DirectX::XMFLOAT4{ 1.0f, 0.1f, 0.1f, 0.4f } };
        const DirectX::XMFLOAT3 targetPos{ m_blackboard.target->transform.position };
        ctx.shapes->DrawLine(eyePos, { targetPos.x, targetPos.y + 1.2f, targetPos.z }, losColor);
    }
}

void AIControllerComponent::DrawInspector()
{
    ImGui::TextDisabled("AI Perception & State Orchestrator");
    ImGui::Separator();

    static constexpr const char* s_archetypeNames[] = { "Grunt (Melee Chaser)", "Ground Ambush (Prop)", "Wall Lurker (Drop & Chase)" };
    int currentArchetype = static_cast<int>(m_config.archetype);
    if (ImGui::Combo("Archetype", &currentArchetype, s_archetypeNames, 3))
    {
        m_config.archetype = static_cast<EnemyArchetype>(currentArchetype);
        switch (m_config.archetype)
        {
        case EnemyArchetype::grunt: ChangeState(EnemyState::idle); break;
        case EnemyArchetype::ground_ambush: ChangeState(EnemyState::ambush_dormant); break;
        case EnemyArchetype::wall_lurker: ChangeState(EnemyState::wall_hang); break;
        default: break;
        }
    }

    ImGui::Spacing();
    ImGui::TextDisabled("PERCEPTION TUNING");
    ImGui::DragFloat("Sight Range", &m_config.sightRange, 0.5f, 1.0f, 50.0f, "%.1f m");
    ImGui::SliderFloat("Sight FOV", &m_config.sightFovDeg, 10.0f, 360.0f, "%.0f deg");
    ImGui::DragFloat("Attack Range", &m_config.attackRange, 0.1f, 0.5f, 10.0f, "%.2f m");
    ImGui::DragFloat("Leash Range", &m_config.leashRange, 0.5f, 2.0f, 100.0f, "%.1f m");
    ImGui::DragFloat("Leash Hysteresis", &m_config.leashHysteresis, 0.2f, 0.5f, 20.0f, "%.1f m");
    ImGui::DragFloat("Wander Radius", &m_config.wanderRadius, 0.2f, 0.0f, 30.0f, "%.1f m");
    ImGui::DragFloat("Turn Speed", &m_config.turnSpeedDeg, 10.0f, 45.0f, 1440.0f, "%.0f deg/s");
    ImGui::DragFloat("Idle Duration", &m_config.idleDuration, 0.1f, 0.1f, 10.0f, "%.1f s");
    ImGui::DragFloat("De-aggro Duration", &m_config.deaggroDuration, 0.1f, 0.5f, 10.0f, "%.1f s");
    ImGui::DragFloat("Patrol Timeout", &m_config.patrolTimeout, 0.2f, 1.0f, 30.0f, "%.1f s");

    if (m_config.archetype == EnemyArchetype::ground_ambush)
    {
        ImGui::DragFloat("Ambush Radius", &m_config.ambushTriggerRadius, 0.2f, 0.5f, 20.0f, "%.1f m");
        ImGui::DragFloat("Standoff Dist", &m_config.ambushStandoffDistance, 0.1f, 0.5f, 10.0f, "%.1f m");
        if (m_currentCoverObstacle)
        {
            ImGui::TextColored(ImVec4{ 0.2f, 0.9f, 0.9f, 1.0f }, "Cover: %s", m_currentCoverObstacle->GetName().c_str());
        }
    }
    else if (m_config.archetype == EnemyArchetype::wall_lurker)
    {
        ImGui::DragFloat("Wall Drop Radius", &m_config.wallDropTriggerRadius, 0.2f, 0.5f, 20.0f, "%.1f m");
    }

    ImGui::Spacing();
    ImGui::TextDisabled("COMBAT TIMING");
    ImGui::DragFloat("Windup Duration", &m_config.attackWindupDuration, 0.05f, 0.05f, 3.0f, "%.2f s");
    ImGui::DragFloat("Active Duration", &m_config.attackActiveDuration, 0.05f, 0.05f, 3.0f, "%.2f s");
    ImGui::DragFloat("Attack Cooldown", &m_config.attackCooldownDuration, 0.1f, 0.1f, 5.0f, "%.2f s");
    ImGui::DragFloat("Hit React Time", &m_config.hitReactDuration, 0.05f, 0.05f, 2.0f, "%.2f s");

    ImGui::Spacing();
    ImGui::TextDisabled("RUNTIME TELEMETRY");
    ImGui::BeginDisabled();

    static constexpr const char* s_stateNames[] = {
        "Idle", "Patrol", "Ambush Dormant", "Ambush Burst",
        "Wall Hang", "Wall Drop", "Chase", "Attack Windup",
        "Attack Active", "Hit React", "Dead"
    };
    ImGui::Text("Current State: %s", s_stateNames[static_cast<std::size_t>(m_currentState)]);
    ImGui::Text("Dist to Target: %.2f m", std::sqrt(m_blackboard.targetDistanceSq));

    bool losCheck = m_blackboard.getFlag(SensoryFlag::has_line_of_sight);
    bool fovCheck = m_blackboard.getFlag(SensoryFlag::is_target_in_fov);
    ImGui::Checkbox("Has Line of Sight", &losCheck);
    ImGui::Checkbox("Is in FOV", &fovCheck);

    ImGui::EndDisabled();
}

void AIControllerComponent::Serialize(nlohmann::json& outJson) const
{
    outJson["Archetype"] = static_cast<int>(m_config.archetype);
    outJson["SightRange"] = m_config.sightRange;
    outJson["SightFovDeg"] = m_config.sightFovDeg;
    outJson["AttackRange"] = m_config.attackRange;
    outJson["LeashRange"] = m_config.leashRange;
    outJson["LeashHysteresis"] = m_config.leashHysteresis;
    outJson["WanderRadius"] = m_config.wanderRadius;
    outJson["TurnSpeedDeg"] = m_config.turnSpeedDeg;
    outJson["IdleDuration"] = m_config.idleDuration;
    outJson["DeaggroDuration"] = m_config.deaggroDuration;
    outJson["PatrolTimeout"] = m_config.patrolTimeout;
    outJson["AmbushTriggerRadius"] = m_config.ambushTriggerRadius;
    outJson["AmbushStandoffDistance"] = m_config.ambushStandoffDistance;
    outJson["WallDropTriggerRadius"] = m_config.wallDropTriggerRadius;
    outJson["AttackWindupDuration"] = m_config.attackWindupDuration;
    outJson["AttackActiveDuration"] = m_config.attackActiveDuration;
    outJson["AttackCooldownDuration"] = m_config.attackCooldownDuration;
    outJson["HitReactDuration"] = m_config.hitReactDuration;
}

void AIControllerComponent::Deserialize(const nlohmann::json& inJson)
{
    m_config.archetype = static_cast<EnemyArchetype>(inJson.value("Archetype", 0));
    m_config.sightRange = inJson.value("SightRange", 14.0f);
    m_config.sightFovDeg = inJson.value("SightFovDeg", 90.0f);
    m_config.attackRange = inJson.value("AttackRange", 1.8f);
    m_config.leashRange = inJson.value("LeashRange", 22.0f);
    m_config.leashHysteresis = inJson.value("LeashHysteresis", 4.0f);
    m_config.wanderRadius = inJson.value("WanderRadius", 4.0f);
    m_config.turnSpeedDeg = inJson.value("TurnSpeedDeg", 360.0f);
    m_config.idleDuration = inJson.value("IdleDuration", 1.5f);
    m_config.deaggroDuration = inJson.value("DeaggroDuration", 2.5f);
    m_config.patrolTimeout = inJson.value("PatrolTimeout", 6.0f);
    m_config.ambushTriggerRadius = inJson.value("AmbushTriggerRadius", 4.5f);
    m_config.ambushStandoffDistance = inJson.value("AmbushStandoffDistance", 2.2f);
    m_config.wallDropTriggerRadius = inJson.value("WallDropTriggerRadius", 3.5f);
    m_config.attackWindupDuration = inJson.value("AttackWindupDuration", 0.5f);
    m_config.attackActiveDuration = inJson.value("AttackActiveDuration", 0.35f);
    m_config.attackCooldownDuration = inJson.value("AttackCooldownDuration", 1.2f);
    m_config.hitReactDuration = inJson.value("HitReactDuration", 0.45f);

    switch (m_config.archetype)
    {
    case EnemyArchetype::grunt: ChangeState(EnemyState::idle); break;
    case EnemyArchetype::ground_ambush: ChangeState(EnemyState::ambush_dormant); break;
    case EnemyArchetype::wall_lurker: ChangeState(EnemyState::wall_hang); break;
    default: break;
    }
}

REGISTER_COMPONENT(AIControllerComponent)