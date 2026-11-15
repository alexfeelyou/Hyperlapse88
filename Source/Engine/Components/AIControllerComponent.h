#pragma once

#include <DirectXMath.h>
#include <json.hpp>
#include "AIBlackboard.h"
#include "AnimBlackboard.h"
#include "IComponent.h"

class AnimationComponent;
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
    DirectX::XMFLOAT3 dropLedgeOffset{ 0.0f, 0.0f, 0.0f }; // World offset from spawn position to the ledge where the enemy leaps off
    float wallDropTriggerRadius{ 3.5f };
    float wallLandDuration{ 0.45f };       // Theatrical impact recovery window before initiating chase
    float wallDropEjectSpeed{ 2.5f };      // Outward lateral impulse away from wall face to prevent capsule friction drag

    float attackWindupDuration{ 0.5f };
    float attackActiveDuration{ 0.35f };
    float attackCooldownDuration{ 1.2f };
    float hitReactDuration{ 0.45f };
    float deaggroDuration{ 2.5f };   // Time enemy remains disinterested after reaching leash limit
    std::uint8_t maxComboHits{ 3 };  // Number of chained strikes in Attack_Primary (Combo_Attack_01 to 03)

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
    void NotifyHitReaction(const DirectX::XMFLOAT3& slashDir = { 0.0f, 0.0f, 0.0f }) noexcept;
    void NotifyParried(const DirectX::XMFLOAT3& parrierPos, float poiseDamage = 35.0f, float hitStopDuration = 0.0f) noexcept;
    void NotifyLaunch(float launchVelocityY) noexcept;
    void NotifyAirJuggle(float hitStopDuration, bool isFinisher = false, float finisherFollowThrough = 0.55f, const DirectX::XMFLOAT3& slashDir = { 0.0f, 0.0f, 0.0f }) noexcept;
    void NotifyAirDrop() noexcept;
    void NotifySpike(float downwardVelocityY) noexcept;
    void NotifyPlungeSlam() noexcept;

    // State accessors
    [[nodiscard]] EnemyState GetCurrentState() const noexcept { return m_currentState; }
    [[nodiscard]] const AIBlackboard& GetBlackboard() const noexcept { return m_blackboard; }
    [[nodiscard]] AIControllerConfig& GetConfig() noexcept { return m_config; }
    [[nodiscard]] bool IsInvincible() const noexcept;
    [[nodiscard]] bool IsGetUp() const noexcept;
    [[nodiscard]] bool HasOpenParryWindow() const noexcept;
    void ConsumeParryOpportunity() noexcept { m_parryOpportunityConsumed = true; }
    [[nodiscard]] bool IsAmbushRunAttackPrimed() const noexcept { return m_isAmbushRunAttackPrimed; }
    [[nodiscard]] bool IsExecutingRunAttack() const noexcept { return m_isExecutingRunAttack; }

    // Animation contract accessors
    [[nodiscard]] AnimationComponent* GetAnimation() const noexcept { return m_animation; }
    [[nodiscard]] Engine::Animation::AnimBlackboard& GetAnimBlackboard() noexcept { return m_animBlackboard; }
    [[nodiscard]] const Engine::Animation::AnimBlackboard& GetAnimBlackboard() const noexcept { return m_animBlackboard; }

private:
    void ChangeState(EnemyState newState) noexcept;
    void AcquirePlayerTarget() noexcept;
    void UpdatePerception(float dt) noexcept;
    void TriggerAttackComboStep(std::uint8_t comboIndex) noexcept;
    void TriggerRunAttack() noexcept;

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
        Ascent,         // Ballistic ascent under 1.0x natural gravity alongside player (Hit_To_Air)
        FloatHold,      // Zero-G lock: floating at eye-level waiting for air combo (Hit_Air_F/B/L/R)
        FallLoop,       // Dropped combo: falling under gravity (Hit_Air_To_Floor_Loop)
        FinisherDrop,   // Finisher/spike: playing Hit_Air_To_Floor_Start, then dropping
        ImpactLand,     // Ground impact: lying on floor (Hit_Air_To_Floor_End)
        PlungeImpact,   // Plunge slam impact: lying on floor (Target)
        GetUp           // Recovering to feet (Get_up)
    };

    AIControllerConfig m_config{};
    EnemyState m_currentState{ EnemyState::idle };
    AIBlackboard m_blackboard{};
    JugglePhase m_jugglePhase{ JugglePhase::None };
    float m_juggleHoldTimer{ 0.0f };
    float m_recoveryTimer{ 0.0f };
    std::uint8_t m_hitDirectionIndex{ 0 }; // 0: Front, 1: Back, 2: Left, 3: Right
    std::uint8_t m_comboIndex{ 0 };        // Active combo strike index (0: Attack_01, 1: Attack_02, 2: Attack_03)
    bool m_parryOpportunityConsumed{ false }; // Consumed once player initiates a parry against this swing's Parry_Window
    bool m_isAmbushRunAttackPrimed{ false };  // Primed in ambush; consumed on attack execution or taking damage
    bool m_isExecutingRunAttack{ false };     // Active state marker during the initial contextual running slash

    // Sibling component caches
    AnimationComponent* m_animation{ nullptr };
    CapsuleColliderComponent* m_capsule{ nullptr };
    CharacterMovementComponent* m_movement{ nullptr };
    HealthComponent* m_health{ nullptr };

    // Shared memory contract for skeletal animation evaluator
    Engine::Animation::AnimBlackboard m_animBlackboard{};

    // Initial gravity backup to guarantee proper restoration after wall-lurker drops
    bool m_initialGravity{ true };
    bool m_wasPlayMode{ false };
    bool m_isAmbushSettled{ false }; // Latched once arrived at cover to prevent shadow jitter

    // Cover obstacle caches
    GameObject* m_currentCoverObstacle{ nullptr };
    std::vector<GameObject*> m_cachedCoverProps{};
};