#include <algorithm>
#include <imgui.h>
#include "AnimationComponent.h"
#include "EditorManager.h"
#include "ComponentRegistry.h"
#include "GameObject.h"
#include "MeshComponent.h"
#include "StringHash.h"

namespace
{
    void BlendPoses(
        const std::vector<Model::NodePose>& sourceA,
        const std::vector<Model::NodePose>& sourceB,
        float weight,
        std::vector<Model::NodePose>& outResult) noexcept
    {
        const std::size_t nodeCount{ (std::min)({ sourceA.size(), sourceB.size(), outResult.size() }) };
        const float safeWeight{ std::clamp(weight, 0.0f, 1.0f) };

        for (std::size_t i{ 0 }; i < nodeCount; ++i)
        {
            const DirectX::XMVECTOR s0{ DirectX::XMLoadFloat3(&sourceA[i].scale) };
            const DirectX::XMVECTOR r0{ DirectX::XMLoadFloat4(&sourceA[i].rotation) };
            const DirectX::XMVECTOR t0{ DirectX::XMLoadFloat3(&sourceA[i].position) };

            const DirectX::XMVECTOR s1{ DirectX::XMLoadFloat3(&sourceB[i].scale) };
            const DirectX::XMVECTOR r1{ DirectX::XMLoadFloat4(&sourceB[i].rotation) };
            const DirectX::XMVECTOR t1{ DirectX::XMLoadFloat3(&sourceB[i].position) };

            DirectX::XMStoreFloat3(&outResult[i].scale, DirectX::XMVectorLerp(s0, s1, safeWeight));
            DirectX::XMStoreFloat4(&outResult[i].rotation, DirectX::XMQuaternionSlerp(r0, r1, safeWeight));
            DirectX::XMStoreFloat3(&outResult[i].position, DirectX::XMVectorLerp(t0, t1, safeWeight));
        }
    }
}

float AnimationComponent::GetStateDurationByHash(const std::uint64_t stateHash) const noexcept
{
    if (!m_model) return 0.0f;
    for (std::size_t i{ 0 }; i < m_stateHashes.size(); ++i)
    {
        if (m_stateHashes[i] == stateHash)
        {
            const AnimationState& state{ m_states[i] };
            if (state.nodes.empty()) return 0.0f;

            float totalDuration = 0.0f;
            std::size_t nodeA = 0, nodeB = 0;
            float t = 0.0f;

            if (state.type == AnimStateType::Blend1D && state.nodes.size() > 1 && m_blackboard)
            {
                const float param{ m_blackboard->groundSpeed };
                if (param <= state.nodes.front().threshold) { nodeA = 0; nodeB = 0; }
                else if (param >= state.nodes.back().threshold) { nodeA = state.nodes.size() - 1; nodeB = nodeA; }
                else
                {
                    for (std::size_t j{ 0 }; j < state.nodes.size() - 1; ++j)
                    {
                        if (param >= state.nodes[j].threshold && param < state.nodes[j + 1].threshold)
                        {
                            nodeA = j; nodeB = j + 1;
                            const float range = state.nodes[nodeB].threshold - state.nodes[nodeA].threshold;
                            t = (param - state.nodes[nodeA].threshold) / (range > 0.001f ? range : 1.0f);
                            break;
                        }
                    }
                }
            }
            else if (state.type == AnimStateType::Selector && m_blackboard)
            {
                int rawIndex = m_blackboard->actionIndex;
                if (rawIndex < 0) rawIndex = 0;
                if (rawIndex >= static_cast<int>(state.nodes.size())) rawIndex = static_cast<int>(state.nodes.size()) - 1;
                nodeA = static_cast<std::size_t>(rawIndex);
                nodeB = nodeA;
            }

            const int clipA = state.nodes[nodeA].clipIndex;
            const int clipB = state.nodes[nodeB].clipIndex;
            const auto& animations = m_model->GetAnimations();

            if (clipA >= 0 && static_cast<std::size_t>(clipA) < animations.size() && clipB >= 0 && static_cast<std::size_t>(clipB) < animations.size())
            {
                totalDuration = (animations[clipA].secondsLength * (1.0f - t)) + (animations[clipB].secondsLength * t);
            }

            if (totalDuration > 0.001f)
            {
                const float currentSpeed = (state.nodes[nodeA].speedMultiplier * (1.0f - t)) + (state.nodes[nodeB].speedMultiplier * t);
                const float currentOffset = (state.nodes[nodeA].startOffset * (1.0f - t)) + (state.nodes[nodeB].startOffset * t);

                const float remainingDuration = (totalDuration - currentOffset) / (currentSpeed > 0.01f ? currentSpeed : 1.0f);
                return (remainingDuration > 0.0f) ? remainingDuration : 0.0f;
            }
            return 0.0f;
        }
    }
    return 0.0f;
}

void AnimationComponent::OnAttach(GameObject* owner) noexcept
{
    IComponent::OnAttach(owner);
    m_eventQueue.reserve(16);
    if (m_owner)
    {
        if (auto* meshComp{ m_owner->GetComponent<MeshComponent>() })
        {
            SetModel(meshComp->GetModel());
        }
    }
}

