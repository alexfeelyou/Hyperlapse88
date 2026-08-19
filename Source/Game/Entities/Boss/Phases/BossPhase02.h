#pragma once
#include "INaviPhase.h"
#include <DirectXMath.h>
#include <vector>
#include <memory>
#include "BeyondWindow.h"
#include "Camera.h"
#include "Bullet.h"
#include "Primitive.h"
#include "EffectManager.h"
#include "UIDialogueBox.h"
#include "HUDRenderer.h"
#include "IBossAttackPattern.h"
#include "BossAI.h"

class Sprite;
class Player;

struct WingNode {
    DirectX::XMFLOAT2 localOffset;
    DirectX::XMFLOAT2 targetOffset;
    DirectX::XMFLOAT2 size;
    float flapOffset;
    float spawnDelay = 0.0f;
    float animScale = 0.0f;
    bool isClosing = false;
};

class BossPhase02 : public INaviPhase {
public:
    BossPhase02(Player* player = nullptr);
    ~BossPhase02() override = default;

    void Enter(Boss* boss) override;
    void Update(float dt, Boss* boss) override;
    void Render(ID3D11DeviceContext* context, Camera* currentCamera, Boss* boss) override;
    void Exit(Boss* boss) override;

    void ReplayAnimation();

    void SetWingFlapParams(float speed, float intensity) { m_wingFlapSpeed = speed; m_wingFlapIntensity = intensity; }
    float GetWingFlapSpeed() const { return m_wingFlapSpeed; }
    float GetWingFlapIntensity() const { return m_wingFlapIntensity; }

    void SetWingOffsets(float xOffset, float zOffset) { m_wingXOffset = xOffset; m_wingZOffset = zOffset; }
    float GetWingOffsetX() const { return m_wingXOffset; }
    float GetWingOffsetZ() const { return m_wingZOffset; }

    void SetWingSeed(unsigned int seed) {
        if (m_wingSeed != seed) { m_wingSeed = seed; GenerateButterflyWings(); }
    }
    unsigned int GetWingSeed() const { return m_wingSeed; }

    void SetSpawnParams(float popDur, float spawnDur, float chaos) {
        m_popDuration = popDur; m_spawnDuration = spawnDur; m_spawnChaos = chaos;
    }
    float GetPopDuration() const { return m_popDuration; }
    float GetSpawnDuration() const { return m_spawnDuration; }
    float GetSpawnChaos() const { return m_spawnChaos; }

    void SetScalingParams(float p2u, float scale) { m_pixelToUnit = p2u; m_wingGlobalScale = scale; }
    float GetPixelToUnit() const { return m_pixelToUnit; }
    float GetWingGlobalScale() const { return m_wingGlobalScale; }

    void SetClickThrough(bool clickThrough) { m_isClickThrough = clickThrough; }
    bool IsClickThrough() const { return m_isClickThrough; }
    Camera* GetFXCamera() const { return m_fxCamera.get(); }

    std::vector<Bullet*> GetProjectiles();

    void SetAITarget(Player* p) { m_aiTarget = p; }
    void SetAIEnabled(bool val) { m_aiEnabled = val; }
    bool IsAIEnabled() const { return m_aiEnabled; }

    void TakeDamage(int damage, DirectX::XMFLOAT3 hitPos);
    void SetHP(int hp) { m_bossHP = hp; }
    int  GetHP() const { return m_bossHP; }
    int  GetMaxHP() const { return m_bossMaxHP; }
    bool IsDead() const { return m_bossHP <= 0; }

    void SetOverdriveSpriteScale(float scale) { m_overdriveSpriteScale = scale; }
    float GetOverdriveSpriteScale() const { return m_overdriveSpriteScale; }

    bool IsReadyToChangeScene() const { return m_isDying && m_deathTimer >= 7.0f; }

    void AddAttack(std::unique_ptr<IBossAttackPattern> attack);
    bool HasActiveAttacks() const { return !m_activeAttacks.empty(); }
    Player* GetAITarget() const { return m_aiTarget; }

    // [FIX] 各Attack.h に定義された Params 構造体を返す Getter
    BouncingBulletParams& GetBouncingParams() { return m_bouncingParams; }
    BoomerangParams& GetBoomerangParams() { return m_boomerangParams; }
    BlasterParams& GetBlasterParams() { return m_blasterParams; }
    UndyneSpearParams& GetUndyneParams() { return m_undyneParams; }

