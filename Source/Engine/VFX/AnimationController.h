#pragma once
#include <vector>
#include <string>
#include <memory>
#include <DirectXMath.h> 
#include "System/Model.h"

class AnimationController
{
public:
    void Initialize(std::shared_ptr<Model> model);
    void Update(float dt);

    void Play(const std::string& name, bool loop = true, float blendTime = 0.2f);

    void SetPlaybackSpeed(float speed) { m_playbackSpeed = speed; }
    [[nodiscard]] float GetPlaybackSpeed() const { return m_playbackSpeed; }

    bool IsPlaying(const std::string& name) const;
    float GetCurrentTime() const { return timer; }

    void SetUpperBodyMaskRoot(const std::string& rootNodeName);
    void PlayUpper(const std::string& name, bool loop = false);

    bool IsUpperPlaying() const { return upperAnimIndex != -1; }

    [[nodiscard]] const std::vector<DirectX::XMFLOAT4X4>& GetCurrentNodeGlobals() const noexcept { return m_currentNodeGlobals; }
    [[nodiscard]] const std::vector<DirectX::XMFLOAT4X4>& GetPreviousNodeGlobals() const noexcept { return m_hasPreviousGlobals ? m_previousNodeGlobals : m_currentNodeGlobals; }

    void InvalidateHistory() noexcept { m_hasPreviousGlobals = false; }
    void SnapshotBones() noexcept;

    bool upperIsLooping = false;

private:
    std::shared_ptr<Model> ownerModel;

    int currentAnimIndex = -1;
    float timer = 0.0f;
    bool isLooping = true;
    float m_playbackSpeed{ 1.0f };
    int upperAnimIndex = -1;
    float upperTimer = 0.0f;

    // State Blending
    bool isBlending = false;
    float blendTimer = 0.0f;
    float blendDuration = 0.0f;

    // Buffer Pose
    std::vector<Model::NodePose> nodePoses;

    std::vector<Model::NodePose> prevNodePoses;

    std::vector<Model::NodePose> upperNodePoses;
    std::vector<bool> upperBodyMask;

    std::vector<DirectX::XMFLOAT4X4> m_currentNodeGlobals{};
    std::vector<DirectX::XMFLOAT4X4> m_previousNodeGlobals{};
    bool m_hasPreviousGlobals{ false };
};