void AnimationComponent::SetModel(std::shared_ptr<Model> model) noexcept
{
    m_model = std::move(model);
    if (!m_model) return;

    const std::size_t nodeCount{ m_model->GetNodes().size() };

    m_currentLocalPoses.resize(nodeCount);
    m_snapshotPoses.resize(nodeCount);
    m_blendedLocalPoses.resize(nodeCount);

    m_currentNodeGlobals.resize(nodeCount);
    m_previousNodeGlobals.resize(nodeCount);

    m_scratchpad.bufferA.resize(nodeCount);
    m_scratchpad.bufferB.resize(nodeCount);
    m_scratchpad.result.resize(nodeCount);

    const auto& nodes{ m_model->GetNodes() };
    for (std::size_t i{ 0 }; i < nodeCount; ++i)
    {
        m_currentLocalPoses[i].position = nodes[i].position;
        m_currentLocalPoses[i].rotation = nodes[i].rotation;
        m_currentLocalPoses[i].scale = nodes[i].scale;
        m_snapshotPoses[i] = m_currentLocalPoses[i];
        m_blendedLocalPoses[i] = m_currentLocalPoses[i];
    }

    ComputeGlobalTransforms();
    m_previousNodeGlobals = m_currentNodeGlobals;
    m_hasPreviousGlobals = true;
}

void AnimationComponent::PlayState(const std::size_t stateIndex, bool forceRestart, int forceNodeIndex) noexcept
{
    if (stateIndex >= m_states.size()) return;

    if (m_currentStateIndex == stateIndex && !forceRestart) return;

    m_previousStateIndex = m_currentStateIndex;
    m_previousNodeIndex = m_currentNodeIndex;
    m_currentStateIndex = stateIndex;
    m_previousTimer = m_currentTimer;
    m_isolatedNodeIndex = forceNodeIndex;

    const AnimationState& targetState{ m_states[m_currentStateIndex] };

    // EVALUATE WHICH NODE WE ARE ENTERING FIRST 
    std::size_t entryNode = 0;
    if (!targetState.nodes.empty())
    {
        if (forceNodeIndex >= 0 && forceNodeIndex < static_cast<int>(targetState.nodes.size()))
        {
            entryNode = static_cast<std::size_t>(forceNodeIndex);
        }
        else if (targetState.type == AnimStateType::Selector && m_blackboard)
        {
            const int safeIndex = (std::max)(0, m_blackboard->actionIndex);
            const int maxIndex = targetState.nodes.empty() ? 0 : static_cast<int>(targetState.nodes.size()) - 1;
            entryNode = static_cast<std::size_t>(std::clamp(safeIndex, 0, maxIndex));
        }
        else if (targetState.type == AnimStateType::Blend1D && targetState.nodes.size() > 1 && m_blackboard)
        {
            const float param{ m_blackboard->groundSpeed };
            if (param <= targetState.nodes.front().threshold) { entryNode = 0; }
            else if (param >= targetState.nodes.back().threshold) { entryNode = targetState.nodes.size() - 1; }
            else
            {
                for (std::size_t i{ 0 }; i < targetState.nodes.size() - 1; ++i)
                {
                    if (param >= targetState.nodes[i].threshold && param < targetState.nodes[i + 1].threshold)
                    {
                        const float range = targetState.nodes[i + 1].threshold - targetState.nodes[i].threshold;
                        const float t = (param - targetState.nodes[i].threshold) / (range > 0.001f ? range : 1.0f);
                        entryNode = (t <= 0.5f) ? i : i + 1;
                        break;
                    }
                }
            }
        }
    }

    // EXTRACT PROPERTIES FROM THE DOMINANT NODE 
    float activeBlendDuration = targetState.nodes.empty() ? 0.2f : targetState.nodes[entryNode].blendDuration;
    float activeStartOffset = targetState.nodes.empty() ? 0.0f : targetState.nodes[entryNode].startOffset;
    bool doSync = !targetState.nodes.empty() && targetState.nodes[entryNode].syncPhase;
    bool ruleFound{ false };

    if (m_previousStateIndex < m_stateHashes.size() && !targetState.nodes.empty())
    {
        const std::uint64_t prevHash{ m_stateHashes[m_previousStateIndex] };
        for (const auto& rule : targetState.nodes[entryNode].transitionRules)
        {
            if (rule.sourceStateHash == prevHash &&
                (rule.sourceNodeIndex == -1 || rule.sourceNodeIndex == static_cast<int>(m_previousNodeIndex)))
            {
                activeBlendDuration = rule.blendDuration;
                activeStartOffset = rule.targetStartOffset;
                ruleFound = true;
                break;
            }
        }
    }

    m_currentNodeIndex = entryNode;

    // PHASE SYNC CALCULATIONS (With Memory Bounds Protection)
    if (!ruleFound && doSync && m_previousStateIndex < m_states.size())
    {
        const AnimationState& sourceState{ m_states[m_previousStateIndex] };

        // BOUNDS CHECK TO PREVENT CRASH
        std::size_t safePrevNode = 0;
        if (!sourceState.nodes.empty())
        {
            safePrevNode = std::clamp(m_previousNodeIndex, std::size_t(0), sourceState.nodes.size() - 1);
        }

        float srcDuration{ 1.0f };
        if (!sourceState.nodes.empty() && sourceState.nodes[safePrevNode].clipIndex >= 0)
            srcDuration = m_model->GetAnimations()[sourceState.nodes[safePrevNode].clipIndex].secondsLength;

        float targetDuration{ 1.0f };
        if (!targetState.nodes.empty() && targetState.nodes[entryNode].clipIndex >= 0)
            targetDuration = m_model->GetAnimations()[targetState.nodes[entryNode].clipIndex].secondsLength;

        if (srcDuration > 0.001f && targetDuration > 0.001f)
        {
            float normTime{ m_previousTimer / srcDuration };
            normTime = normTime - std::floor(normTime);
            m_currentTimer = normTime * targetDuration;
        }
        else { m_currentTimer = activeStartOffset; }
    }
    else
    {
        m_currentTimer = activeStartOffset;
    }

    if (activeBlendDuration > 0.001f)
    {
        m_isBlending = true;
        m_blendTimer = 0.0f;
        m_activeBlendDuration = activeBlendDuration;

        if (!m_blendedLocalPoses.empty()) m_snapshotPoses = m_blendedLocalPoses;
    }
    else
    {
        m_isBlending = false;
    }
}