    bool IsPlayerCaged() const { return m_isPlayerCaged; }
    DirectX::XMFLOAT3 GetCagePos() const { return m_cagePos; }
    float GetCageSize() const { return m_cageSizeWorld; }
    void DamageCage(int dmg);

    BossAI_Phase02* GetAI() const { return m_ai.get(); }

private:
    void GenerateButterflyWings();
    void TriggerCageFirstHitDialogue(Boss* boss);

private:
    Beyond::Window* m_fxWindow = nullptr;
    std::shared_ptr<Camera> m_fxCamera;
    std::unique_ptr<Sprite> m_wingSprite;

    std::vector<WingNode> m_leftWingData;
    std::vector<WingNode> m_rightWingData;

    float m_screenW = 1920.0f;
    float m_screenH = 1080.0f;

    bool m_isClickThrough = false;

    enum class WingState { Expanding, Idle };
    WingState m_wingState = WingState::Expanding;
    float m_wingStateTimer = 0.0f;
    const float WING_EXPAND_DURATION = 2.0f;

    unsigned int m_wingSeed = 1337;
    float m_wingFlickerTimer = 0.0f;
    float m_nextFlickerTarget = 0.2f;

    float m_popDuration = 0.15f;
    float m_spawnDuration = 1.5f;
    float m_spawnChaos = 0.5f;

    float m_glitchTimer = 0.0f;
    float m_wingXOffset = 4.2f;
    float m_wingZOffset = -0.856f;
    float m_wingFlapSpeed = 0.7f;
    float m_wingFlapIntensity = 0.02f;

    float m_pixelToUnit = 40.0f;
    float m_wingGlobalScale = 2.5f;

    // ==========================================
    // 攻撃パラメータの実体（UIから調整可能）
    // ==========================================
    BouncingBulletParams m_bouncingParams;
    BoomerangParams m_boomerangParams;
    BlasterParams m_blasterParams;
    UndyneSpearParams m_undyneParams;

    Player* m_aiTarget = nullptr;
    bool    m_aiEnabled = false;

    int   m_bossMaxHP = 10000;
    int   m_bossHP = 10000;
    float m_hitFlashTimer = 0.0f;

    Boss* m_bossRef = nullptr;
    bool m_isPlayerCaged = false;
    int m_cageMaxHP = 1000;
    int m_cageHP = 1000;
    DirectX::XMFLOAT3 m_cagePos = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_cageWindowPos = { 0.0f, 0.0f, 0.0f };
    float m_cageShakeTimer = 0.0f;
    float m_cageShakeDuration = 0.18f;
    float m_cageShakeIntensity = 0.22f;
    float m_cageSizeWorld = 7.5f;
    std::string m_cageWindowName = "player_cage_window";

    // HUD Renderer — dirender ke FX window
    std::unique_ptr<HUDRenderer> m_hudRenderer;

    // [BARU] Overdrive Sprite
    std::unique_ptr<Sprite> m_overdriveSprite;
    float m_overdriveSpriteScale = 0.02f;
    float m_overdriveAlpha = 0.0f;
    float m_overdriveFadeSpeed = 2.0f;

    std::unique_ptr<UIDialogueBox> m_dialogueBox;
    Beyond::Window* m_dialogueWindow = nullptr;
    std::shared_ptr<Camera> m_dialogueCamera;
    DirectX::XMFLOAT3      m_dialogueWorldPos = { 0.0f, 0.0f, 0.0f };
    const float            m_dialogueWindowW = 420.0f;
    const float            m_dialogueWindowH = 160.0f;
    const std::string      m_dialogueWindowName = "navi_dialogue";
    bool                   m_isDialogueActive = false;

    bool m_overdriveDialogueTriggered = false;
    bool m_cageFirstHitTriggered = false;

    bool  m_isDying = false;
    float m_deathTimer = 0.0f;
    Effekseer::Handle m_deathVfxHandle = -1;
    bool m_deathCleanupDone = false;
    bool m_deathWindowRaised = false;

    // ==========================================
    // MODULAR AI & ATTACK SYSTEM
    // ==========================================
    std::vector<std::unique_ptr<IBossAttackPattern>> m_activeAttacks;

    std::unique_ptr<BossAI_Phase02> m_ai;

    Beyond::Window* m_blockerWindow = nullptr;
};