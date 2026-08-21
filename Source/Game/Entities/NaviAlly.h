#pragma once

#include "AnimationController.h"
#include "Character.h"
#include "Bullet.h"
#include <DirectXMath.h>
#include <memory>
#include <deque>

class Player;
class EnemyManager;
class Enemy;
class Camera;
class ModelRenderer;
class AnimationController;

class NaviAlly : public Character
{
public:
    explicit NaviAlly(ID3D11Device* device, Player* targetPlayer, EnemyManager* enemyManager);
    ~NaviAlly() override = default;

    void Reset() noexcept;

    void Update(float elapsedTime, Camera* camera) override;
    void Render(ModelRenderer* renderer);
    void RenderDebug(ShapeRenderer* shapeRenderer);

    void RenderProjectiles(ModelRenderer* renderer);
    std::deque<std::unique_ptr<Bullet>>& GetProjectiles() { return m_projectiles; }

    enum class PotionAttackPattern { Radial, Fan };
    PotionAttackPattern m_currentPattern{ PotionAttackPattern::Radial };
    float m_bossAttackTimer{ 0.0f };

    void FireRadialBurst() noexcept;
    void FireFanBurst(const DirectX::XMFLOAT3& targetPos) noexcept;

    void SpawnBullet(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& fwd, float speed) noexcept;

    void SetPosition(const DirectX::XMFLOAT3& pos);
    void SetPotionedState(bool isPotioned) noexcept;

    void StartAttackDelay(float duration) noexcept { m_attackDelayTimer = duration; }

    void OnHitByPlayerBullet() { /* Add your HP reduction logic here */ }

    void TakeDamage(int damage) noexcept;

    [[nodiscard]] bool IsAlive() const noexcept { return m_hp > 0; }

    [[nodiscard]] DirectX::XMFLOAT3 GetAimPoint() const noexcept {
        return movement->GetPosition();
    }

    [[nodiscard]] bool IsPotioned() const noexcept { return m_isPotioned; }

private:
    void UpdateHoverLogic(float elapsedTime);
    void UpdateShootingLogic(float elapsedTime, Camera* camera);
    void UpdateProjectiles(float elapsedTime, Camera* camera);
    void UpdateAttackDelay(float dt) noexcept;
    void FireAtTarget(const DirectX::XMFLOAT3& targetPos);
    float GetRandomFloat(float min, float max);

    // Safe Observer Pointers
    Player* m_targetPlayer{ nullptr };
    EnemyManager* m_enemyManager{ nullptr };
    Enemy* m_currentTarget{ nullptr };

    // State Variables 
    float m_animTime{ 0.0f };
    float m_fireTimer{ 0.0f };
    float m_reactionTimer{ 0.0f };
    DirectX::XMFLOAT4 m_color{ 1.0f, 1.0f, 1.0f, 1.0f };
    float m_lazyHoverYaw{ 0.0f };

    // True Object Pool for Navi's Bullets
    std::deque<std::unique_ptr<Bullet>> m_projectiles{};

	// Potion State
    bool m_isPotioned{ false }; // Track the state
    float m_pulseTimer{ 0.0f }; // Local timer for the pulse

    float m_attackDelayTimer{ 0.0f };

    DirectX::XMFLOAT3 m_potionAnchorPos{ 0.0f, 0.0f, 0.0f };
    bool m_hasCapturedAnchor{ false };

    DirectX::XMFLOAT3 m_randomTargetOffset{ 0.0f, 0.0f, 0.0f };
    float m_randomMoveTimer{ 0.0f };

    static constexpr int MAX_HP{ 250 };
    int m_hp{ MAX_HP };

    std::unique_ptr<AnimationController> m_animator{ nullptr };

    static constexpr float FLOAT_SPEED{ 2.0f };
    static constexpr float FLOAT_AMP{ 0.25f };
    static constexpr float FOLLOW_SPEED{ 15.0f };
    static constexpr float HOVER_HEIGHT{ 2.0f };

    static constexpr float HOVER_RIGHT_OFFSET{ 1.2f }; 
    static constexpr float HOVER_BACK_OFFSET{ 0.4f };

    static constexpr float LAZY_ROTATION_SPEED{ 4.0f };

    static constexpr float REACTION_DELAY{ 2.0f };
    static constexpr float FIRE_RATE{ 0.5f }; 
    static constexpr float ATTACK_RANGE_SQ{ 900.0f }; 

    static constexpr int   MAX_BULLETS{ 128 };
    static constexpr float BULLET_SPEED{ 25.0f };
    static constexpr float DESPAWN_DIST_SQ{ 2500.0f }; 
    static constexpr float BOSS_ATTACK_COOLDOWN{ 2.5f };
    static constexpr float BOSS_BULLET_SPEED{ 12.0f };

    static constexpr float TETHER_RADIUS{ 2.5f };    
    static constexpr float MOVE_SWITCH_TIME{ 1.2f }; 
    static constexpr float MOVE_SPEED_POTIONED{ 5.0f };
    static constexpr float HITBOX_RADIUS{ 0.8f };
};