void AnimationComponent::TestPlayState(std::size_t stateIndex, int isolatedNodeIndex) noexcept
{
    if (stateIndex >= m_states.size()) return;

    if (m_currentStateIndex == stateIndex)
    {
        float targetOffset = 0.0f;
        if (!m_states[stateIndex].nodes.empty())
        {
            int safeIdx = (std::max)(0, isolatedNodeIndex);
            if (safeIdx >= m_states[stateIndex].nodes.size()) safeIdx = 0;
            targetOffset = m_states[stateIndex].nodes[safeIdx].startOffset;
        }

        m_currentTimer = targetOffset;
        m_isBlending = false;
        m_blendTimer = 0.0f;
    }
    else
    {
        PlayState(stateIndex);
    }

    m_editorPreview = true;
    m_isolatedNodeIndex = isolatedNodeIndex;
}

void AnimationComponent::StopPreview() noexcept
{
    m_editorPreview = false;
    m_isolatedNodeIndex = -1;
    m_isBlending = false;
    m_blendTimer = 0.0f;
    m_currentTimer = 0.0f;

    // Hard reset back to the default state (State 0) so the game starts cleanly
    if (!m_states.empty())
    {
        m_previousStateIndex = 0;
        m_currentStateIndex = 0;
        m_previousNodeIndex = 0;
        m_currentNodeIndex = 0;
    }
}

