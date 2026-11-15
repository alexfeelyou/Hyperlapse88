#pragma once

#include <algorithm>
#include <cmath>

namespace Engine::Time
{
    // Central dual-clock time dilation manager (Unscaled Real Time vs. Scaled Gameplay World Time).
    class TimeManager final
    {
    public:
        TimeManager(const TimeManager&) = delete;
        TimeManager& operator=(const TimeManager&) = delete;
        TimeManager(TimeManager&&) = delete;
        TimeManager& operator=(TimeManager&&) = delete;

        [[nodiscard]] static TimeManager& Instance() noexcept
        {
            static TimeManager s_instance{};
            return s_instance;
        }

        // Advances the gameplay time-dilation envelope using unscaled real-time delta.
        void Update(const float unscaledDt) noexcept
        {
            if (unscaledDt <= 0.0f)
            {
                return;
            }

            // Wait for blade-clash hit-stop to finish before engaging slow-motion
            if (m_delayTimer > 0.0f)
            {
                m_delayTimer -= unscaledDt;
                if (m_delayTimer > 0.0f)
                {
                    m_gameplayScale = 1.0f;
                    return;
                }
                m_delayTimer = 0.0f;
            }

            // Hold peak slow-motion scale
            if (m_holdTimer > 0.0f)
            {
                m_holdTimer -= unscaledDt;
                m_gameplayScale = m_targetScale;
                if (m_holdTimer <= 0.0f)
                {
                    m_holdTimer = 0.0f;
                }
                return;
            }

            // C1-continuous Hermite SmoothStep ease-out back to 1.0x
            if (m_recoveryTimer > 0.0f)
            {
                m_recoveryTimer -= unscaledDt;
                if (m_recoveryTimer <= 0.0f || m_recoveryDuration <= 0.0001f)
                {
                    m_recoveryTimer = 0.0f;
                    m_gameplayScale = 1.0f;
                }
                else
                {
                    // progress goes 0.0 (at peak slow-mo) -> 1.0 (fully restored 1.0x)
                    const float progress{ std::clamp(1.0f - (m_recoveryTimer / m_recoveryDuration), 0.0f, 1.0f) };
                    const float smoothT{ progress * progress * (3.0f - (2.0f * progress)) };
                    m_gameplayScale = m_targetScale + ((1.0f - m_targetScale) * smoothT);
                }
                return;
            }

            m_gameplayScale = 1.0f;
        }

        // Triggers or extends a non-stacking slow-motion envelope (safe against multi-enemy swarm parries).
        void TriggerSlowMotion(
            const float targetScale,
            const float holdDuration,
            const float recoveryDuration,
            const float hitStopDelay = 0.0f) noexcept
        {
            const float clampedScale{ std::clamp(targetScale, 0.05f, 1.0f) };
            const float clampedHold{ (std::max)(0.0f, holdDuration) };
            const float clampedRecovery{ (std::max)(0.0f, recoveryDuration) };
            const float clampedDelay{ (std::max)(0.0f, hitStopDelay) };

            if (IsSlowMotionActive())
            {
                // Non-stacking envelope: take the strongest slowdown and longest remaining window
                m_targetScale = (std::min)(m_targetScale, clampedScale);
                m_holdTimer = (std::max)(m_holdTimer, clampedHold);
                m_recoveryDuration = (std::max)(m_recoveryDuration, clampedRecovery);
                m_recoveryTimer = m_recoveryDuration;
            }
            else
            {
                m_targetScale = clampedScale;
                m_delayTimer = clampedDelay;
                m_holdTimer = clampedHold;
                m_recoveryDuration = clampedRecovery;
                m_recoveryTimer = clampedRecovery;
            }
        }

        // Immediately clears active gameplay slow-motion (used on scene reset, respawn, or Edit mode exit).
        void ResetGameplayDilation() noexcept
        {
            m_gameplayScale = 1.0f;
            m_targetScale = 1.0f;
            m_delayTimer = 0.0f;
            m_holdTimer = 0.0f;
            m_recoveryTimer = 0.0f;
            m_recoveryDuration = 0.0f;
        }

        // QA/Editor-only time multiplier (never serialized or used for gameplay logic).
        void SetDebugTimeScale(const float scale) noexcept
        {
            m_debugTimeScale = std::clamp(scale, 0.05f, 4.0f);
        }

        void ResetDebugTimeScale() noexcept
        {
            m_debugTimeScale = 1.0f;
        }

        [[nodiscard]] float GetDebugTimeScale() const noexcept { return m_debugTimeScale; }
        [[nodiscard]] float GetGameplayTimeScale() const noexcept { return m_gameplayScale; }
        [[nodiscard]] float GetWorldTimeScale() const noexcept { return m_gameplayScale * m_debugTimeScale; }

        [[nodiscard]] float GetWorldDeltaTime(const float unscaledDt) const noexcept
        {
            return unscaledDt * GetWorldTimeScale();
        }

        [[nodiscard]] bool IsSlowMotionActive() const noexcept
        {
            return (m_delayTimer > 0.0f) || (m_holdTimer > 0.0f) || (m_recoveryTimer > 0.0f);
        }

    private:
        TimeManager() noexcept = default;
        ~TimeManager() = default;

        float m_gameplayScale{ 1.0f };
        float m_debugTimeScale{ 1.0f };
        float m_targetScale{ 1.0f };

        float m_delayTimer{ 0.0f };
        float m_holdTimer{ 0.0f };
        float m_recoveryTimer{ 0.0f };
        float m_recoveryDuration{ 0.0f };
    };
}