#pragma once

#include "Bullet.h"
#include "Character.h"
#include "EffectManager.h"
#include "EnemyManager.h"
#include <cmath>
#include <deque>
#include <DirectXMath.h>
#include <memory>
#include <string> 
#include <vector>
#include "System/AudioManager.h"
#include "System/Model.h"
#include "System/ShapeRenderer.h"

enum class EnemyType;
enum class AttackType;
enum class MoveDir;

class ShapeRenderer;
class Enemy : public Character
{
public:
    Enemy(ID3D11Device* device, const char* filePath, DirectX::XMFLOAT3 startPos, DirectX::XMFLOAT3 startRot,
        DirectX::XMFLOAT4 startColor, EnemyType type, AttackType attackType,
        float minX = 0.0f, float maxX = 0.0f,
        float minZ = 0.0f, float maxZ = 0.0f, MoveDir dir = (MoveDir)0);
    ~Enemy() override;

    void Update(float elapsedTime, Camera* camera) override;
    void UpdateTracking(float elapsedTime, Camera* camera, const DirectX::XMFLOAT3& playerPos, bool allowAttack = true);
    void UpdateProjectiles(float elapsedTime, Camera* camera);
    void SetActive(bool active) { m_isActive = active; }
    void SetHighlight(bool highlight) { m_isHighlighted = highlight; }
    void UpdateOriginalTransform(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& rot);
    void SetBaseMoveSpeed(float speed) { m_baseMoveSpeed = speed; }
    void SetMoveDir(MoveDir dir) { m_moveDir = dir; }
    void SetPatrolLimitsX(float minOffset, float maxOffset);
    void SetPatrolLimitsZ(float minOffset, float maxOffset);
    void SetPosition(const DirectX::XMFLOAT3& pos);
    void SetRotation(const DirectX::XMFLOAT3& rot);
    void Reinitialize(ID3D11Device* device, const char* filePath, const DirectX::XMFLOAT3& startPos,
        const DirectX::XMFLOAT3& startRot, const DirectX::XMFLOAT4& startColor,
        EnemyType type, AttackType attackType, float minX, float maxX,
        float minZ, float maxZ, MoveDir dir);
    void RenderDebugProjectiles(ShapeRenderer* renderer);
    void RenderProjectiles(ModelRenderer* renderer);

    DirectX::XMFLOAT3 GetPosition() const;
    DirectX::XMFLOAT3 GetRotation() const;
    [[nodiscard]] DirectX::XMFLOAT4 GetRenderColor() const;
    [[nodiscard]] DirectX::XMFLOAT4 GetBaseColor() const { return m_baseColor; }
    [[nodiscard]] DirectX::XMFLOAT4& GetMutableBaseColor() { return m_baseColor; }
    DirectX::XMFLOAT3 GetOriginalPosition() const { return originalPosition; }
    DirectX::XMFLOAT3 GetOriginalRotation() const { return originalRotation; }

    EnemyType GetType() const { return m_type; }
    std::shared_ptr<Model> GetModel() const { return m_model; }

    AttackType GetAttackType() const { return m_attackType; }
    std::deque<std::unique_ptr<Bullet>>& GetProjectiles() { return m_projectiles; }

    [[nodiscard]] bool IsActive() const noexcept override { return m_isActive; }
    bool IsHighlighted() const { return m_isHighlighted; }

    float GetMinX() const { return m_patrolMinX - originalPosition.x; }
    float GetMaxX() const { return m_patrolMaxX - originalPosition.x; }
    float GetMinZ() const { return m_patrolMinZ - originalPosition.z; }
    float GetMaxZ() const { return m_patrolMaxZ - originalPosition.z; }
    MoveDir GetMoveDir() const { return m_moveDir; }

    void SetScale(const DirectX::XMFLOAT3& scale) { m_scale = scale; }
    DirectX::XMFLOAT3 GetScale() const { return m_scale; }

    void TakeDamage(int damage);
    void SetMaxHP(int hp) { m_hp = hp; }
    [[nodiscard]] int GetHP() const { return m_hp; }

    void SetInvincible(bool invincible) { m_isInvincible = invincible; }
    [[nodiscard]] bool IsInvincible() const { return m_isInvincible; }

    void SetKilledPlayer(bool k) { m_killedPlayer = k; }
    bool HasKilledPlayer() const { return m_killedPlayer; }

private:
    void UpdateAttackLogic(float elapsedTime, Camera* camera, const DirectX::XMFLOAT3& playerPos, bool allowAttack);

    DirectX::XMFLOAT3 GetForwardVector() const;
    DirectX::XMFLOAT3 originalPosition;
    DirectX::XMFLOAT3 originalRotation;
    EnemyType m_type;
    AttackType m_attackType;
    std::shared_ptr<Model> m_model;
    std::deque<std::unique_ptr<Bullet>> m_projectiles;

    // ==========================================
    // ATTACK SETTINGS 
    // ==========================================
    float m_attackTimer = 0.0f;
    float m_aggroTimer = 0.0f;
    float m_fireRate = 0.7f;
    float m_projectileSpeed = 7.0f;
    float m_activationDistance = 15.0f;
    float m_despawnDistance = 55.0f;
    float m_patrolMinX = 0.0f;
    float m_patrolMaxX = 0.0f;
    float m_patrolMinZ = 0.0f;
    float m_patrolMaxZ = 0.0f;
    float m_currentSpeed = 0.0f;
    float m_baseMoveSpeed = 0.0f;

    float GetRandomFloat(float min, float max);
    DirectX::XMFLOAT3 m_randomTargetPos;

    static constexpr int MAX_PROJECTILES = 5;
    static constexpr float SPAWN_OFFSET_FWD = 0.6f;
    static constexpr float SPAWN_OFFSET_Y = 0.0f;

    bool m_isActive = false;
    MoveDir m_moveDir;

    bool m_isInvincible = false;

    DirectX::XMFLOAT4 m_baseColor{ 1.0f, 1.0f, 1.0f, 1.0f }; 

    float m_blinkTimer{ 0.0f };
    float m_lifeTime{ 0.0f };
    static constexpr float BLINK_DURATION{ 0.1f };

    DirectX::XMFLOAT4 m_projectileColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT3 m_scale = { 1.0f, 1.0f, 1.0f };
    bool m_isHighlighted = false;

    bool m_killedPlayer = false;

    int m_hp = 30;
};