void AnimationComponent::Update(const float dt)
{
    m_eventQueue.clear();

    if (!m_model) return;

    if (m_hasPreviousGlobals)
    {
        m_previousNodeGlobals = m_currentNodeGlobals;
    }

    if (m_states.empty() || m_currentStateIndex >= m_states.size()) return;

    float evalDt = dt;
    if (evalDt <= 0.0001f && m_editorPreview)
    {
        evalDt = ImGui::GetIO().DeltaTime;
    }
    else if (evalDt > 0.0001f && m_editorPreview)
    {
        // PREVENTIVE BUG FIX: The Engine just went Live
        // Nuke the preview instantly so the character doesn't resume a frozen animation.
        StopPreview();
    }
    else if (evalDt > 0.0001f)
    {
        m_editorPreview = false;
    }

    const AnimationState& targetState{ m_states[m_currentStateIndex] };
    const float previousFrameTimer{ m_currentTimer };

    if (!targetState.nodes.empty())
    {
        std::size_t nodeA{ 0 }, nodeB{ 0 };
        float t{ 0.0f };

        if (m_isolatedNodeIndex >= 0 && m_isolatedNodeIndex < static_cast<int>(targetState.nodes.size()))
        {
            nodeA = static_cast<std::size_t>(m_isolatedNodeIndex);
            nodeB = nodeA;
        }
        else if (targetState.type == AnimStateType::Blend1D && targetState.nodes.size() > 1 && m_blackboard)
        {
            // Extract the correct parameter dynamically
            float param = 0.0f;
            if (targetState.blendParam == BlendParamType::GroundSpeed) param = m_blackboard->groundSpeed;
            else if (targetState.blendParam == BlendParamType::VerticalVelocity) param = m_blackboard->verticalVelocity;

            if (param <= targetState.nodes.front().threshold) { nodeA = 0; nodeB = 0; }
            else if (param >= targetState.nodes.back().threshold) { nodeA = targetState.nodes.size() - 1; nodeB = nodeA; }
            else
            {
                for (std::size_t i{ 0 }; i < targetState.nodes.size() - 1; ++i)
                {
                    if (param >= targetState.nodes[i].threshold && param < targetState.nodes[i + 1].threshold)
                    {
                        nodeA = i; nodeB = i + 1;
                        const float range = targetState.nodes[nodeB].threshold - targetState.nodes[nodeA].threshold;
                        t = (param - targetState.nodes[nodeA].threshold) / (range > 0.001f ? range : 1.0f);
                        break;
                    }
                }
            }
        }
        else if (targetState.type == AnimStateType::Selector && m_blackboard)
        {
            int rawIndex = m_blackboard->actionIndex;
            if (rawIndex < 0) rawIndex = 0;
            if (rawIndex >= static_cast<int>(targetState.nodes.size())) rawIndex = static_cast<int>(targetState.nodes.size()) - 1;
            nodeA = static_cast<std::size_t>(rawIndex);
            nodeB = nodeA;
        }

        const int clipA{ targetState.nodes[nodeA].clipIndex };
        const int clipB{ targetState.nodes[nodeB].clipIndex };
        const auto& animations{ m_model->GetAnimations() };

        if (clipA >= 0 && static_cast<std::size_t>(clipA) < animations.size() &&
            clipB >= 0 && static_cast<std::size_t>(clipB) < animations.size())
        {
            const float durationA{ animations[clipA].secondsLength };
            const float durationB{ animations[clipB].secondsLength };
            const float currentDuration{ (durationA * (1.0f - t)) + (durationB * t) };

            if (currentDuration > 0.001f)
            {
                const float currentSpeed = (targetState.nodes[nodeA].speedMultiplier * (1.0f - t)) + (targetState.nodes[nodeB].speedMultiplier * t);
                m_currentTimer += (evalDt * currentSpeed);

                const std::size_t dominantNode = (t <= 0.5f) ? nodeA : nodeB;
                m_currentNodeIndex = dominantNode;

                ProcessEvents(evalDt, targetState.nodes[dominantNode].events, previousFrameTimer, m_currentTimer, currentDuration);

                if (targetState.nodes[dominantNode].isLooping)
                {
                    while (m_currentTimer >= currentDuration) m_currentTimer -= currentDuration;
                    while (m_currentTimer < 0.0f) m_currentTimer += currentDuration;
                }
                else
                {
                    m_currentTimer = std::clamp(m_currentTimer, 0.0f, currentDuration);

                    // AUTO-RESET: If a non-looping preview hits the end of the clip, turn off preview mode
                    if (m_editorPreview && m_currentTimer >= currentDuration)
                    {
                        StopPreview();
                    }
                }

                if (nodeA == nodeB) m_model->ComputeAnimation(clipA, m_currentTimer, m_currentLocalPoses);
                else
                {
                    const float normPhase{ m_currentTimer / currentDuration };
                    m_model->ComputeAnimation(clipA, normPhase * durationA, m_scratchpad.bufferA);
                    m_model->ComputeAnimation(clipB, normPhase * durationB, m_scratchpad.bufferB);
                    BlendPoses(m_scratchpad.bufferA, m_scratchpad.bufferB, t, m_currentLocalPoses);
                }

                const bool hasRootLock = targetState.nodes[dominantNode].lockRootX ||
                    targetState.nodes[dominantNode].lockRootY ||
                    targetState.nodes[dominantNode].lockRootZ;

                if (hasRootLock && !m_currentLocalPoses.empty())
                {
                    const int boneIdx = std::clamp(targetState.nodes[dominantNode].rootBoneIndex, 0, static_cast<int>(m_currentLocalPoses.size() - 1));
                    if (targetState.nodes[dominantNode].lockRootX) m_currentLocalPoses[boneIdx].position.x = m_model->GetNodes()[boneIdx].position.x;
                    if (targetState.nodes[dominantNode].lockRootY) m_currentLocalPoses[boneIdx].position.y = m_model->GetNodes()[boneIdx].position.y;
                    if (targetState.nodes[dominantNode].lockRootZ) m_currentLocalPoses[boneIdx].position.z = m_model->GetNodes()[boneIdx].position.z;
                }
            }
        }
    }

    if (m_isBlending)
    {
        m_blendTimer += evalDt;
        float t = (m_activeBlendDuration > 0.001f) ? (m_blendTimer / m_activeBlendDuration) : 1.0f;
        if (t >= 1.0f) { t = 1.0f; m_isBlending = false; }

        const float decayWeight = 1.0f - ((1.0f - t) * (1.0f - t) * (1.0f - t));

        for (std::size_t i = 0; i < m_blendedLocalPoses.size(); ++i)
        {
            const DirectX::XMVECTOR s0{ DirectX::XMLoadFloat3(&m_snapshotPoses[i].scale) };
            const DirectX::XMVECTOR r0{ DirectX::XMLoadFloat4(&m_snapshotPoses[i].rotation) };
            const DirectX::XMVECTOR t0{ DirectX::XMLoadFloat3(&m_snapshotPoses[i].position) };

            const DirectX::XMVECTOR s1{ DirectX::XMLoadFloat3(&m_currentLocalPoses[i].scale) };
            const DirectX::XMVECTOR r1{ DirectX::XMLoadFloat4(&m_currentLocalPoses[i].rotation) };
            const DirectX::XMVECTOR t1{ DirectX::XMLoadFloat3(&m_currentLocalPoses[i].position) };

            DirectX::XMStoreFloat3(&m_blendedLocalPoses[i].scale, DirectX::XMVectorLerp(s0, s1, decayWeight));
            DirectX::XMStoreFloat4(&m_blendedLocalPoses[i].rotation, DirectX::XMQuaternionSlerp(r0, r1, decayWeight));
            DirectX::XMStoreFloat3(&m_blendedLocalPoses[i].position, DirectX::XMVectorLerp(t0, t1, decayWeight));
        }
    }
    else
    {
        m_blendedLocalPoses = m_currentLocalPoses;
    }

    ComputeGlobalTransforms();

    if (!m_hasPreviousGlobals)
    {
        m_previousNodeGlobals = m_currentNodeGlobals;
        m_hasPreviousGlobals = true;
    }
}

