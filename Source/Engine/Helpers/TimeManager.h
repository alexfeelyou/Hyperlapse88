#pragma once
#include <algorithm>

// Singleton untuk manipulasi skala waktu global
class TimeManager {
public:
    static TimeManager& Instance() {
        static TimeManager instance;
        return instance;
    }

    // Update harus dipanggil dengan waktu murni (Unscaled Delta Time)
    void Update(float unscaledDt) {
        if (m_hitStopTimer > 0.0f) {
            m_hitStopTimer -= unscaledDt;
            if (m_hitStopTimer <= 0.0f) {
                m_hitStopMultiplier = 1.0f; // Waktu kembali normal
            }
        }
    }

    // Panggil ini dari mana saja (misal: saat kena damage, parry, ledakan)
    void TriggerHitStop(float durationInSeconds, float timeScaleDuringHitStop = 0.0f) {
        m_hitStopTimer = durationInSeconds;
        m_hitStopMultiplier = timeScaleDuringHitStop;
    }

    float GetHitStopMultiplier() const { return m_hitStopMultiplier; }

private:
    TimeManager() = default;
    float m_hitStopMultiplier = 1.0f;
    float m_hitStopTimer = 0.0f;
};