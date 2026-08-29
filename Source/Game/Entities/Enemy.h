#pragma once

#include <cmath>
#include <deque>
#include <DirectXMath.h>
#include <memory>
#include <string> 
#include <vector>
#include "System/AudioManager.h"
#include "System/Model.h"
#include "System/ShapeRenderer.h"
#include "Bullet.h"
#include "Character.h"
#include "EffectManager.h"
#include "EnemyManager.h"

enum class EnemyType;
enum class AttackType;
enum class MoveDir;

class ShapeRenderer;

class Enemy final : public Character
{
public:
    Enemy(ID3D11Device* device, const char* filePath, DirectX::XMFLOAT3 startPos, DirectX::XMFLOAT3 startRot,
        DirectX::XMFLOAT4 startColor, EnemyType type, AttackType attackType,
        float minX = 0.0f, float maxX = 0.0f,
        float minZ = 0.0f, float maxZ = 0.0f, MoveDir dir = MoveDir::None);
    ~Enemy() override;

    void Update(float elapsedTime, Camera* camera) override;
    void UpdateTracking(float elapsedTime, Camera* camera, const DirectX::XMFLOAT3& playerPos, bool allowAttack = true);
    void UpdateProjectiles(float elapsedTime, Camera* camera);

    void SetActive(bool active) noexcept { m_isActive = active; }
    void SetHighlight(bool highlight) noexcept { m_isHighlighted = highlight; }
    void UpdateOriginalTransform(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& rot) noexcept;

    void SetBaseMoveSpeed(float speed) noexcept { m_baseMoveSpeed = speed; }
    void SetMoveDir(MoveDir dir) noexcept { m_moveDir = dir; }
    void SetPatrolLimitsX(float minOffset, float maxOffset) noexcept;
    void SetPatrolLimitsZ(float minOffset, float maxOffset) noexcept;

    void SetPosition(const DirectX::XMFLOAT3& pos) noexcept;
    void SetRotation(const DirectX::XMFLOAT3& rot) noexcept;

    void Reinitialize(ID3D11Device* device, const char* filePath, const DirectX::XMFLOAT3& startPos,
        const DirectX::XMFLOAT3& startRot, const DirectX::XMFLOAT4& startColor,
        EnemyType type, AttackType attackType, float minX, float maxX,
        float minZ, float maxZ, MoveDir dir);

    void RenderDebugProjectiles(ShapeRenderer* renderer);
    void RenderProjectiles(ModelRenderer* renderer);

    [[nodiscard]] DirectX::XMFLOAT3 GetPosition() const noexcept;
    [[nodiscard]] DirectX::XMFLOAT3 GetRotation() const noexcept;
    [[nodiscard]] DirectX::XMFLOAT4 GetRenderColor() const noexcept;
    [[nodiscard]] DirectX::XMFLOAT4 GetBaseColor() const noexcept { return m_baseColor; }
    [[nodiscard]] DirectX::XMFLOAT4& GetMutableBaseColor() noexcept { return m_baseColor; }
    [[nodiscard]] DirectX::XMFLOAT3 GetOriginalPosition() const noexcept { return m_originalPosition; }
    [[nodiscard]] DirectX::XMFLOAT3 GetOriginalRotation() const noexcept { return m_originalRotation; }

    [[nodiscard]] EnemyType GetType() const noexcept { return m_type; }
    [[nodiscard]] std::shared_ptr<Model> GetModel() const noexcept { return m_model; }
    [[nodiscard]] AttackType GetAttackType() const noexcept { return m_attackType; }
    [[nodiscard]] std::deque<std::unique_ptr<Bullet>>& GetProjectiles() noexcept { return m_projectiles; }

    [[nodiscard]] bool IsActive() const noexcept override { return m_isActive; }
    [[nodiscard]] bool IsHighlighted() const noexcept { return m_isHighlighted; }

    [[nodiscard]] float GetMinX() const noexcept { return m_patrolMinX - m_originalPosition.x; }
    [[nodiscard]] float GetMaxX() const noexcept { return m_patrolMaxX - m_originalPosition.x; }
    [[nodiscard]] float GetMinZ() const noexcept { return m_patrolMinZ - m_originalPosition.z; }
    [[nodiscard]] float GetMaxZ() const noexcept { return m_patrolMaxZ - m_originalPosition.z; }
    [[nodiscard]] MoveDir GetMoveDir() const noexcept { return m_moveDir; }

    // Map directly to the inherited Character::scale variable
    void SetScale(const DirectX::XMFLOAT3& scl) noexcept { scale = scl; }
    [[nodiscard]] DirectX::XMFLOAT3 GetScale() const noexcept { return scale; }

    void TakeDamage(int damage);
    void SetMaxHP(int hp) noexcept { m_hp = hp; }
    [[nodiscard]] int GetHP() const noexcept { return m_hp; }

    void SetInvincible(bool invincible) noexcept { m_isInvincible = invincible; }
    [[nodiscard]] bool IsInvincible() const noexcept { return m_isInvincible; }

    void SetKilledPlayer(bool k) noexcept { m_killedPlayer = k; }
    [[nodiscard]] bool HasKilledPlayer() const noexcept { return m_killedPlayer; }

private:
    void UpdateAttackLogic(float elapsedTime, Camera* camera, const DirectX::XMFLOAT3& playerPos, bool allowAttack);
    [[nodiscard]] DirectX::XMFLOAT3 GetForwardVector() const noexcept;
    [[nodiscard]] static float GetRandomFloat(float min, float max) noexcept;

    DirectX::XMFLOAT3 m_originalPosition{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_originalRotation{ 0.0f, 0.0f, 0.0f };
    EnemyType m_type{ EnemyType::MushroomNone };
    AttackType m_attackType{ AttackType::None };
    std::shared_ptr<Model> m_model{};
    std::deque<std::unique_ptr<Bullet>> m_projectiles{};

    // ==========================================
    // ATTACK SETTINGS 
    // ==========================================
    float m_attackTimer{ 0.0f };
    float m_aggroTimer{ 0.0f };
    float m_fireRate{ 0.7f };
    float m_projectileSpeed{ 7.0f };
    float m_activationDistance{ 15.0f };
    float m_despawnDistance{ 55.0f };
    float m_patrolMinX{ 0.0f };
    float m_patrolMaxX{ 0.0f };
    float m_patrolMinZ{ 0.0f };
    float m_patrolMaxZ{ 0.0f };
    float m_currentSpeed{ 0.0f };
    float m_baseMoveSpeed{ 0.0f };

    DirectX::XMFLOAT3 m_randomTargetPos{ 0.0f, 0.0f, 0.0f };

    static constexpr int MAX_PROJECTILES{ 5 };
    static constexpr float SPAWN_OFFSET_FWD{ 0.6f };
    static constexpr float SPAWN_OFFSET_Y{ 0.0f };

    bool m_isActive{ false };
    MoveDir m_moveDir{ MoveDir::None };
    bool m_isInvincible{ false };

    DirectX::XMFLOAT4 m_baseColor{ 1.0f, 1.0f, 1.0f, 1.0f };

    float m_blinkTimer{ 0.0f };
    float m_lifeTime{ 0.0f };
    static constexpr float BLINK_DURATION{ 0.1f };

    DirectX::XMFLOAT4 m_projectileColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    bool m_isHighlighted{ false };
    bool m_killedPlayer{ false };
    int m_hp{ 30 };
};