void AnimationComponent::ProcessEvents(const float dt, const std::vector<AnimationEvent>& events, const float previousTimer, const float currentTimer, const float currentDuration) noexcept
{
    if (events.empty() || dt <= 0.0001f || currentDuration <= 0.001f) return;

    const float prevNorm{ previousTimer / currentDuration };
    const float currNorm{ currentTimer / currentDuration };
    const bool looped = (currNorm < prevNorm);

    for (const auto& ev : events)
    {
        if (looped)
        {
            if (ev.normalizedTime >= prevNorm || ev.normalizedTime <= currNorm) m_eventQueue.push_back(ev);
        }
        else
        {
            if (prevNorm <= ev.normalizedTime && currNorm > ev.normalizedTime) m_eventQueue.push_back(ev);
        }
    }
}

void AnimationComponent::ComputeGlobalTransforms() noexcept
{
    const auto& nodes{ m_model->GetNodes() };
    const std::size_t nodeCount{ (std::min)(nodes.size(), m_blendedLocalPoses.size()) };

    for (std::size_t i = 0; i < nodeCount; ++i)
    {
        const auto& node{ nodes[i] };
        const auto& pose{ m_blendedLocalPoses[i] };

        const DirectX::XMMATRIX S{ DirectX::XMMatrixScaling(pose.scale.x, pose.scale.y, pose.scale.z) };
        const DirectX::XMMATRIX R{ DirectX::XMMatrixRotationQuaternion(DirectX::XMLoadFloat4(&pose.rotation)) };
        const DirectX::XMMATRIX T{ DirectX::XMMatrixTranslation(pose.position.x, pose.position.y, pose.position.z) };
        const DirectX::XMMATRIX localTransform{ S * R * T };

        if (node.parentIndex >= 0 && static_cast<std::size_t>(node.parentIndex) < i)
        {
            const DirectX::XMMATRIX parentGlobal{ DirectX::XMLoadFloat4x4(&m_currentNodeGlobals[node.parentIndex]) };
            DirectX::XMStoreFloat4x4(&m_currentNodeGlobals[i], localTransform * parentGlobal);
        }
        else
        {
            DirectX::XMStoreFloat4x4(&m_currentNodeGlobals[i], localTransform);
        }
    }
}

void AnimationComponent::PlayStateByHash(const std::uint64_t stateHash, bool forceRestart, int forceNodeIndex) noexcept
{
    for (std::size_t i{ 0 }; i < m_stateHashes.size(); ++i)
    {
        if (m_stateHashes[i] == stateHash)
        {
            PlayState(i, forceRestart, forceNodeIndex);
            return;
        }
    }
}

void AnimationComponent::ScrubToTime(std::size_t stateIndex, float time) noexcept
{
    m_currentStateIndex = stateIndex;
    m_currentTimer = time;
    m_isBlending = false;
    m_editorPreview = false;
    m_isolatedNodeIndex = -1;

    if (!m_model || m_currentStateIndex >= m_states.size()) return;

    const AnimationState& state{ m_states[m_currentStateIndex] };

    if (!state.nodes.empty())
    {
        std::size_t nodeA = 0, nodeB = 0;
        float t = 0.0f;

        if (state.type == AnimStateType::Blend1D && state.nodes.size() > 1 && m_blackboard)
        {
            const float param{ m_blackboard->groundSpeed };
            if (param <= state.nodes.front().threshold) { nodeA = 0; nodeB = 0; }
            else if (param >= state.nodes.back().threshold) { nodeA = state.nodes.size() - 1; nodeB = nodeA; }
            else
            {
                for (std::size_t i{ 0 }; i < state.nodes.size() - 1; ++i)
                {
                    if (param >= state.nodes[i].threshold && param < state.nodes[i + 1].threshold)
                    {
                        nodeA = i; nodeB = i + 1;
                        const float range = state.nodes[nodeB].threshold - state.nodes[nodeA].threshold;
                        t = (param - state.nodes[nodeA].threshold) / (range > 0.001f ? range : 1.0f);
                        break;
                    }
                }
            }
        }
        else if (state.type == AnimStateType::Selector && m_blackboard)
        {
            int rawIndex = m_blackboard->actionIndex;
            if (rawIndex < 0) rawIndex = 0;
            if (rawIndex >= static_cast<int>(state.nodes.size())) rawIndex = static_cast<int>(state.nodes.size()) - 1;
            nodeA = static_cast<std::size_t>(rawIndex);
            nodeB = nodeA;
        }

        const int clipA = state.nodes[nodeA].clipIndex;
        const int clipB = state.nodes[nodeB].clipIndex;
        const auto& animations = m_model->GetAnimations();

        if (clipA >= 0 && static_cast<std::size_t>(clipA) < animations.size() && clipB >= 0 && static_cast<std::size_t>(clipB) < animations.size())
        {
            const float durationA{ animations[clipA].secondsLength };
            const float durationB{ animations[clipB].secondsLength };
            const float currentDuration = (durationA * (1.0f - t)) + (durationB * t);

            if (currentDuration > 0.001f)
            {
                if (nodeA == nodeB)
                {
                    m_model->ComputeAnimation(clipA, m_currentTimer, m_currentLocalPoses);
                }
                else
                {
                    const float normPhase = m_currentTimer / currentDuration;
                    m_model->ComputeAnimation(clipA, normPhase * durationA, m_scratchpad.bufferA);
                    m_model->ComputeAnimation(clipB, normPhase * durationB, m_scratchpad.bufferB);
                    BlendPoses(m_scratchpad.bufferA, m_scratchpad.bufferB, t, m_currentLocalPoses);
                }

                const std::size_t dominantNode = (t <= 0.5f) ? nodeA : nodeB;
                const bool hasRootLock = state.nodes[dominantNode].lockRootX ||
                    state.nodes[dominantNode].lockRootY ||
                    state.nodes[dominantNode].lockRootZ;

                if (hasRootLock && !m_currentLocalPoses.empty())
                {
                    const int boneIdx = std::clamp(state.nodes[dominantNode].rootBoneIndex, 0, static_cast<int>(m_currentLocalPoses.size() - 1));
                    if (state.nodes[dominantNode].lockRootX) m_currentLocalPoses[boneIdx].position.x = m_model->GetNodes()[boneIdx].position.x;
                    if (state.nodes[dominantNode].lockRootY) m_currentLocalPoses[boneIdx].position.y = m_model->GetNodes()[boneIdx].position.y;
                    if (state.nodes[dominantNode].lockRootZ) m_currentLocalPoses[boneIdx].position.z = m_model->GetNodes()[boneIdx].position.z;
                }
            }
        }
    }

    ComputeGlobalTransforms();
}

