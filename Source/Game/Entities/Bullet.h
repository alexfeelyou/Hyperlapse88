#pragma once

#include <cmath>
#include <memory>        
#include <DirectXMath.h>
#include "System/Graphics.h"
#include "System/Model.h"
#include "Character.h"

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
    [[nodiscard]] bool IsActive() const noexcept override { return isActive; }
    void SetActive(bool active);

    void SetDamage(int damage) { m_damage = damage; }
    int GetDamage() const { return m_damage; }

    float GetLifeTime() const { return m_lifeTime; }

private:
    DirectX::XMFLOAT3 velocity = { 0, 0, 0 };
    float radius = 0.25f;
    bool isActive = false;
    Character* m_homingTarget = nullptr;
    float m_turnSpeed = 8.0f;

    int m_damage = 10;
    float m_lifeTime = 0.0f;
};