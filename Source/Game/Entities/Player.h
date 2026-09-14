#pragma once

#include <array>
#include <cmath>
#include <DirectXMath.h>
#include <deque>
#include <memory>
#include <SDL3/SDL.h>
#include "System/AudioManager.h"
#include "System/CollisionManager.h"
#include "System/Input.h"
#include "System/Graphics.h"
#include "AnimationController.h"
#include "Bullet.h"
#include "Camera.h"
#include "CapeSimulator.h"
#include "Character.h"
#include "EffectManager.h"
#include "Framework.h"
#include "Weapon.h"

class AnimationController;
class Camera;
class CollisionManager;

class Player : public Character
{
public:
    enum class WeaponType {
        Crossbow = 0,
        Sword,
        Count
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
    CharacterMovement* GetMovement() const { return movement.get(); }
    AnimationController* GetAnimator() const { return animator.get(); }
    std::shared_ptr<Model> GetModel() const { return model; }

    void SetInputEnabled(bool enable) { isInputEnabled = enable; }
    [[nodiscard]] bool IsInputEnabled() const { return isInputEnabled; }
    void SetCamera(Camera* cam) { activeCamera = cam; }

    void SetPosition(const DirectX::XMFLOAT3& pos) noexcept override;
    void SetRotation(const DirectX::XMFLOAT3& rot) noexcept override { if (movement) movement->SetRotation(rot); }

    void SetActiveWeapon(WeaponType type) { m_activeWeaponType = type; }
    [[nodiscard]] Weapon* GetActiveWeapon() const { return m_weapons[static_cast<size_t>(m_activeWeaponType)].get(); }

    void RenderWeapon(ModelRenderer* renderer);
    void RotateModelToPoint(const DirectX::XMFLOAT3& targetPos);
    [[nodiscard]] const DirectX::XMFLOAT3& GetAimTarget() const { return m_aimTarget; }

    void FireProjectile();
    void RenderProjectiles(ModelRenderer* renderer);
    void ResetPlayerBulletOffsets() {
        m_playerbulletOffsetPos = { 0.0f, 0.0f, 0.0f };
        m_playerbulletOffsetRot = { 0.0f, 0.0f, 0.0f };
        m_playerbulletOffsetScale = { 1.0f, 1.0f, 1.0f };
    }

    float GetShootDelay() const { return ShootDuration; }
    std::deque<std::unique_ptr<Bullet>>& GetProjectiles() { return m_projectiles; }

    [[nodiscard]] bool IsBackpedaling() const { return m_isBackpedaling; }

    DirectX::XMFLOAT4 color = { 1.0f, 1.0f, 1.0f, 1.0f };

    DirectX::XMFLOAT2 GetLastValidInput() const { return lastValidInput; }

    // Health & Invincibility
    void TakeDamage(float damage);
    void SetMaxHP(float maxHp) { m_maxHp = maxHp; m_hp = maxHp; }
    [[nodiscard]] float GetHP() const { return m_hp; }
    [[nodiscard]] float GetMaxHP() const { return m_maxHp; }
    void TriggerInvincibility(float duration) { m_invincibilityTimer = duration; }
    [[nodiscard]] bool IsInvincible() const { return m_invincibilityTimer > 0.0f; }

    void SetLastValidInput(DirectX::XMFLOAT2 dir) { lastValidInput = dir; }
    void SetAimLocked(bool locked) { m_aimLocked = locked; }
    void ForceAimTarget(const DirectX::XMFLOAT3& target) { m_aimTarget = target; }

    void SetCollisionManager(CollisionManager* colMgr) { m_collisionManager = colMgr; }
    CollisionManager* GetCollisionManager() const { return m_collisionManager; }

    [[nodiscard]] float GetDamageGlitchIntensity() const noexcept;

    static constexpr float CapsuleHalfHeight{ 1.0f };
    static constexpr float AimMinDistSq{ 0.0f };
    static constexpr float MaxTorsoAngle{ 1.5707963f };
    static constexpr float RotSmoothSpeed{ 15.0f };
    static constexpr float BulletSpeed{ 50.0f };
    static constexpr float BulletSpawnFwd{ 1.5f };
    static constexpr float BulletSpawnY{ 1.0f };
    static constexpr int   MaxBullets{ 150 };
    static constexpr float ShootDuration{ 0.15f };
    static constexpr float DashCooldown{ 1.0f };

private:
    void HandleAimInput(Camera* camera);
    void UpdateFootRotation(float dt, float& outSmoothedYaw);
    void UpdateAimConstraint(float dt, float& inOutSmoothedYaw, bool& outShouldAim, float& outRelativeAngle);
    void ApplyWorldMatrix(float smoothedYaw, bool shouldAim, float relativeAngle);
    void UpdateProjectiles(float dt, Camera* camera);
    void StopAllVFX();

    std::unique_ptr<AnimationController> animator;
    Camera* activeCamera = nullptr;

    bool isInputEnabled = true;
    bool m_isBackpedaling = false;
    DirectX::XMFLOAT2 lastValidInput = { 0.0f, 1.0f };

    float m_hp = 30.0f;
    float m_maxHp = 30.0f;
    float m_invincibilityTimer = 0.0f;

    std::array<std::unique_ptr<Weapon>, static_cast<size_t>(WeaponType::Count)> m_weapons{};
    WeaponType m_activeWeaponType{ WeaponType::Crossbow };
    int m_rightHandBoneIndex{ -1 };

    DirectX::XMFLOAT3 m_aimTarget = { 0.0f, 0.0f, 0.0f };
    bool m_aimLocked = false;

    std::shared_ptr<Model> m_playerbulletModel{};
    DirectX::XMFLOAT3 m_playerbulletOffsetPos{ 0.000f, 0.460f, -0.950f };
    DirectX::XMFLOAT3 m_playerbulletOffsetRot{ 0.000f, 90.000f, 0.000f };
    DirectX::XMFLOAT3 m_playerbulletOffsetScale{ 0.200f, 0.200f, 0.700f };
    DirectX::XMFLOAT4 m_playerbulletColor{ 4.000f, 4.000f, 4.000f, 1.000f };
    std::deque<std::unique_ptr<Bullet>> m_projectiles;
    int m_bulletDamage = 5;

    CollisionManager* m_collisionManager = nullptr;
    std::unique_ptr<CapeSimulator> m_capeSimulator{};

    DebugAnimState m_debugState{};
    bool m_enableIFrames = false;
    float m_iFrameDuration = 1.0f;

    int m_dashReadyVfxHandle = -1;
    float m_dashReadyOffsetY = 0.0f;
    int m_dashStandbyVfxHandle = -1;

    float m_damageGlitchTimer{ 0.0f };
    static constexpr float DAMAGE_GLITCH_DURATION{ 0.4f };
    static constexpr float DAMAGE_GLITCH_MAX_INTENSITY{ 0.120f };
};