#pragma once
#include "IBossAttackPattern.h"
#include "Bullet.h"
#include <memory>
#include <string>
#include <vector>

// Configuration parameters for the bouncing window attack.
struct BouncingBulletParams {
    float speed = 35.0f;
    int   maxBounces = 5;
    int   spawnCount = 3;
    float spawnDelay = 0.2f;
    float windowWidth = 230.0f;
    float windowHeight = 230.0f;
    float visualScale = 12.0f;
    float hitboxRadius = 2.0f;
    int   damage = 10;
};

class AttackBouncing : public IBossAttackPattern {
public:
    explicit AttackBouncing(const BouncingBulletParams& params);
    ~AttackBouncing() override = default;

    void Start(Boss* boss) override;
    void Update(float dt, Boss* boss) override;
    void Render(ID3D11DeviceContext* context, Camera* camera, Boss* boss) override;
    void Stop(Boss* boss) override;

    bool IsFinished() const override;
    std::vector<Bullet*> GetActiveProjectiles() const override;

private:
    struct BouncingWindowBullet {
        std::unique_ptr<Bullet> bullet;
        std::string windowName;
        int bounceCount = 0;
        int maxBounces = 8;
    };

    BouncingBulletParams m_params;
    std::vector<BouncingWindowBullet> m_bullets;

    bool  m_isSpawning = false;
    int   m_spawnedCount = 0;
    float m_spawnTimer = 0.0f;

    float m_screenLimitX = 0.0f;
    float m_screenLimitZ = 0.0f;
};