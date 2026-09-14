#pragma once

#include <cstdint>
#include <DirectXMath.h>
#include <json.hpp>
#include <memory>
#include <string>
#include <vector>
#include "System/Model.h"
#include "IComponent.h"

// Strongly typed combat event IDs mapped directly to integers
enum class CombatEventId : std::uint32_t
{
    None = 0,
    Hitbox_Active,
    Hitbox_Inactive,
    CancelWindow_Open,
    Invincible_Start,
    Invincible_End,
    Play_SFX,
    Play_VFX
};

// Authored event embedded within an animation state's timeline
struct AnimationEvent
{
    float normalizedTime{ 0.0f };
    std::uint32_t eventId{ 0 };
    float payload{ 0.0f };
};

// Represents a single, tunable action state in the flat state machine
struct AnimationState
{
    std::string name{ "Idle" };
    int clipIndex{ -1 };
    float speedMultiplier{ 1.0f };
    float blendDuration{ 0.2f };
    bool isLooping{ true };
    std::vector<AnimationEvent> events{};
};

// Data-oriented animation evaluation engine
class AnimationComponent final : public IComponent
{
public:
    AnimationComponent() noexcept = default;
    ~AnimationComponent() override = default;

    // Enforce 1:1 entity mapping by deleting copy/move
    AnimationComponent(const AnimationComponent&) = delete;
    AnimationComponent& operator=(const AnimationComponent&) = delete;

    void OnAttach(GameObject* owner) noexcept override;
    void Update(float dt) override;
    void DrawInspector() override;

    void Serialize(nlohmann::json& outJson) const override;
    void Deserialize(const nlohmann::json& inJson) override;

    [[nodiscard]] const char* GetTypeName() const noexcept override { return "AnimationComponent"; }

    // Initialization & Rig Binding
    void SetModel(std::shared_ptr<Model> model) noexcept;

    // State Machine Interface
    void PlayState(std::size_t stateIndex) noexcept;
    void AddState(AnimationState state) noexcept { m_states.push_back(std::move(state)); }

    // Read-only accessors for the MeshComponent and Event consumers
    [[nodiscard]] const std::vector<DirectX::XMFLOAT4X4>& GetCurrentNodeGlobals() const noexcept { return m_currentNodeGlobals; }
    [[nodiscard]] const std::vector<DirectX::XMFLOAT4X4>& GetPreviousNodeGlobals() const noexcept { return m_hasPreviousGlobals ? m_previousNodeGlobals : m_currentNodeGlobals; }
    [[nodiscard]] const std::vector<std::uint32_t>& GetFiredEvents() const noexcept { return m_eventQueue; }

private:
    void ComputeGlobalTransforms() noexcept;
    void ProcessEvents(float dt, const AnimationState& state, float previousTimer, float currentTimer) noexcept;

    std::shared_ptr<Model> m_model{};

    // Flat state machine data
    std::vector<AnimationState> m_states{};
    std::size_t m_currentStateIndex{ 0 };
    std::size_t m_previousStateIndex{ 0 };

    // Playhead tracking
    float m_currentTimer{ 0.0f };
    float m_previousTimer{ 0.0f };
    float m_blendTimer{ 0.0f };
    bool m_isBlending{ false };

    // Fixed-size evaluation buffers (Allocated exactly once in SetModel)
    std::vector<Model::NodePose> m_currentLocalPoses{};
    std::vector<Model::NodePose> m_previousLocalPoses{};
    std::vector<Model::NodePose> m_blendedLocalPoses{};

    std::vector<DirectX::XMFLOAT4X4> m_currentNodeGlobals{};
    std::vector<DirectX::XMFLOAT4X4> m_previousNodeGlobals{};
    bool m_hasPreviousGlobals{ false };

    // Cleared and repopulated every frame; capacity reserved to prevent allocations
    std::vector<std::uint32_t> m_eventQueue{};

    // Track if we are forcing time to flow in the editor
    bool m_editorPreview{ false };
};