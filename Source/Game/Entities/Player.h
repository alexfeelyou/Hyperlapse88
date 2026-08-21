#pragma once

#include "Bullet.h"
#include "CapeSimulator.h"
#include "Character.h"
#include "PlayerConstants.h"
#include "Weapon.h"
#include "System/CollisionManager.h"
#include "System/Input.h"
#include "System/Graphics.h"
#include "AnimationController.h"
#include "Camera.h"
#include "Framework.h"
#include "NaviAlly.h"
#include "PlayerConstants.h"
#include "PlayerStates.h"
#include "StateMachine.h"
#include <cmath>
#include <imgui.h>
#include "EffectManager.h"
#include "System/AudioManager.h"
#include <array>
#include <deque>
#include <memory>
#include <DirectXMath.h>
#include <SDL3/SDL.h>
#include <characterkinematic/PxController.h> 
#include <characterkinematic/PxCapsuleController.h>
#include <characterkinematic/PxControllerManager.h>

class StateMachine;
class AnimationController;
class Camera;
class CollisionManager;

struct PlayerConfig
{
    float moveSpeed{ PlayerConst::MoveSpeed };
    float dashSpeed{ PlayerConst::DashSpeed };
    float dashDuration{ PlayerConst::DashDuration };
    float dashCooldown{ PlayerConst::DashCooldown };
    float acceleration{ PlayerConst::Acceleration };
    float deceleration{ PlayerConst::Deceleration };
    bool  gravityEnabled{ true };
    bool  invertControls{ false };
};

class Player : public Character
{
public:
    enum class WeaponType {
        Crossbow = 0,
        Sword,
        Count // Automatically tracks the number of weapons
    };

    struct DebugAnimState {
        bool forceAnimation{ false };
        std::string animationName{ "" };
        bool disableAimConstraint{ false };
    };
    [[nodiscard]] DebugAnimState& GetDebugState() { return m_debugState; }

    Player();
    ~Player() override;

    void Update(float elapsedTime, Camera* camera) override;

    // Component accessors 
    StateMachine* GetStateMachine() const { return stateMachine.get(); }
    CharacterMovement* GetMovement()     const { return movement.get(); }
    AnimationController* GetAnimator()     const { return animator.get(); }
    std::shared_ptr<Model> GetModel()        const { return model; }

    // Input & camera 
    void SetInputEnabled(bool enable) { isInputEnabled = enable; }
    [[nodiscard]] bool IsInputEnabled() const { return isInputEnabled; } 
    void SetCamera(Camera* cam) { activeCamera = cam; }

    // Position helpers 
    void SetPosition(float x, float y, float z);
    void SetPosition(const DirectX::XMFLOAT3& pos);

    // Movement config
    void ApplyConfig(const PlayerConfig& config) noexcept;

    // Physics init (call once after scene PhysX setup) 
    void InitPhysics(physx::PxControllerManager* manager, physx::PxMaterial* material,
        float spawnY = 15.0f);

	// Weapon 
    void SetActiveWeapon(WeaponType type) { m_activeWeaponType = type; }
    [[nodiscard]] WeaponType GetActiveWeaponType() const { return m_activeWeaponType; }

    // Returns a specific weapon (used by GUI)
    [[nodiscard]] Weapon* GetWeapon(WeaponType type) const { return m_weapons[static_cast<size_t>(type)].get(); }

    // Returns the weapon currently being held (used by Render)
    [[nodiscard]] Weapon* GetActiveWeapon() const { return m_weapons[static_cast<size_t>(m_activeWeaponType)].get(); }

    float GetRadius() const { return 2.0f; } 

    void RenderWeapon(ModelRenderer* renderer);

    // Aim 
    void RotateModelToPoint(const DirectX::XMFLOAT3& targetPos);
    [[nodiscard]] const DirectX::XMFLOAT3& GetAimTarget() const { return m_aimTarget; }

