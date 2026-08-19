#include "Bullet.h"
#include "EffectManager.h"
#include <cmath>

using namespace DirectX;

Bullet::Bullet()
{
    ID3D11Device* device = Graphics::Instance().GetDevice();
    model = std::make_shared<Model>(device, "Data/Model/Character/PLACEHOLDER_mdl_Ball.glb");

    isActive = false;
    velocity = { 0, 0, 0 };
    movement->SetRotationY(0.0f);
    SyncData();
}

void Bullet::Fire(const DirectX::XMFLOAT3& startPos, const DirectX::XMFLOAT3& direction, float projectileSpeed)
{
    isActive = true;
    m_lifeTime = 0.0f;
    movement->SetPosition(startPos);

    XMVECTOR vDir = XMLoadFloat3(&direction);
    vDir = XMVector3Normalize(vDir);
    XMVECTOR vVel = vDir * projectileSpeed;
    XMStoreFloat3(&velocity, vVel);

    SyncData();
}

void Bullet::Update(float elapsedTime, Camera* camera)
{
    if (!isActive) return;

    m_lifeTime += elapsedTime;
    // =========================================================
    // [BARU] LOGIKA KURVA BEZIER (PARABOLA SEMPURNA)
    // =========================================================
    if (m_isParabolic && m_bossTarget) {
        m_parabolaTime += elapsedTime;
        float t = min(1.0f, m_parabolaTime / m_parabolaDuration);

        // Rumus Quadratic Bezier: (1-t)^2*P0 + 2(1-t)t*P1 + t^2*P2
        float u = 1.0f - t;
        float tt = t * t;
        float uu = u * u;

        DirectX::XMFLOAT3 p0 = m_parabolaStart;
        DirectX::XMFLOAT3 p1 = m_parabolaCtrl;
        DirectX::XMFLOAT3 p2 = m_bossTarget->GetPosition();

        DirectX::XMFLOAT3 newPos;
        newPos.x = uu * p0.x + 2 * u * t * p1.x + tt * p2.x;
        newPos.y = uu * p0.y + 2 * u * t * p1.y + tt * p2.y;
        newPos.z = uu * p0.z + 2 * u * t * p1.z + tt * p2.z;

        // Hitung kecepatan palsu untuk keperluan update sistem / efek partikel
        DirectX::XMFLOAT3 currentPos = movement->GetPosition();
        velocity.x = (newPos.x - currentPos.x) / max(elapsedTime, 0.0001f);
        velocity.y = (newPos.y - currentPos.y) / max(elapsedTime, 0.0001f);
        velocity.z = (newPos.z - currentPos.z) / max(elapsedTime, 0.0001f);

        movement->SetPosition(newPos);
        SyncData();
        return; // LANGSUNG RETURN agar logika lurus di bawah tidak dieksekusi!
    }

    if (m_homingTarget || m_bossTarget)
    {
        DirectX::XMFLOAT3 myPos = movement->GetPosition();

        // Ambil posisi target (Gunakan logika percabangan)
        DirectX::XMFLOAT3 ePos = m_homingTarget ? m_homingTarget->GetPosition() : m_bossTarget->GetPosition();

        // Ambil kecepatan target untuk kalkulasi "Lead Shooting"
        DirectX::XMFLOAT3 eVel = { 0, 0, 0 };
        if (m_homingTarget) {
            eVel = m_homingTarget->GetMovement()->GetVelocity();
        }
        else {
            // Untuk Boss, kita anggap kecepatannya 0 atau statis saat ini
            eVel = { 0, 0, 0 };
        }

        float dx = ePos.x - myPos.x;
        float dz = ePos.z - myPos.z;
        float distance = std::sqrt((dx * dx) + (dz * dz));

        DirectX::XMVECTOR vCurrentVel = DirectX::XMLoadFloat3(&velocity);
        float currentSpeed = DirectX::XMVectorGetX(DirectX::XMVector3Length(vCurrentVel));

        float leadTime = distance / currentSpeed;

        DirectX::XMFLOAT3 predictedPos = {
            ePos.x + (eVel.x * leadTime),
            ePos.y,
            ePos.z + (eVel.z * leadTime)
        };

        DirectX::XMVECTOR vPos = DirectX::XMLoadFloat3(&myPos);
        DirectX::XMVECTOR vTarget = DirectX::XMLoadFloat3(&predictedPos);
        DirectX::XMVECTOR vDirToTarget = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(vTarget, vPos));

        float dynamicTurnSpeed = m_turnSpeed;
        if (distance < 5.0f) {
            dynamicTurnSpeed *= (5.0f / max(distance, 0.1f));
        }

        DirectX::XMVECTOR vDesiredVel = DirectX::XMVectorScale(vDirToTarget, currentSpeed);
        vCurrentVel = DirectX::XMVectorLerp(vCurrentVel, vDesiredVel, dynamicTurnSpeed * elapsedTime);

        vCurrentVel = DirectX::XMVectorScale(DirectX::XMVector3Normalize(vCurrentVel), currentSpeed);
        DirectX::XMStoreFloat3(&velocity, vCurrentVel);
    }

    XMFLOAT3 pos = movement->GetPosition();

    // Apply full 3D velocity
    pos.x += velocity.x * elapsedTime;
    pos.y += velocity.y * elapsedTime;
    pos.z += velocity.z * elapsedTime;

    movement->SetPosition(pos);
    SyncData();

    // =========================================================
        // [BARU] UPDATE POSISI, SKALA & ARAH HADAP VFX
        // =========================================================
    if (m_vfxHandle != -1 && EffectManager::Instance().IsPlaying(m_vfxHandle))
    {
        DirectX::XMFLOAT3 vfxPos = pos;
        float speed = std::sqrt((velocity.x * velocity.x) + (velocity.y * velocity.y) + (velocity.z * velocity.z));

        // 1. Tentukan seberapa jauh efek harus didorong maju.
                // Gunakan variabel m_vfxForwardOffsetMult yang kita buat di langkah sebelumnya.
                // - Untuk Bijuudama: m_vfxForwardOffsetMult = 0.0f (jadi offset = 0, efek pas di tengah!)
                // - Untuk Phalanx: m_vfxForwardOffsetMult = 0.5f (jadi didorong ke ujung pedang)
        float actualOffset = scale.x * m_vfxForwardOffsetMult;

        float pitch = 0.0f;
        float yaw = 0.0f;

        if (speed > 0.1f) {
            // Jika meluncur: Arah dan offset ikuti Velocity
            float horizontalDist = std::sqrt((velocity.x * velocity.x) + (velocity.z * velocity.z));
            yaw = std::atan2(velocity.x, velocity.z);
            pitch = std::atan2(-velocity.y, horizontalDist);

            // [FIX] Kalikan arah maju (velocity/speed) dengan actualOffset!
            vfxPos.x += (velocity.x / speed) * actualOffset;
            vfxPos.y += (velocity.y / speed) * actualOffset;
            vfxPos.z += (velocity.z / speed) * actualOffset;
        }
        else {
            // Jika diam (Charging): Arah dan offset ikuti Rotasi Model 3D
            yaw = DirectX::XMConvertToRadians(movement->GetRotation().y);
            pitch = DirectX::XMConvertToRadians(movement->GetRotation().x);

            // [FIX] Kalikan juga arah maju saat diam dengan actualOffset!
            vfxPos.x += std::sin(yaw) * actualOffset;
            vfxPos.z += std::cos(yaw) * actualOffset;
        }

        // Jangan lupa putar 180 derajat (Pi) agar Phalanx tidak hadap belakang
        yaw += DirectX::XM_PI;

        // Terapkan Posisi & Rotasi
        EffectManager::Instance().SetPosition(m_vfxHandle, vfxPos);
        EffectManager::Instance().SetRotation(m_vfxHandle, { pitch, yaw, 0.0f });

        // Terapkan Skala Dinamis setiap frame
        EffectManager::Instance().SetScale(m_vfxHandle, {
            scale.x * m_vfxScaleMultiplier,
            scale.y * m_vfxScaleMultiplier,
            scale.z * m_vfxScaleMultiplier
            });
    }
    else if (m_vfxHandle != -1)
    {
        m_vfxHandle = -1;
    }
}

void Bullet::ApplyMovement(const DirectX::XMFLOAT3& newPos, const DirectX::XMFLOAT3& newVel)
{
    movement->SetPosition(newPos);
    velocity = newVel;
    SyncData();
}

void Bullet::SetActive(bool active)
{
    isActive = active;

    // Jika peluru dimatikan (menabrak/keluar layar), MATIKAN JUGA EFEKNYA!
    if (!active) {
        StopVFX();
    }
}

void Bullet::StopVFX()
{
    if (m_vfxHandle != -1) {
        EffectManager::Instance().Stop(m_vfxHandle);
        m_vfxHandle = -1;
    }
}

void Bullet::AttachVFX(const char* path, float scale)
{
    StopVFX(); // Amankan memori: Hentikan efek lama (jika ada) sebelum memutar yang baru
    m_vfxScaleMultiplier = scale;
    m_vfxHandle = EffectManager::Instance().Play(path, movement->GetPosition(), scale);
}