void AnimationComponent::ScrubNodeToTime(std::size_t stateIndex, std::size_t nodeIndex, float time) noexcept
{
    m_currentStateIndex = stateIndex;
    m_currentTimer = time;
    m_isBlending = false;
    m_editorPreview = false;
    m_isolatedNodeIndex = static_cast<int>(nodeIndex);

    if (!m_model || m_currentStateIndex >= m_states.size()) return;

    const AnimationState& state{ m_states[m_currentStateIndex] };
    if (nodeIndex >= state.nodes.size()) return;

    const int clipIdx = state.nodes[nodeIndex].clipIndex;
    if (clipIdx >= 0 && static_cast<std::size_t>(clipIdx) < m_model->GetAnimations().size())
    {
        m_model->ComputeAnimation(clipIdx, m_currentTimer, m_currentLocalPoses);
    }

    const bool hasRootLock = state.nodes[nodeIndex].lockRootX ||
        state.nodes[nodeIndex].lockRootY ||
        state.nodes[nodeIndex].lockRootZ;

    if (hasRootLock && !m_currentLocalPoses.empty())
    {
        const int boneIdx = std::clamp(state.nodes[nodeIndex].rootBoneIndex, 0, static_cast<int>(m_currentLocalPoses.size() - 1));
        if (state.nodes[nodeIndex].lockRootX) m_currentLocalPoses[boneIdx].position.x = m_model->GetNodes()[boneIdx].position.x;
        if (state.nodes[nodeIndex].lockRootY) m_currentLocalPoses[boneIdx].position.y = m_model->GetNodes()[boneIdx].position.y;
        if (state.nodes[nodeIndex].lockRootZ) m_currentLocalPoses[boneIdx].position.z = m_model->GetNodes()[boneIdx].position.z;
    }

    ComputeGlobalTransforms();
}

void AnimationComponent::AddState() noexcept
{
    AnimationState newState{};
    newState.name = "State_" + std::to_string(m_states.size());
    m_states.push_back(newState);
    m_stateHashes.push_back(Core::RuntimeHash(newState.name));
}

void AnimationComponent::RemoveState(std::size_t index) noexcept
{
    if (index < m_states.size())
    {
        m_states.erase(m_states.begin() + index);
        m_stateHashes.erase(m_stateHashes.begin() + index);

        if (m_currentStateIndex == index) m_currentStateIndex = 0;
        else if (m_currentStateIndex > index) m_currentStateIndex--;
    }
}

void AnimationComponent::RenameState(std::size_t index, const std::string& newName) noexcept
{
    if (index < m_states.size())
    {
        m_states[index].name = newName;
        m_stateHashes[index] = Core::RuntimeHash(newName);
    }
}

