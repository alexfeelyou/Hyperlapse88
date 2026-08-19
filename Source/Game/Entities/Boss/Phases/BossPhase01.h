#pragma once
#include "INaviPhase.h"
#include "Bullet.h"
#include "Primitive.h"
#include "UIDialogueBox.h"
#include "IPooledAttackPattern.h"

// AI
#include "BossAI.h"
#include "AttackParamManager.h"
#include <vector>
#include <memory>

class Player;

#define DEBUG_SKIP_INTRO 1

class BossPhase01 : public INaviPhase {
public:
    explicit BossPhase01(Player* target = nullptr);
    ~BossPhase01() override = default;

    // ----- INaviPhase Interface -----
    void Enter(Boss* boss) override;
    void Update(float dt, Boss* boss) override;
    void Render(ID3D11DeviceContext* context, Camera* currentCamera, Boss* boss) override;
    void Exit(Boss* boss) override;

    // ----- Attack Management -----
    void AddPooledAttack(std::unique_ptr<IPooledAttackPattern> attack);
    bool HasActiveAttacks() const override { return !m_activeAttacks.empty(); }

    void TriggerRain(const RainParams& params, RainMode mode, bool isPositiveSide, float sweepDir = 1.0f);
    bool HasRainActive() const { return m_rainAttack != nullptr && !m_rainAttack->IsFinished(); }

    void OnBijuudamaParried(DirectX::XMFLOAT3 parryPos, Boss* boss);

    // ----- Boss HP -----
    void TakeDamage(int damage, DirectX::XMFLOAT3 hitPos);
    int  GetHP()    const { return m_bossHP; }
    int  GetMaxHP() const { return m_bossMaxHP; }
    bool IsDead()   const { return m_bossHP <= 0; }
    void SetHP(int hp) { m_bossHP = hp; }

    // ----- AI / player -----
    void    SetAITarget(Player* p) { m_aiTarget = p; }
    void    SetAIEnabled(bool val) { m_aiEnabled = val; }
    bool    IsAIEnabled()     const { return m_aiEnabled; }
    Player* GetAITarget()     const { return m_aiTarget; }

    // ----- Accessors -----
    AttackUltimate* GetActiveUltimate() const {
        for (auto& attack : m_activeAttacks) {
            if (auto* ult = dynamic_cast<AttackUltimate*>(attack.get())) {
                return ult;
            }
        }
        return nullptr;
    }

    BossAI_Phase01* GetAI() const { return m_ai.get(); }
    const std::vector<std::unique_ptr<IPooledAttackPattern>>& GetActiveAttacks() const { return m_activeAttacks; }
    std::vector<std::unique_ptr<Bullet>>& GetProjectiles() { return m_bulletPool; }

private:
    void UpdateBossMovement(float dt, Boss* boss);
    void UpdateIdleHover(float dt, Boss* boss);
    void UpdateBulletPool(float dt, Boss* boss);
    void UpdateGlitchVFX(float dt, Boss* boss);
    void UpdateDeathSequence(float dt, Boss* boss);

private:
    Boss* m_bossRef = nullptr;

    // ---- Bullet pool ----
    std::vector<std::unique_ptr<Bullet>> m_bulletPool;

    // ---- Active attacks ----
    std::vector<std::unique_ptr<IPooledAttackPattern>> m_activeAttacks;
    std::unique_ptr<AttackRain> m_rainAttack;

    // ---- AI Director ----
    std::unique_ptr<BossAI_Phase01> m_ai;
    bool    m_aiEnabled = false;
    Player* m_aiTarget = nullptr;

    // ---- Boss movement state ----
    DirectX::XMFLOAT3 m_targetPosition = { 0.0f, 0.0f, 0.0f };
    float             m_moveLerpSpeed = 3.5f;
    float             m_currentMoveLerpSpeed = 0.0f;
    float             m_moveAcceleration = 8.0f;
    float             m_idleHoverTimer = 0.0f;

    // ---- Boss HP & hit flash ----
    int   m_bossMaxHP = 2000;
    int   m_bossHP = 2000;
    float m_hitFlashTimer = 0.0f;

    // ---- Glitch VFX ----
    int   m_bossGlitchVfxHandle = -1;
    float m_bossGlitchVfxTimer = 2.0f;

    // ---- Death sequence ----
    bool  m_isDying = false;
    float m_deathTimer = 0.0f;
    int   m_deathVfxHandle = -1;

    // ---- Opening dialogue ----
    bool m_isOpeningEvent = true;
    bool m_hasSpawnedWindow = false;
    std::unique_ptr<UIDialogueBox> m_dialogueBox;

    // ---- Visual ----
    std::unique_ptr<Primitive> m_zonePrimitive;
};