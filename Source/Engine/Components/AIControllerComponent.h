#pragma once

#include <DirectXMath.h>
#include <json.hpp>
#include "AIBlackboard.h"
#include "IComponent.h"

class CapsuleColliderComponent;
class CharacterMovementComponent;
class HealthComponent;

// Data-driven tuning parameters for perception, distances, and combat timing
struct AIControllerConfig
{
    EnemyArchetype archetype{ EnemyArchetype::grunt };

    float sightRange{ 14.0f };
    float sightFovDeg{ 90.0f };
    float attackRange{ 1.8f };
    float leashRange{ 22.0f };
    float leashHysteresis{ 4.0f };   // Dual-threshold: re-aggro requires target inside (leashRange - leashHysteresis)
    float wanderRadius{ 4.0f };
    float turnSpeedDeg{ 360.0f };    // Angular rate in deg/sec to eliminate instant 1-frame rotation snaps
    float ambushTriggerRadius{ 4.5f };
    float ambushStandoffDistance{ 2.2f }; // Distance behind pillar center to position the enemy
    float wallDropTriggerRadius{ 3.5f };

    float attackWindupDuration{ 0.5f };
    float attackActiveDuration{ 0.35f };
    float attackCooldownDuration{ 1.2f };
    float hitReactDuration{ 0.45f };
    float deaggroDuration{ 2.5f };   // Time enemy remains disinterested after reaching leash limit

    // Perception update rate (amortized time-slicing at ~6.6 Hz)
    float perceptionInterval{ 0.15f };

    // Locomotion & wander timing
    float idleDuration{ 1.5f };     // Time spent resting before picking next wander destination
    float patrolTimeout{ 6.0f };    // Max time allowed to reach wander point before resetting to idle
};

// Symmetrically controls the character motor based on sensory evaluation
class AIControllerComponent final : public IComponent
{
public:
    AIControllerComponent() noexcept = default;
    explicit AIControllerComponent(const AIControllerConfig& config) noexcept;
    ~AIControllerComponent() override;

    AIControllerComponent(const AIControllerComponent&) = delete;
    AIControllerComponent& operator=(const AIControllerComponent&) = delete;
    AIControllerComponent(AIControllerComponent&&) noexcept = delete;
    AIControllerComponent& operator=(AIControllerComponent&&) noexcept = delete;

    void OnAttach(GameObject* owner) noexcept override;
    void OnEnable() noexcept override;
    void OnDisable() noexcept override;
    void Update(float dt) override;
    void DrawGizmo(const GizmoContext& ctx) noexcept override;
    void DrawInspector() override;

    void Serialize(nlohmann::json& outJson) const override;
    void Deserialize(const nlohmann::json& inJson) override;

    [[nodiscard]] const char* GetTypeName() const noexcept override { return "AIControllerComponent"; }

    // Direct interface for external combat interruptions (e.g., player heavy weapon impacts)
    void NotifyHitReaction() noexcept;
    void NotifyLaunch(float launchVelocityY) noexcept;
    void NotifyAirJuggle(float hitStopDuration, bool isFinisher = false, float finisherFollowThrough = 0.55f) noexcept;
    void NotifySpike(float downwardVelocityY) noexcept;

    // State accessors
    [[nodiscard]] EnemyState GetCurrentState() const noexcept { return m_currentState; }
    [[nodiscard]] const AIBlackboard& GetBlackboard() const noexcept { return m_blackboard; }
    [[nodiscard]] AIControllerConfig& GetConfig() noexcept { return m_config; }

private:
    void ChangeState(EnemyState newState) noexcept;
    void AcquirePlayerTarget() noexcept;
    void UpdatePerception(float dt) noexcept;

    // Archetype tick handlers
    void TickGrunt(float dt) noexcept;
    void TickGroundAmbush(float dt) noexcept;
    void TickWallLurker(float dt) noexcept;
    void TickAirborneJuggle(float dt) noexcept;

    // Cover & Ambush positioning helpers
    void FindCoverProps() noexcept;
    void UpdateAmbushCover(float dt) noexcept;

    // Movement helper (supports decoupled combat gaze for strafing)
    void SteerTowards(const DirectX::XMFLOAT3& targetWorldPos, float dt, const DirectX::XMFLOAT3* lookAtWorldPos = nullptr) noexcept;
    void StopMovement() noexcept;

    enum class JugglePhase : std::uint8_t
    {
        None = 0,
        Ascent,     // Ballistic ascent under 1.0x natural gravity alongside player
        FloatHold   // Zero-G lock: floating at eye-level waiting for air combo or finisher recovery
    };

    AIControllerConfig m_config{};
    EnemyState m_currentState{ EnemyState::idle };
    AIBlackboard m_blackboard{};
    JugglePhase m_jugglePhase{ JugglePhase::None };
    float m_juggleHoldTimer{ 0.0f };

    // Sibling component caches
    CapsuleColliderComponent* m_capsule{ nullptr };
    CharacterMovementComponent* m_movement{ nullptr };
    HealthComponent* m_health{ nullptr };

    // Initial gravity backup to guarantee proper restoration after wall-lurker drops
    bool m_initialGravity{ true };
    bool m_wasPlayMode{ false };

    // Cover obstacle caches
    GameObject* m_currentCoverObstacle{ nullptr };
    std::vector<GameObject*> m_cachedCoverProps{};
};