void AnimationComponent::DrawInspector()
{
    ImGui::TextDisabled("Animation Evaluator");
    ImGui::Separator();

    if (!m_model && m_owner)
    {
        if (auto* meshComp{ m_owner->GetComponent<MeshComponent>() })
        {
            SetModel(meshComp->GetModel());
        }
    }

    if (!m_model)
    {
        ImGui::TextColored(ImVec4{ 1.0f, 0.2f, 0.2f, 1.0f }, "Warning: No Model Attached to Component!");
        return;
    }

    if (!m_states.empty() && m_currentStateIndex < m_states.size())
    {
        ImGui::Text("Active State: %s", m_states[m_currentStateIndex].name.c_str());

        float progress{ 0.0f };
        if (!m_states[m_currentStateIndex].nodes.empty())
        {
            const int clipIdx{ m_states[m_currentStateIndex].nodes[0].clipIndex };
            if (clipIdx >= 0 && static_cast<std::size_t>(clipIdx) < m_model->GetAnimations().size())
            {
                const float duration{ m_model->GetAnimations()[clipIdx].secondsLength };
                if (duration > 0.001f) progress = m_currentTimer / duration;
            }
        }
        ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f), "Playback");
    }
    else
    {
        ImGui::TextDisabled("No active state.");
    }

    ImGui::Spacing();
    ImGui::Separator();

    if (ImGui::Button("Open Animation Graph Editor", ImVec2(-1.0f, 30.0f)))
    {
        EditorManager::Instance().OpenAnimationGraph(this);
    }

    if (ImGui::Button("Open Timeline Sequencer", ImVec2(-1.0f, 30.0f)))
    {
        EditorManager::Instance().OpenAnimationTimeline(this, m_states.empty() ? 0 : m_currentStateIndex);
    }
}

void AnimationComponent::Serialize(nlohmann::json& j) const
{
    nlohmann::json statesArray = nlohmann::json::array();
    for (const auto& state : m_states)
    {
        nlohmann::json stateJson{};
        stateJson["Name"] = state.name;
        stateJson["Type"] = static_cast<int>(state.type);
        stateJson["Slot"] = static_cast<int>(state.slot);

        nlohmann::json nodesArray = nlohmann::json::array();
        for (const auto& node : state.nodes)
        {
            nlohmann::json nodeJson{};
            nodeJson["Threshold"] = node.threshold;
            nodeJson["ClipIndex"] = node.clipIndex;
            nodeJson["LockRootX"] = node.lockRootX;
            nodeJson["LockRootY"] = node.lockRootY;
            nodeJson["LockRootZ"] = node.lockRootZ;
            nodeJson["RootBoneIndex"] = node.rootBoneIndex;
            nodeJson["Loop"] = node.isLooping;
            nodeJson["SyncPhase"] = node.syncPhase;
            nodeJson["StartOffset"] = node.startOffset;
            nodeJson["Speed"] = node.speedMultiplier;
            nodeJson["Blend"] = node.blendDuration;
            stateJson["BlendParam"] = static_cast<int>(state.blendParam);

            nlohmann::json eventsArray = nlohmann::json::array();
            for (const auto& ev : node.events)
            {
                nlohmann::json evJson{};
                evJson["Time"] = ev.normalizedTime;
                evJson["Id"] = ev.eventId;
                evJson["Payload"] = ev.payload;
                evJson["IsRange"] = ev.isRange;
                evJson["EndTime"] = ev.normalizedEndTime;
                eventsArray.push_back(evJson);
            }
            nodeJson["Events"] = eventsArray;

            nlohmann::json transArray = nlohmann::json::array();
            for (const auto& rule : node.transitionRules)
            {
                nlohmann::json ruleJson{};
                ruleJson["Source"] = rule.sourceStateName;
                ruleJson["NodeIdx"] = rule.sourceNodeIndex;
                ruleJson["Blend"] = rule.blendDuration;
                ruleJson["Offset"] = rule.targetStartOffset;
                transArray.push_back(ruleJson);
            }
            nodeJson["Transitions"] = transArray;

            nodesArray.push_back(nodeJson);
        }
        stateJson["Nodes"] = nodesArray;
        statesArray.push_back(stateJson);
    }
    j["States"] = statesArray;
}

