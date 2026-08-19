#pragma once
#include "IPooledAttackPattern.h"
#include <DirectXMath.h>
#include <vector>

// ============================================================
// AttackWave - Phase 1 attack pattern.
//
// Menembakkan peluru dari luar layar (Kiri/Kanan) secara lurus.
// Tembakan akan berselang-seling: Sisi kanan mengisi track ganjil,
// lalu sisi kiri mengisi track genap di gelombang berikutnya.
// ============================================================

struct WaveParams {
    int   trackCount = 10;      // Total jalur/track yang tersedia (misal 10)
    int   bulletsPerWave = 5;   // Jumlah peluru yang ditembakkan per gelombang
    int   waves = 6;            // Total gelombang (Kanan->Kiri->Kanan = 3)
    float waveDelay = 0.8f;     // Jeda antar gelombang tembakan
    float speed = 25.0f;        // Kecepatan peluru
    float trackSpacing = 3.5f;  // Jarak antar track (Sumbu Z)
    float startZ = -15.75f;     // Posisi awal Track 0 (Paling bawah/atas)
    float spawnX = 40.0f;       // Jarak spawn dari tengah layar (di luar layar)
    int   damage = 2;
    float sfxVolume = 1.0f;
    DirectX::XMFLOAT4 color = { 1.0f, 1.0f, 0.0f, 1.0f }; // Warna default (Kuning)
};

class AttackWave : public IPooledAttackPattern {
public:
    explicit AttackWave(const WaveParams& params);
    ~AttackWave() override = default;

    void StartPooled(Boss* boss, std::vector<std::unique_ptr<Bullet>>* pool) override;
    void Update(float dt, Boss* boss) override;
    void Render(ID3D11DeviceContext* context, Camera* camera, Boss* boss) override;
    void Stop(Boss* boss) override;

    bool IsFinished() const override;
    std::vector<Bullet*> GetActiveProjectiles() const override { return {}; }

private:
    void FireWave(bool fromRight);

    WaveParams                            m_params;
    std::vector<std::unique_ptr<Bullet>>* m_pool = nullptr;
    Boss* m_boss = nullptr;

    bool  m_active = false;
    int   m_wavesFired = 0;
    float m_waveTimer = 0.0f;
    bool  m_nextIsRight = true; // Toggle selang-seling
};