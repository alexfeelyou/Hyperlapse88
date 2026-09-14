#pragma once

#include <DirectXMath.h>
#include <memory>

class Camera;
class Model;
class ModelRenderer;

class Bullet final
{
public:
    Bullet() noexcept;
    ~Bullet() = default;

    Bullet(const Bullet&) = delete;
    Bullet& operator=(const Bullet&) = delete;
    Bullet(Bullet&&) noexcept = default;
    Bullet& operator=(Bullet&&) noexcept = default;

    void Fire(const DirectX::XMFLOAT3& startPos, const DirectX::XMFLOAT3& direction, float projectileSpeed) noexcept;
    void Update(float elapsedTime, Camera* camera = nullptr) noexcept;
    void ApplyMovement(const DirectX::XMFLOAT3& newPos, const DirectX::XMFLOAT3& newVel) noexcept;

    // Getters & Setters
    [[nodiscard]] const DirectX::XMFLOAT3& GetPosition() const noexcept { return m_position; }
    void SetPosition(const DirectX::XMFLOAT3& pos) noexcept { m_position = pos; }

    [[nodiscard]] const DirectX::XMFLOAT3& GetVelocity() const noexcept { return m_velocity; }
    void SetVelocity(const DirectX::XMFLOAT3& vel) noexcept { m_velocity = vel; }

    [[nodiscard]] std::shared_ptr<Model> GetModel() const noexcept { return m_model; }

    [[nodiscard]] float GetRadius() const noexcept { return m_radius; }
    void SetRadius(float r) noexcept { m_radius = r; }

    [[nodiscard]] bool IsActive() const noexcept { return m_isActive; }
    void SetActive(bool active) noexcept { m_isActive = active; }

    [[nodiscard]] int GetDamage() const noexcept { return m_damage; }
    void SetDamage(int damage) noexcept { m_damage = damage; }

    [[nodiscard]] float GetLifeTime() const noexcept { return m_lifeTime; }

    DirectX::XMFLOAT3 scale{ 0.01f, 0.01f, 0.01f };

private:
    DirectX::XMFLOAT3 m_position{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_velocity{ 0.0f, 0.0f, 0.0f };
    std::shared_ptr<Model> m_model{ nullptr };

    float m_radius{ 0.25f };
    float m_lifeTime{ 0.0f };
    int   m_damage{ 10 };
    bool  m_isActive{ false };
};