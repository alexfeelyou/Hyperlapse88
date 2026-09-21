#pragma once
#include <cstddef>

class AnimationComponent;

// A dedicated workspace for authoring Blend Trees and Macro States
class AnimationGraphPanel final
{
public:
    AnimationGraphPanel() noexcept = default;
    ~AnimationGraphPanel() = default;

    AnimationGraphPanel(const AnimationGraphPanel&) = delete;
    AnimationGraphPanel& operator=(const AnimationGraphPanel&) = delete;

    void Draw(bool* pOpen) noexcept;
    void SetTarget(AnimationComponent* target) noexcept;

private:
    AnimationComponent* m_targetComponent{ nullptr };
    std::size_t m_selectedStateIndex{ 0 };

    int m_selectedNodeForProps{ -1 };

    // Editor-only mock blackboard for live-scrubbing parameters without running the game
    float m_debugSpeed{ 0.0f };
    int m_debugActionIndex{ 0 };

    // Transition Preview Harness
    bool m_isPreviewingTransition{ false };
    int m_previewRuleIndex{ -1 };
    int m_transitionPhase{ 0 };
    float m_transitionLoopTimer{ 0.0f };
};