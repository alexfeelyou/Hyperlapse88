#pragma once

#include <cstddef>

// Forward declaration 
class AnimationComponent;

// A dedicated, zero-allocation immediate-mode workspace tool
// Exists purely in Editor bounds; stripped mathematically in Release.
class AnimationTimelinePanel final
{
public:
    AnimationTimelinePanel() noexcept = default;
    ~AnimationTimelinePanel() = default;

    AnimationTimelinePanel(const AnimationTimelinePanel&) = delete;
    AnimationTimelinePanel& operator=(const AnimationTimelinePanel&) = delete;

    void Draw(bool* pOpen) noexcept;
    void SetTarget(AnimationComponent* target, std::size_t stateIndex = 0) noexcept;

private:
    AnimationComponent* m_targetComponent{ nullptr };
    std::size_t m_selectedStateIndex{ 0 };
    std::size_t m_selectedNodeIndex{ 0 }; 
    int m_selectedEventIndex{ -1 };
};