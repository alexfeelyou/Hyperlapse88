#pragma once
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>

// Konfigurasi Shake (Parameter Object Pattern)
struct ShakeSettings {
    float Duration = 0.5f;          // Durasi total (detik)
    float AmplitudePos = 0.5f;      // Kekuatan geser (Meter)
    float AmplitudeRot = 2.0f;      // Kekuatan putar (Derajat)
    float Frequency = 25.0f;        // Kecepatan getar (Hz)
    float TraumaFalloff = 1.5f;     // Kecepatan reda (Makin tinggi = makin cepat berhenti)
};

class JuiceEngine
{
public:
    // Singleton Access
    static JuiceEngine& Instance();

    // Panggil ini di Scene::Update atau Game::Update
    void Update(float deltaTime);

    // --- TRIGGER METHODS (Panggil dari Player/Gameplay) ---

    // Memicu getaran kamera (Trauma-based)
    void TriggerShake(const ShakeSettings& settings, float intensityMultiplier = 1.0f);

    // Memicu efek glitch pada layar (Kick & Decay)
    void TriggerGlitchKick(float power = 1.5f, float decaySpeed = 5.0f);

    // --- GETTER METHODS (Panggil dari Camera & Shader) ---

    // Dapatkan offset posisi untuk kamera frame ini
    DirectX::XMFLOAT3 GetShakePosOffset() const { return m_shakeResultPos; }

    // Dapatkan offset rotasi untuk kamera frame ini (Pitch, Yaw, Roll)
    DirectX::XMFLOAT3 GetShakeRotOffset() const { return m_shakeResultRot; }

    // Dapatkan nilai intensitas glitch saat ini (untuk dikirim ke Shader)
    float GetGlitchIntensity() const { return m_currentGlitchIntensity; }

    // Reset semua efek (misal saat ganti scene)
    void Reset();

private:
    JuiceEngine() = default;
    ~JuiceEngine() = default;
    JuiceEngine(const JuiceEngine&) = delete;
    void operator=(const JuiceEngine&) = delete;

    // --- Internal State: Camera Shake ---
    ShakeSettings m_activeShake;
    float m_shakeTrauma = 0.0f;     // 0.0 to 1.0
    float m_shakeTimeCounter = 0.0f;
    DirectX::XMFLOAT3 m_shakeResultPos = { 0,0,0 };
    DirectX::XMFLOAT3 m_shakeResultRot = { 0,0,0 };

    // --- Internal State: Glitch Kick ---
    float m_currentGlitchIntensity = 0.0f;
    float m_glitchDecaySpeed = 5.0f;
};