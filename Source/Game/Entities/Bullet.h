#pragma once

#include <cmath>
#include <memory>        
#include <DirectXMath.h>
#include "System/Graphics.h"
#include "System/Model.h"
#include "Character.h"
#include "Boss.h"

class Boss;

class Bullet : public Character
{
public:
    Bullet();
    ~Bullet() override = default;

    void Update(float elapsedTime, Camera* camera) override;

    // Fires the bullet in a specific direction
    void Fire(const DirectX::XMFLOAT3& startPos, const DirectX::XMFLOAT3& direction, float speed);

    // Used when the bullet bounces off a wall
    void ApplyMovement(const DirectX::XMFLOAT3& newPos, const DirectX::XMFLOAT3& newVel);

    // Getters & Setters
    CharacterMovement* GetMovement() const { return movement.get(); }
    DirectX::XMFLOAT3 GetVelocity() const { return velocity; }
    std::shared_ptr<Model> GetModel() const { return model; }
    void SetHomingTarget(Character* target) { m_homingTarget = target; }
    Character* GetHomingTarget() const { return m_homingTarget; }
    float GetRadius() const { return radius; }
	void SetRadius(float r) { radius = r; }
	void SetTurnSpeed(float speed) { m_turnSpeed = speed; }
    [[nodiscard]] bool IsActive() const { return isActive; }
    void SetActive(bool active);

    void AttachVFX(const char* path, float scale);
    void StopVFX();

    void SetBossTarget(Boss* target) {
        m_bossTarget = target;
        m_homingTarget = nullptr; // Reset target biasa jika ada
    }
    Boss* GetBossTarget() const { return m_bossTarget; }

    // [BARU] Fungsi setup untuk Kurva Parabola
    void SetParabolic(bool val) { m_isParabolic = val; }
    void SetParabolaParams(const DirectX::XMFLOAT3& startPos, const DirectX::XMFLOAT3& ctrl, float duration) {
        m_parabolaStart = startPos;
        m_parabolaCtrl = ctrl;
        m_parabolaDuration = duration;
        m_parabolaTime = 0.0f;
    }

    void SetParryReturn(bool val) { m_isParryReturn = val; }
    bool IsParryReturn() const { return m_isParryReturn; }

    void SetDamage(int damage) { m_damage = damage; }
    int GetDamage() const { return m_damage; }



    float GetLifeTime() const { return m_lifeTime; }

private:
    Boss* m_bossTarget = nullptr;

    bool m_isParabolic = false;
    DirectX::XMFLOAT3 m_parabolaStart = { 0,0,0 };
    DirectX::XMFLOAT3 m_parabolaCtrl = { 0,0,0 };
    float m_parabolaTime = 0.0f;
    float m_parabolaDuration = 1.0f; 

    bool m_isParryReturn = false;

private:
    DirectX::XMFLOAT3 velocity = { 0, 0, 0 };
    float radius = 0.25f;
    bool isActive = false;
    Character* m_homingTarget = nullptr;
    float m_turnSpeed = 8.0f;

    int m_damage = 10;
    int m_vfxHandle = -1;
    float m_vfxScaleMultiplier = 1.0f; // <--- [BARU] Tambahkan ini
    float m_vfxForwardOffsetMult = 0.0f;
    float m_lifeTime = 0.0f;

};