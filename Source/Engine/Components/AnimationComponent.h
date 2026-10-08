#pragma once

#include <array>
#include <cstdint>
#include <DirectXMath.h>
#include <json.hpp>
#include <memory>
#include <string>
#include <vector>
#include "System/Model.h"
#include "AnimBlackboard.h"
#include "IComponent.h"

enum class CombatEventId : std::uint32_t
{
    None = 0,
    Hitbox_Active,
    Hitbox_Inactive,
    CancelWindow_Open,
    Invincible_Start,
    Invincible_End,
    Play_SFX,
    Play_VFX,
    Lunge_Impulse,
    Lunge_Vertical,
    Movement_Halt,
    Pose_HoldMarker
};

enum class AnimStateType : std::uint8_t {
    Single = 0,
    Blend1D,
    Selector
};

struct AnimationEvent
{
    float normalizedTime{ 0.0f };
    std::uint32_t eventId{ 0 };
    float payload{ 0.0f };
    bool isRange{ false };
    float normalizedEndTime{ 0.0f };
};

struct TransitionRule
{
    std::string sourceStateName{ "" };
    std::uint64_t sourceStateHash{ 0 };
    int sourceNodeIndex{ -1 }; // -1 means Any Node
    float blendDuration{ 0.1f };
    float targetStartOffset{ 0.0f };
};

// The Node is the absolute source of truth.
struct AnimNode
{
    float threshold{ 0.0f };
    int clipIndex{ -1 };

    bool lockRootX{ true };
    bool lockRootY{ true };
    bool lockRootZ{ true };
    int rootBoneIndex{ 0 };
    bool isLooping{ true };
    bool syncPhase{ false };

    float startOffset{ 0.0f };
    float speedMultiplier{ 1.0f };
    float blendDuration{ 0.2f };

    // Additive Weapon Grip Deltas (Node/Attack Specific Pivot Adjustment)
    bool hasGripOverride{ false };
    DirectX::XMFLOAT3 gripPosition{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 gripRotation{ 0.0f, 0.0f, 0.0f };

    std::vector<AnimationEvent> events{};
    std::vector<TransitionRule> transitionRules{};
};

enum class BlendParamType : std::uint8_t {
    GroundSpeed = 0,
    VerticalVelocity
};

struct AnimationState
{
    std::string name{ "State" };
    Engine::Animation::AnimSlot slot{ Engine::Animation::AnimSlot::None };
    AnimStateType type{ AnimStateType::Single };
    BlendParamType blendParam{ BlendParamType::GroundSpeed }; 
    std::vector<AnimNode> nodes{};
};

class AnimationComponent final : public IComponent
{
public:
    AnimationComponent() noexcept = default;
    ~AnimationComponent() override = default;

    AnimationComponent(const AnimationComponent&) = delete;
    AnimationComponent& operator=(const AnimationComponent&) = delete;

    void OnAttach(GameObject* owner) noexcept override;
    void Update(float dt) override;
    void DrawInspector() override;

    void Serialize(nlohmann::json& outJson) const override;
    void Deserialize(const nlohmann::json& inJson) override;

    [[nodiscard]] const char* GetTypeName() const noexcept override { return "AnimationComponent"; }

    void SetModel(std::shared_ptr<Model> model) noexcept;
    void SetBlackboard(const Engine::Animation::AnimBlackboard* bb) noexcept { m_blackboard = bb; }
    [[nodiscard]] const Engine::Animation::AnimBlackboard* GetBlackboard() const noexcept { return m_blackboard; }

    void PlayState(std::size_t stateIndex, bool forceRestart = false, int forceNodeIndex = -1) noexcept;
    void AddState() noexcept;
    void RemoveState(std::size_t index) noexcept;
    void RenameState(std::size_t index, const std::string& newName) noexcept;
    void PlayStateByHash(std::uint64_t stateHash, bool forceRestart = false, int forceNodeIndex = -1) noexcept;

    void ScrubToTime(std::size_t stateIndex, float targetTime) noexcept;
    void ScrubNodeToTime(std::size_t stateIndex, std::size_t nodeIndex, float targetTime) noexcept;

    void TestPlayState(std::size_t stateIndex, int isolatedNodeIndex = -1) noexcept;
    void StopPreview() noexcept;
    void JumpToPreviewTime(float time) noexcept { m_currentTimer = time; }

    // Called on Deserialize and when Editor Graph changes
    void RebuildSlotTable() noexcept;

    // O(1) Zero-Overhead Slot Execution (Esoterica Engine Principle)
    inline void PlaySlot(Engine::Animation::AnimSlot slot, bool forceRestart = false, int forceNodeIndex = -1) noexcept
    {
        const auto slotIdx{ static_cast<std::size_t>(slot) };
        if (slotIdx >= m_slotLookup.size()) return;

        const int16_t stateIdx{ m_slotLookup[slotIdx] };
        if (stateIdx >= 0)
        {
            PlayState(static_cast<std::size_t>(stateIdx), forceRestart, forceNodeIndex);
        }
    }