void AnimationComponent::Deserialize(const nlohmann::json& j)
{
    m_states.clear();
    if (!j.contains("States")) return;

    for (const auto& stateJson : j["States"])
    {
        AnimationState state{};
        state.name = stateJson.value("Name", "State");
        state.type = static_cast<AnimStateType>(stateJson.value("Type", 0));
        state.slot = static_cast<Engine::Animation::AnimSlot>(stateJson.value("Slot", 0));
        state.blendParam = static_cast<BlendParamType>(stateJson.value("BlendParam", 0));

        if (stateJson.contains("Nodes"))
        {
            for (const auto& nodeJson : stateJson["Nodes"])
            {
                AnimNode node{};
                node.threshold = nodeJson.value("Threshold", 0.0f);
                node.clipIndex = nodeJson.value("ClipIndex", -1);
                if (nodeJson.contains("RootLock"))
                {
                    // Load old file format
                    const bool legacyLock = nodeJson.value("RootLock", true);
                    node.lockRootX = legacyLock;
                    node.lockRootY = legacyLock;
                    node.lockRootZ = legacyLock;
                }
                else
                {
                    // Load new file format
                    node.lockRootX = nodeJson.value("LockRootX", true);
                    node.lockRootY = nodeJson.value("LockRootY", true);
                    node.lockRootZ = nodeJson.value("LockRootZ", true);
                }
                node.rootBoneIndex = nodeJson.value("RootBoneIndex", 0);
                node.isLooping = nodeJson.value("Loop", true);
                node.syncPhase = nodeJson.value("SyncPhase", false);

                node.startOffset = nodeJson.value("StartOffset", 0.0f);
                node.speedMultiplier = nodeJson.value("Speed", 1.0f);
                node.blendDuration = nodeJson.value("Blend", 0.2f);

                if (nodeJson.contains("Events"))
                {
                    for (const auto& evJson : nodeJson["Events"])
                    {
                        AnimationEvent ev{};
                        ev.normalizedTime = evJson.value("Time", 0.0f);
                        ev.eventId = evJson.value("Id", 0u);
                        ev.payload = evJson.value("Payload", 0.0f);
                        ev.isRange = evJson.value("IsRange", false);
                        ev.normalizedEndTime = evJson.value("EndTime", 0.0f);
                        node.events.push_back(ev);
                    }
                }

                if (nodeJson.contains("Transitions"))
                {
                    for (const auto& ruleJson : nodeJson["Transitions"])
                    {
                        TransitionRule rule{};
                        rule.sourceStateName = ruleJson.value("Source", "");
                        rule.sourceStateHash = Core::RuntimeHash(rule.sourceStateName);
                        rule.sourceNodeIndex = ruleJson.value("NodeIdx", -1);
                        rule.blendDuration = ruleJson.value("Blend", 0.1f);
                        rule.targetStartOffset = ruleJson.value("Offset", 0.0f);
                        node.transitionRules.push_back(rule);
                    }
                }

                state.nodes.push_back(node);
            }
        }

        m_stateHashes.push_back(Core::RuntimeHash(state.name));
        m_states.push_back(state);
    }
    RebuildSlotTable();
}

// Rebuilds the O(1) lookup table. Call this after loading JSON or changing slots in the Editor.
void AnimationComponent::RebuildSlotTable() noexcept
{
    m_slotLookup.fill(-1);
    for (std::size_t i{ 0 }; i < m_states.size(); ++i)
    {
        if (m_states[i].slot != Engine::Animation::AnimSlot::None)
        {
            m_slotLookup[static_cast<std::size_t>(m_states[i].slot)] = static_cast<int16_t>(i);
        }
    }
}

// Direct index execution
float AnimationComponent::GetStateDurationByIndex(std::size_t stateIndex) const noexcept
{
    if (!m_model || stateIndex >= m_states.size()) return 0.0f;

    const AnimationState& state{ m_states[stateIndex] };
    if (state.nodes.empty()) return 0.0f;

    float totalDuration = 0.0f;
    std::size_t nodeA = 0, nodeB = 0;
    float t = 0.0f;

    if (state.type == AnimStateType::Blend1D && state.nodes.size() > 1 && m_blackboard)
    {
        float param = 0.0f;
        if (state.blendParam == BlendParamType::GroundSpeed) param = m_blackboard->groundSpeed;
        else if (state.blendParam == BlendParamType::VerticalVelocity) param = m_blackboard->verticalVelocity;

        if (param <= state.nodes.front().threshold) { nodeA = 0; nodeB = 0; }
        else if (param >= state.nodes.back().threshold) { nodeA = state.nodes.size() - 1; nodeB = nodeA; }
        else
        {
            for (std::size_t j{ 0 }; j < state.nodes.size() - 1; ++j)
            {
                if (param >= state.nodes[j].threshold && param < state.nodes[j + 1].threshold)
                {
                    nodeA = j; nodeB = j + 1;
                    const float range = state.nodes[nodeB].threshold - state.nodes[nodeA].threshold;
                    t = (param - state.nodes[nodeA].threshold) / (range > 0.001f ? range : 1.0f);
                    break;
                }
            }
        }
    }
    else if (state.type == AnimStateType::Selector && m_blackboard)
    {
        int rawIndex = m_blackboard->actionIndex;
        if (rawIndex < 0) rawIndex = 0;
        if (rawIndex >= static_cast<int>(state.nodes.size())) rawIndex = static_cast<int>(state.nodes.size()) - 1;
        nodeA = static_cast<std::size_t>(rawIndex);
        nodeB = nodeA;
    }

    const int clipA = state.nodes[nodeA].clipIndex;
    const int clipB = state.nodes[nodeB].clipIndex;
    const auto& animations = m_model->GetAnimations();

    if (clipA >= 0 && static_cast<std::size_t>(clipA) < animations.size() &&
        clipB >= 0 && static_cast<std::size_t>(clipB) < animations.size())
    {
        totalDuration = (animations[clipA].secondsLength * (1.0f - t)) + (animations[clipB].secondsLength * t);
    }

    if (totalDuration > 0.001f)
    {
        const float currentSpeed = (state.nodes[nodeA].speedMultiplier * (1.0f - t)) + (state.nodes[nodeB].speedMultiplier * t);
        const float currentOffset = (state.nodes[nodeA].startOffset * (1.0f - t)) + (state.nodes[nodeB].startOffset * t);

        const float remainingDuration = (totalDuration - currentOffset) / (currentSpeed > 0.01f ? currentSpeed : 1.0f);
        return (remainingDuration > 0.0f) ? remainingDuration : 0.0f;
    }
    return 0.0f;
}

REGISTER_COMPONENT(AnimationComponent)