    // Projectiles
    void FireProjectile();
    void RenderProjectiles(ModelRenderer* renderer);
    void ResetPlayerBulletOffsets() {
        m_playerbulletOffsetPos = { 0.0f, 0.0f, 0.0f };
        m_playerbulletOffsetRot = { 0.0f, 0.0f, 0.0f };
        m_playerbulletOffsetScale = { 1.0f, 1.0f, 1.0f };
    }

    void SetShootDelay(float newDelay) {
        m_shootDelay = newDelay;
    }
    void RestoreShootDelay() {
        m_shootDelay = PlayerConst::ShootDuration; 
    }
    float GetShootDelay() const {
        return m_shootDelay;
    }

    std::shared_ptr<Model> GetPlayerBulletModel() const { return m_playerbulletModel; }
    DirectX::XMFLOAT3* GetPlayerBulletOffsetPos() { return &m_playerbulletOffsetPos; }
    DirectX::XMFLOAT3* GetPlayerBulletOffsetRot() { return &m_playerbulletOffsetRot; }
    DirectX::XMFLOAT3* GetPlayerBulletOffsetScale() { return &m_playerbulletOffsetScale; }
    DirectX::XMFLOAT4* GetPlayerBulletColor() { return &m_playerbulletColor; }
    std::deque<std::unique_ptr<Bullet>>& GetProjectiles() { return m_projectiles; }

    // Debug
    void DrawDebugGUI();

    bool IsMoving() const
    {
        return (std::abs(currentSmoothInput.x) > 0.01f ||
            std::abs(currentSmoothInput.y) > 0.01f);
    }
    [[nodiscard]] bool IsBackpedaling() const { return m_isBackpedaling; }

    // Visual tint (used by states for hit flash, etc)
    DirectX::XMFLOAT4 color = { 1.0f, 1.0f, 1.0f, 1.0f };

    // Read accessors for state machine 
    float GetBaseSpeed()    const { return baseSpeed; }
    float GetDashSpeed()    const { return dashSpeed; }
    float GetDashDuration() const { return dashDuration; }
    float GetDashCooldown() const { return dashCooldown; } 
    DirectX::XMFLOAT2 GetLastValidInput() const { return lastValidInput; }

    bool  canDash = true;
    float dashCooldownTimer = 0.0f;

	// Health 
    void TakeDamage(float damage);
    void SetMaxHP(float maxHp) { m_maxHp = maxHp; m_hp = maxHp; } 
    void Heal(float amount);
    void Heal(int amount);
    [[nodiscard]] float GetHP() const { return m_hp; }      
    [[nodiscard]] float GetMaxHP() const { return m_maxHp; }

	// Invincibility (used by PlayerDash and PlayerHit states) 
    void TriggerInvincibility(float duration) { m_invincibilityTimer = duration; }
    [[nodiscard]] bool IsInvincible() const { return m_invincibilityTimer > 0.0f; }

    void SetLastValidInput(DirectX::XMFLOAT2 dir) { lastValidInput = dir; }
    void SetAimLocked(bool locked) { m_aimLocked = locked; }
    void ForceAimTarget(const DirectX::XMFLOAT3& target) { m_aimTarget = target; }

    void SetCollisionManager(CollisionManager* colMgr) { m_collisionManager = colMgr; }
    CollisionManager* GetCollisionManager() const { return m_collisionManager; }

	// Cape Simulator (optional, only used if player model has a cape) 
    CapeSimulator* GetCapeSimulator() const { return m_capeSimulator.get(); }

