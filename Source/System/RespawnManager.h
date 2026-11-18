#pragma once

#include <cstdint>
#include <DirectXMath.h>
#include <string_view>
#include <vector>

class GameObject;

// Hot POD representation of an active respawn anchor
struct RespawnAnchorData
{
    std::uint64_t     id{ 0 };
    DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };
    float             yawDegrees{ 0.0f };
    const GameObject* owner{ nullptr }; // Lookup only
};

// Queued trigger event emitted during PhysX simulation and drained safely post-fetchResults
struct PendingRespawnEvent
{
    GameObject* playerObject{ nullptr };
    std::uint64_t     targetAnchorId{ 0 };
    float             fadeOutDuration{ 0.35f };
    float             fadeInDuration{ 0.35f };
    float             hazardDamage{ 0.0f };
    bool              applyDamage{ false };
};

// Centralized Data-Oriented Respawn Registry & Transition Orchestrator
class RespawnManager final
{
public:
    enum class TransitionPhase : std::uint8_t
    {
        Idle = 0,
        FadingOut,
        FadingIn
    };

    [[nodiscard]] static RespawnManager& Instance() noexcept
    {
        static RespawnManager s_instance{};
        return s_instance;
    }

    RespawnManager(const RespawnManager&) = delete;
    RespawnManager& operator=(const RespawnManager&) = delete;
    RespawnManager(RespawnManager&&) = delete;
    RespawnManager& operator=(RespawnManager&&) = delete;

    // Resets transient runtime state when entering/exiting Play Mode or switching scenes
    void ResetRuntimeState() noexcept;

    // Captures the player's initial spawn transform as a guaranteed anti-softlock fallback
    void SetFallbackSpawn(const DirectX::XMFLOAT3& position, float yawDegrees) noexcept;

    // Anchor Registry Operations (called by RespawnPointComponent lifecycle hooks)
    void RegisterAnchor(const RespawnAnchorData& anchor) noexcept;
    void UnregisterAnchor(const GameObject* owner) noexcept;
    void UpdateAnchorTransform(const GameObject* owner, const DirectX::XMFLOAT3& position, float yawDegrees) noexcept;

    // Query interface for Editor Gizmos & Target Validation
    [[nodiscard]] const RespawnAnchorData* FindAnchorById(std::uint64_t id) const noexcept;
    [[nodiscard]] const RespawnAnchorData* FindNearestAnchor(const DirectX::XMFLOAT3& worldPos) const noexcept;
    [[nodiscard]] const std::vector<RespawnAnchorData>& GetAnchors() const noexcept { return m_anchors; }

    // Called from PhysX onTrigger callback (deferred until simulation step completes)
    void EnqueueRespawn(const PendingRespawnEvent& request) noexcept;

    // Steps active fade/teleport sequence
    void Update(float unscaledDt) noexcept;

    // State Queries for SceneGame screen fade & input gating
    [[nodiscard]] bool IsRespawning() const noexcept { return m_phase != TransitionPhase::Idle; }
    [[nodiscard]] float GetFadeAlpha() const noexcept { return m_fadeAlpha; }

private:
    RespawnManager() = default;
    ~RespawnManager() = default;

    void ExecuteTeleportAndReset() noexcept;

    std::vector<RespawnAnchorData> m_anchors{};
    PendingRespawnEvent            m_activeRequest{};
    bool                           m_hasQueuedRequest{ false };

    TransitionPhase                m_phase{ TransitionPhase::Idle };
    float                          m_phaseTimer{ 0.0f };
    float                          m_fadeAlpha{ 0.0f };

    DirectX::XMFLOAT3              m_fallbackPos{ 0.0f, 0.0f, 0.0f };
    float                          m_fallbackYaw{ 0.0f };
};