    // Resolves accurate timing for combat logic without hashing
    [[nodiscard]] inline float GetSlotDuration(Engine::Animation::AnimSlot slot) const noexcept
    {
        const auto slotIdx{ static_cast<std::size_t>(slot) };
        if (slotIdx >= m_slotLookup.size()) return 0.0f;

        const int16_t stateIdx{ m_slotLookup[slotIdx] };
        return (stateIdx >= 0) ? GetStateDurationByIndex(static_cast<std::size_t>(stateIdx)) : 0.0f;
    }

    [[nodiscard]] float GetStateDurationByHash(std::uint64_t stateHash) const noexcept;
    [[nodiscard]] const std::vector<DirectX::XMFLOAT4X4>& GetCurrentNodeGlobals() const noexcept { return m_currentNodeGlobals; }
    [[nodiscard]] const std::vector<DirectX::XMFLOAT4X4>& GetPreviousNodeGlobals() const noexcept { return m_hasPreviousGlobals ? m_previousNodeGlobals : m_currentNodeGlobals; }
    [[nodiscard]] const std::vector<AnimationEvent>& GetFiredEvents() const noexcept { return m_eventQueue; }

    [[nodiscard]] std::vector<AnimationState>& GetStates() noexcept { return m_states; }
    [[nodiscard]] std::shared_ptr<Model> GetModel() const noexcept { return m_model; }
    [[nodiscard]] float GetCurrentTimer() const noexcept { return m_currentTimer; }
    [[nodiscard]] float GetCurrentPhase() const noexcept { return m_currentPhase; }
    [[nodiscard]] std::size_t GetCurrentStateIndex() const noexcept { return m_currentStateIndex; }
    [[nodiscard]] std::size_t GetCurrentNodeIndex() const noexcept { return m_currentNodeIndex; }
    [[nodiscard]] bool IsPreviewing() const noexcept { return m_editorPreview; }
    [[nodiscard]] int GetIsolatedNodeIndex() const noexcept { return m_isolatedNodeIndex; }
    [[nodiscard]] const AnimNode* GetActiveAnimNode() const noexcept;

    void SetPlaybackSpeed(float speed) noexcept { m_playbackSpeed = speed; }
    [[nodiscard]] float GetPlaybackSpeed() const noexcept { return m_playbackSpeed; }

private:
    // Flat mapping array: maps AnimSlot directly to the m_states index.
    // -1 means the slot is unbound.
    std::array<int16_t, static_cast<std::size_t>(Engine::Animation::AnimSlot::Count)> m_slotLookup{};

    // Helper mapped to old hash logic
    [[nodiscard]] float GetStateDurationByIndex(std::size_t stateIndex) const noexcept;

    struct PoseScratchpad
    {
        std::vector<Model::NodePose> bufferA{};
        std::vector<Model::NodePose> bufferB{};
        std::vector<Model::NodePose> result{};
    };

    PoseScratchpad m_scratchpad{};

    void ComputeGlobalTransforms() noexcept;
    void ProcessEvents(float dt, const std::vector<AnimationEvent>& events, float previousTimer, float currentTimer, float currentDuration) noexcept;

    std::shared_ptr<Model> m_model{};

    std::vector<AnimationState> m_states{};

    std::size_t m_currentStateIndex{ 0 };
    std::size_t m_previousStateIndex{ 0 };
    std::size_t m_currentNodeIndex{ 0 };
    std::size_t m_previousNodeIndex{ 0 };

    const Engine::Animation::AnimBlackboard* m_blackboard{ nullptr };

    float m_currentTimer{ 0.0f };
    float m_currentPhase{ 0.0f };
    float m_previousTimer{ 0.0f };
    float m_blendTimer{ 0.0f };
    float m_activeBlendDuration{ 0.2f };
    bool m_isBlending{ false };

    std::vector<Model::NodePose> m_currentLocalPoses{};
    std::vector<Model::NodePose> m_snapshotPoses{};
    std::vector<Model::NodePose> m_blendedLocalPoses{};

    std::vector<DirectX::XMFLOAT4X4> m_currentNodeGlobals{};
    std::vector<DirectX::XMFLOAT4X4> m_previousNodeGlobals{};
    bool m_hasPreviousGlobals{ false };

    std::vector<AnimationEvent> m_eventQueue{};
    std::vector<std::uint64_t> m_stateHashes{};

    bool m_editorPreview{ false };
    bool m_previewPaused{ false };
    int m_isolatedNodeIndex{ -1 };

    float m_playbackSpeed{ 1.0f };
};