	// Glitch Effect 
    [[nodiscard]] float GetDamageGlitchIntensity() const noexcept;

private:
    // Update pipeline 
    void UpdateDashCooldown(float dt);
    void HandleMovementInput(float dt);
    void HandleAimInput(Camera* camera);
    void UpdateHorizontalMovement(float dt);
    void UpdateFootRotation(float dt, float& outSmoothedYaw);
    void UpdateAimConstraint(float dt, float& inOutSmoothedYaw, bool& outShouldAim, float& outRelativeAngle);
    void UpdateAimConstraint(float& inOutSmoothedYaw, bool& outShouldAim, float& outRelativeAngle);
    void ApplyWorldMatrix(float smoothedYaw, bool shouldAim, float relativeAngle);
    void UpdateProjectiles(float dt, Camera* camera);

    // Owned components 
    std::unique_ptr<StateMachine>        stateMachine;
    std::unique_ptr<AnimationController> animator;

    // PhysX controller (lifecycle managed by PhysX, released manually in destructor) 
    physx::PxController* m_physxController = nullptr;

    // Camera 
    Camera* activeCamera = nullptr;

    // Input state 
    bool isInputEnabled = true;
    bool invertControls = false;
    bool m_isBackpedaling = false;
    bool gravityEnabled = true;   
    DirectX::XMFLOAT2 currentSmoothInput = { 0.0f, 0.0f };
    DirectX::XMFLOAT2 lastValidInput = { 0.0f, 1.0f };

    // Movement params 
    float moveSpeed = PlayerConst::MoveSpeed;
    float acceleration = PlayerConst::Acceleration;
    float deceleration = PlayerConst::Deceleration;

    // Dash params 
    float baseSpeed = 10.0f;
    float dashSpeed = PlayerConst::DashSpeed;
    float dashDuration = PlayerConst::DashDuration;
    float dashCooldown = PlayerConst::DashCooldown;

    // Health 
    float m_hp = 30.0f;      
    float m_maxHp = 30.0f;   

	// Invincibility timer (counts down when active, prevents damage) 
    float m_invincibilityTimer = 0.0f;

	// Weapon 
    std::array<std::unique_ptr<Weapon>, static_cast<size_t>(WeaponType::Count)> m_weapons{};
    WeaponType m_activeWeaponType{ WeaponType::Crossbow };
    int m_rightHandBoneIndex{ -1 }; // -1 indicates "Not Found Yet"

    // Aim target (set by RotateModelToPoint) 
    DirectX::XMFLOAT3 m_aimTarget = { 0.0f, 0.0f, 0.0f };

    bool m_aimLocked = false;

    // Projectile pool 
    std::shared_ptr<Model> m_playerbulletModel{};
    DirectX::XMFLOAT3 m_playerbulletOffsetPos{ 0.000f, 0.460f, -0.950f };
    DirectX::XMFLOAT3 m_playerbulletOffsetRot{ 0.000f, 0.000f, 0.000f };
    DirectX::XMFLOAT3 m_playerbulletOffsetScale{ 20.000f, 20.000f, 70.000f };
    DirectX::XMFLOAT4 m_playerbulletColor{ 4.000f, 4.000f, 4.000f, 1.000f };
    std::deque<std::unique_ptr<Bullet>> m_projectiles;
    float m_bulletSpeed = PlayerConst::BulletSpeed;

    float m_shootDelay = PlayerConst::ShootDuration;

    int m_bulletDamage = 5;

    CollisionManager* m_collisionManager = nullptr;

	// Cape Simulator (optional) 
    std::unique_ptr<CapeSimulator> m_capeSimulator{};

	// Debug Animation 
    DebugAnimState m_debugState{};

    bool m_enableIFrames = false;
    float m_iFrameDuration = 1.0f;

    int m_dashReadyVfxHandle = -1;
    float m_dashReadyOffsetY = 0.0f;

    int m_dashStandbyVfxHandle = -1;

	// Stop effect
    void StopAllVFX();


	// Glitch Effect 
    float m_damageGlitchTimer{ 0.0f };
    static constexpr float DAMAGE_GLITCH_DURATION{ 0.4f };
    static constexpr float DAMAGE_GLITCH_MAX_INTENSITY{ 0.120f };
};