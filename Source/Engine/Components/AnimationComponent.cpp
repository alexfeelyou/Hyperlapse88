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
    // Pure mathematical blend primitive (Linear Blend Space Evaluator).
    // Implemented as a free function to isolate math from component state.
    void BlendPoses(
        const std::vector<Model::NodePose>& sourceA,
        const std::vector<Model::NodePose>& sourceB,
        float weight,
        std::vector<Model::NodePose>& outResult) noexcept
    {
        // Safety check to prevent out-of-bounds access
        const std::size_t nodeCount{ (std::min)({ sourceA.size(), sourceB.size(), outResult.size() }) };

        // Strict clamping ensures weight cannot extrapolate beyond physical bounds
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
            const int clipIdx{ m_states[i].clipIndex };
            if (clipIdx >= 0 && static_cast<std::size_t>(clipIdx) < m_model->GetAnimations().size())
            {
                const float totalDuration = m_model->GetAnimations()[clipIdx].secondsLength / m_states[i].speedMultiplier;

                // Subtract start offset so timers match the trimmed length
                const float remainingDuration = totalDuration - (m_states[i].startOffset / m_states[i].speedMultiplier);
                return (remainingDuration > 0.0f) ? remainingDuration : 0.0f;
            }
        }
    }
    return 0.0f;
}

void AnimationComponent::OnAttach(GameObject* owner) noexcept
{
    IComponent::OnAttach(owner);
    m_eventQueue.reserve(16); // Prevent hot-path allocations

    // Lazy fetch if MeshComponent is already attached 
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

    // Allocate flat buffers exactly once
    m_currentLocalPoses.resize(nodeCount);
    m_snapshotPoses.resize(nodeCount); 
    m_blendedLocalPoses.resize(nodeCount);

    m_currentNodeGlobals.resize(nodeCount);
    m_previousNodeGlobals.resize(nodeCount);

    m_scratchpad.bufferA.resize(nodeCount);
    m_scratchpad.bufferB.resize(nodeCount);
    m_scratchpad.result.resize(nodeCount);

    // Initialize buffers to the model's true bind pose 
    // If we don't do this, local translations default to (0,0,0), collapsing the mesh to the root
    const auto& nodes{ m_model->GetNodes() };
    for (std::size_t i{ 0 }; i < nodeCount; ++i)
    {
        m_currentLocalPoses[i].position = nodes[i].position;
        m_currentLocalPoses[i].rotation = nodes[i].rotation;
        m_currentLocalPoses[i].scale = nodes[i].scale;

        m_snapshotPoses[i] = m_currentLocalPoses[i]; 
        m_blendedLocalPoses[i] = m_currentLocalPoses[i];
    }

    // Pre-calculate the globals 
    // Guarantees the character renders correctly on frame 1, even if no state is playing yet.
    ComputeGlobalTransforms();

    m_previousNodeGlobals = m_currentNodeGlobals;
    m_hasPreviousGlobals = true;
}

void AnimationComponent::PlayState(const std::size_t stateIndex) noexcept
{
    if (stateIndex >= m_states.size()) return;
    if (m_currentStateIndex == stateIndex) return;

    m_previousStateIndex = m_currentStateIndex;
    m_currentStateIndex = stateIndex;
    m_previousTimer = m_currentTimer;

    const AnimationState& targetState{ m_states[m_currentStateIndex] };

    // Resolve State-Pair Transition Rule
    float activeBlendDuration{ targetState.blendDuration };
    float activeStartOffset{ targetState.startOffset };
    bool ruleFound{ false };

    if (m_previousStateIndex < m_stateHashes.size())
    {
        const std::uint64_t prevHash{ m_stateHashes[m_previousStateIndex] };
        for (const auto& rule : targetState.transitionRules)
        {
            if (rule.sourceStateHash == prevHash)
            {
                activeBlendDuration = rule.blendDuration;
                activeStartOffset = rule.targetStartOffset;
                ruleFound = true;
                break;
            }
        }
    }

    // Playhead Evaluation
    if (!ruleFound && targetState.syncPhase && m_previousStateIndex < m_states.size())
    {
        const AnimationState& sourceState{ m_states[m_previousStateIndex] };

        float srcDuration{ 1.0f };
        if (sourceState.clipIndex >= 0 && static_cast<std::size_t>(sourceState.clipIndex) < m_model->GetAnimations().size())
            srcDuration = m_model->GetAnimations()[sourceState.clipIndex].secondsLength;

        float targetDuration{ 1.0f };
        if (targetState.clipIndex >= 0 && static_cast<std::size_t>(targetState.clipIndex) < m_model->GetAnimations().size())
            targetDuration = m_model->GetAnimations()[targetState.clipIndex].secondsLength;

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

    // Initiate Crossfade via Inertial Snapshot
    if (activeBlendDuration > 0.001f)
    {
        m_isBlending = true;
        m_blendTimer = 0.0f;
        m_activeBlendDuration = activeBlendDuration;

        // SNAPSHOT: Capture the exact state of the bones on the screen right now
        if (!m_blendedLocalPoses.empty())
        {
            m_snapshotPoses = m_blendedLocalPoses;
        }
    }
    else
    {
        m_isBlending = false;
    }
}

void AnimationComponent::Update(const float dt)
{
    m_eventQueue.clear();

    if (!m_model) return;

    // Snapshot history for TAA/Motion Blur before calculating the new frame
    if (m_hasPreviousGlobals)
    {
        m_previousNodeGlobals = m_currentNodeGlobals;
    }

    if (m_states.empty() || m_currentStateIndex >= m_states.size())
    {
        return;
    }

    // Editor Preview Time Override 
    // If the engine is in Edit Mode (dt == 0) but we are previewing, pull ImGui's time.
    float evalDt = dt;
    if (evalDt <= 0.0001f && m_editorPreview)
    {
        evalDt = ImGui::GetIO().DeltaTime;
    }
    else if (evalDt > 0.0001f)
    {
        m_editorPreview = false; // Auto-disable preview when entering real Play Mode
    }

    const AnimationState& targetState{ m_states[m_currentStateIndex] };
    const float previousFrameTimer{ m_currentTimer };

    // Evaluation Pipeline
    if (targetState.isBlendTree && !targetState.blendNodes.empty() && m_blackboard)
    {
        // Read parameter from Blackboard (Assume groundSpeed for 1D Locomotion)
        const float param{ m_blackboard->groundSpeed };

        // Find the two nodes bounding our parameter
        std::size_t nodeA = 0;
        std::size_t nodeB = 0;
        float t = 0.0f;

        if (param <= targetState.blendNodes.front().threshold)
        {
            // Below lowest threshold: clamp to first node
            nodeA = 0;
            nodeB = 0;
        }
        else if (param >= targetState.blendNodes.back().threshold)
        {
            // Above highest threshold: clamp to last node
            nodeA = targetState.blendNodes.size() - 1;
            nodeB = nodeA;
        }
        else
        {
            // Interpolate between the two bounding nodes
            for (std::size_t i{ 0 }; i < targetState.blendNodes.size() - 1; ++i)
            {
                if (param >= targetState.blendNodes[i].threshold && param < targetState.blendNodes[i + 1].threshold)
                {
                    nodeA = i;
                    nodeB = i + 1;
                    const float range = targetState.blendNodes[nodeB].threshold - targetState.blendNodes[nodeA].threshold;
                    t = (param - targetState.blendNodes[nodeA].threshold) / (range > 0.001f ? range : 1.0f);
                    break;
                }
            }
        }

        const int clipA = targetState.blendNodes[nodeA].clipIndex;
        const int clipB = targetState.blendNodes[nodeB].clipIndex;
        const auto& animations = m_model->GetAnimations();

        if (clipA >= 0 && static_cast<std::size_t>(clipA) < animations.size() &&
            clipB >= 0 && static_cast<std::size_t>(clipB) < animations.size())
        {
            // 3. Phase Synchronization: Calculate Blended Duration
            const float durationA{ animations[clipA].secondsLength };
            const float durationB{ animations[clipB].secondsLength };
            const float blendedDuration = (durationA * (1.0f - t)) + (durationB * t);

            if (blendedDuration > 0.001f)
            {
                m_currentTimer += (evalDt * targetState.speedMultiplier);

                // Assuming blend trees loop for locomotion
                while (m_currentTimer >= blendedDuration) m_currentTimer -= blendedDuration;
                while (m_currentTimer < 0.0f) m_currentTimer += blendedDuration;

                // Convert Absolute Time to Normalized Phase (0.0 to 1.0)
                const float normPhase = m_currentTimer / blendedDuration;

                // Sample both clips into scratchpads using Normalized Phase!
                m_model->ComputeAnimation(clipA, normPhase * durationA, m_scratchpad.bufferA);
                m_model->ComputeAnimation(clipB, normPhase * durationB, m_scratchpad.bufferB);

                // Blend the two scratchpads into the final output
                BlendPoses(m_scratchpad.bufferA, m_scratchpad.bufferB, t, m_currentLocalPoses);
            }
        }
    }
    else if (!targetState.isBlendTree && targetState.clipIndex >= 0 && static_cast<std::size_t>(targetState.clipIndex) < m_model->GetAnimations().size())
    {
        // Standard single clip evaluator (For Actions, Combat, Dashes)
        const float duration{ m_model->GetAnimations()[targetState.clipIndex].secondsLength };
        if (duration > 0.001f)
        {
            m_currentTimer += (evalDt * targetState.speedMultiplier);

            ProcessEvents(evalDt, targetState, previousFrameTimer, m_currentTimer);

            if (targetState.isLooping)
            {
                while (m_currentTimer >= duration) m_currentTimer -= duration;
                while (m_currentTimer < 0.0f) m_currentTimer += duration;
            }
            else
            {
                m_currentTimer = std::clamp(m_currentTimer, 0.0f, duration);
            }
        }

        m_model->ComputeAnimation(targetState.clipIndex, m_currentTimer, m_currentLocalPoses);
    }

    // Inertial Blending
    if (m_isBlending)
    {
        m_blendTimer += evalDt;
        const float blendDuration{ m_activeBlendDuration };

        float t = (blendDuration > 0.001f) ? (m_blendTimer / blendDuration) : 1.0f;
        if (t >= 1.0f)
        {
            t = 1.0f;
            m_isBlending = false;
        }

        // Cubic Ease-Out: Creates a natural physical "spring" damping effect 
        // Fast initial snap to the new pose, slowing down organically as it settles.
        const float decayWeight = 1.0f - ((1.0f - t) * (1.0f - t) * (1.0f - t));

        // Mathematical Interpolation between the frozen Snapshot and the moving Target clip
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

void AnimationComponent::ProcessEvents(const float dt, const AnimationState& state, const float previousTimer, const float currentTimer) noexcept
{
    if (state.events.empty() || dt <= 0.0001f) return;

    const float duration{ m_model->GetAnimations()[state.clipIndex].secondsLength };
    if (duration <= 0.001f) return;

    const float prevNorm{ previousTimer / duration };
    const float currNorm{ currentTimer / duration };

    // Detect if the animation playhead wrapped around (Looping)
    const bool looped = (currNorm < prevNorm);

    for (const auto& ev : state.events)
    {
        if (looped)
        {
            // If it looped, check both ends of the timeline
            if (ev.normalizedTime >= prevNorm || ev.normalizedTime <= currNorm)
            {
                m_eventQueue.push_back(ev); // Push full struct!
            }
        }
        else
        {
            // Standard linear evaluation
            if (prevNorm <= ev.normalizedTime && currNorm > ev.normalizedTime)
            {
                m_eventQueue.push_back(ev); // Push full struct
            }
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

void AnimationComponent::PlayStateByHash(const std::uint64_t stateHash) noexcept
{
    // Linear search
    for (std::size_t i{ 0 }; i < m_stateHashes.size(); ++i)
    {
        if (m_stateHashes[i] == stateHash)
        {
            PlayState(i);
            return;
        }
    }
}

void AnimationComponent::ScrubToTime(std::size_t stateIndex, float time) noexcept
{
    m_currentStateIndex = stateIndex; 
    m_currentTimer = time;
    m_isBlending = false;

    if (m_model && m_currentStateIndex < m_states.size())
    {
        const AnimationState& state{ m_states[m_currentStateIndex] };
        if (state.clipIndex >= 0 && static_cast<std::size_t>(state.clipIndex) < m_model->GetAnimations().size())
        {
            // Mathematically command the skeleton to evaluating the explicit pose right now
            m_model->ComputeAnimation(state.clipIndex, m_currentTimer, m_currentLocalPoses);
            ComputeGlobalTransforms();
        }
    }
}

void AnimationComponent::DrawInspector()
{
    ImGui::TextDisabled("Animation Evaluator");
    ImGui::Separator();

    // Fallback lazy-fetch if JSON load order bypassed the other checks 
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

    // Live Playback HUD
    if (!m_states.empty() && m_currentStateIndex < m_states.size())
    {
        ImGui::Text("Active State: %s", m_states[m_currentStateIndex].name.c_str());

        float progress{ 0.0f };
        const int clipIdx{ m_states[m_currentStateIndex].clipIndex };
        if (clipIdx >= 0 && static_cast<std::size_t>(clipIdx) < m_model->GetAnimations().size())
        {
            const float duration{ m_model->GetAnimations()[clipIdx].secondsLength };
            if (duration > 0.001f) progress = m_currentTimer / duration;
        }

        ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f), "Playback");
    }
    else
    {
        ImGui::TextDisabled("No active state.");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("STATE MACHINE LIBRARY");

    // Add New State Button 
    if (ImGui::Button("+ Add State", ImVec2(-1.0f, 0.0f)))
    {
        AnimationState newState{};
        newState.name = "State_" + std::to_string(m_states.size());
        m_states.push_back(newState); 

        // Sync the cache array instantly
        m_stateHashes.push_back(Core::RuntimeHash(m_states.back().name));
    }

    ImGui::Spacing();

    // The List-Based Editor
    const auto& animations{ m_model->GetAnimations() };

    for (std::size_t i{ 0 }; i < m_states.size(); ++i)
    {
        AnimationState& state{ m_states[i] };

        // Push ID ensures ImGui doesn't mix up sliders for states with the same name
        ImGui::PushID(static_cast<int>(i));

        // Use ### to decouple the visible name from the stable internal ID
        const std::string nodeLabel{ state.name + "###StateNode_" + std::to_string(i) };
        if (ImGui::TreeNodeEx(nodeLabel.c_str(), ImGuiTreeNodeFlags_Framed))
        {
            // Edit State Name
            char nameBuf[64];
            strncpy_s(nameBuf, sizeof(nameBuf), state.name.c_str(), _TRUNCATE);
            if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
            {
                state.name = nameBuf;

                // INSTANT HASH: Re-hash the new string so C++ code can find it immediately
                m_stateHashes[i] = Core::RuntimeHash(state.name);
            }

            // Dropdown: Select which clip from the .glb this state plays
            std::string currentClipName{ "None" };
            if (state.clipIndex >= 0 && static_cast<std::size_t>(state.clipIndex) < animations.size())
            {
                currentClipName = animations[state.clipIndex].name;
            }

            if (ImGui::BeginCombo("Clip", currentClipName.c_str()))
            {
                for (std::size_t a{ 0 }; a < animations.size(); ++a)
                {
                    const bool isSelected{ state.clipIndex == static_cast<int>(a) };
                    if (ImGui::Selectable(animations[a].name.c_str(), isSelected))
                    {
                        state.clipIndex = static_cast<int>(a);
                    }
                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            // Tuning Parameters
            ImGui::Checkbox("In-Place (Lock Root)", &state.rootMotionLock);
            if (state.rootMotionLock)
            {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(100.0f);
                ImGui::InputInt("Bone Index", &state.rootBoneIndex);
            }
            ImGui::Checkbox("Looping", &state.isLooping);
            ImGui::SameLine();
            ImGui::Checkbox("Sync Phase", &state.syncPhase);
            ImGui::SliderFloat("Start Offset", &state.startOffset, 0.0f, 2.0f, "%.2f s"); 
            ImGui::SliderFloat("Speed", &state.speedMultiplier, 0.1f, 5.0f);
            ImGui::SliderFloat("Blend Time", &state.blendDuration, 0.0f, 1.0f);

            ImGui::Spacing();

            // The Test Play Button 
            const float halfWidth = (ImGui::GetContentRegionAvail().x * 0.5f) - 4.0f;

            // Disable preview buttons if we are in Play or Pause mode
            const bool isGameLive = EditorManager::Instance().GetEditorMode() != EditorMode::Edit;
            ImGui::BeginDisabled(isGameLive);

            if (ImGui::Button("Test Play State", ImVec2(halfWidth, 0.0f)))
            {
                if (m_currentStateIndex == static_cast<std::size_t>(i))
                {
                    m_currentTimer = state.startOffset;
                    m_isBlending = false;
                    m_blendTimer = 0.0f;
                }
                else
                {
                    PlayState(i);
                }
                m_editorPreview = true;
            }

            ImGui::SameLine();

            if (ImGui::Button("Stop Preview", ImVec2(halfWidth, 0.0f)))
            {
                m_editorPreview = false;
            }

            ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextDisabled("EVENT TIMELINE WORKSPACE");

            if (ImGui::Button("Open Timeline Sequencer", ImVec2(-1.0f, 0.0f)))
            {
                // Pass 'i' (the specific state index) to the Editor Manager
                EditorManager::Instance().OpenAnimationTimeline(this, i);
            }

            ImGui::Spacing();
            ImGui::TextDisabled("TRANSITION OVERRIDES");
            if (ImGui::Button("+ Add Rule"))
            {
                state.transitionRules.push_back(TransitionRule{ "Dash", Core::RuntimeHash("Dash"), 0.08f, 0.2f });
            }

            for (auto it = state.transitionRules.begin(); it != state.transitionRules.end(); )
            {
                ImGui::PushID(&(*it));
                char srcBuf[64];
                strncpy_s(srcBuf, sizeof(srcBuf), it->sourceStateName.c_str(), _TRUNCATE);
                if (ImGui::BeginCombo("From State", it->sourceStateName.c_str()))
                {
                    for (const auto& availableState : m_states)
                    {
                        const bool isSelected = (it->sourceStateName == availableState.name);
                        if (ImGui::Selectable(availableState.name.c_str(), isSelected))
                        {
                            it->sourceStateName = availableState.name;
                            it->sourceStateHash = Core::RuntimeHash(it->sourceStateName);
                        }
                        if (isSelected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SliderFloat("Blend Time", &it->blendDuration, 0.0f, 1.0f, "%.2f s");
                ImGui::SliderFloat("Start Offset", &it->targetStartOffset, 0.0f, 2.0f, "%.2f s");

                if (ImGui::Button("Remove Rule"))
                {
                    it = state.transitionRules.erase(it);
                    ImGui::PopID();
                }
                else
                {
                    ++it;
                    ImGui::PopID();
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            // Use a red button for destructive actions
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
            if (ImGui::Button("Delete Entire State", ImVec2(-1.0f, 0.0f)))
            {
                // Erase both the state and its cached hash safely
                m_states.erase(m_states.begin() + i);
                m_stateHashes.erase(m_stateHashes.begin() + i);

                ImGui::PopStyleColor();
                ImGui::TreePop();
                ImGui::PopID();
                break; // Break the loop instantly to prevent ImGui iteration crashes
            }
            ImGui::PopStyleColor();

            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

void AnimationComponent::Serialize(nlohmann::json& j) const
{
    nlohmann::json statesArray = nlohmann::json::array();

    for (const auto& state : m_states)
    {
        nlohmann::json stateJson{};
        stateJson["Name"] = state.name;
        stateJson["ClipIndex"] = state.clipIndex;
        stateJson["RootLock"] = state.rootMotionLock;
        stateJson["RootBoneIndex"] = state.rootBoneIndex;
        stateJson["SyncPhase"] = state.syncPhase;
        stateJson["StartOffset"] = state.startOffset;
        stateJson["Speed"] = state.speedMultiplier;
        stateJson["Blend"] = state.blendDuration;
        stateJson["Loop"] = state.isLooping;

        nlohmann::json eventsArray = nlohmann::json::array();
        for (const auto& ev : state.events)
        {
            nlohmann::json evJson{};
            evJson["Time"] = ev.normalizedTime;
            evJson["Id"] = ev.eventId;
            evJson["Payload"] = ev.payload;
            evJson["IsRange"] = ev.isRange;
            evJson["EndTime"] = ev.normalizedEndTime;
            eventsArray.push_back(evJson);
        }
        stateJson["Events"] = eventsArray;

        nlohmann::json transArray = nlohmann::json::array();
        for (const auto& rule : state.transitionRules)
        {
            nlohmann::json ruleJson{};
            ruleJson["Source"] = rule.sourceStateName;
            ruleJson["Blend"] = rule.blendDuration;
            ruleJson["Offset"] = rule.targetStartOffset;
            transArray.push_back(ruleJson);
        }
        stateJson["Transitions"] = transArray;

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
        state.clipIndex = stateJson.value("ClipIndex", -1);
        state.rootMotionLock = stateJson.value("RootLock", true);
		state.rootBoneIndex = stateJson.value("RootBoneIndex", 0);
        state.syncPhase = stateJson.value("SyncPhase", false);
        state.startOffset = stateJson.value("StartOffset", 0.0f);
        state.speedMultiplier = stateJson.value("Speed", 1.0f);
        state.blendDuration = stateJson.value("Blend", 0.2f);
        state.isLooping = stateJson.value("Loop", true);

        if (stateJson.contains("Events"))
        {
            for (const auto& evJson : stateJson["Events"])
            {
                AnimationEvent ev{};
                ev.normalizedTime = evJson.value("Time", 0.0f);
                ev.eventId = evJson.value("Id", 0u);
                ev.payload = evJson.value("Payload", 0.0f);
                ev.isRange = evJson.value("IsRange", false);
                ev.normalizedEndTime = evJson.value("EndTime", 0.0f);
                state.events.push_back(ev);
            }
        }

        if (stateJson.contains("Transitions"))
        {
            for (const auto& ruleJson : stateJson["Transitions"])
            {
                TransitionRule rule{};
                rule.sourceStateName = ruleJson.value("Source", "");
                rule.sourceStateHash = Core::RuntimeHash(rule.sourceStateName);
                rule.blendDuration = ruleJson.value("Blend", 0.1f);
                rule.targetStartOffset = ruleJson.value("Offset", 0.0f);
                state.transitionRules.push_back(rule);
            }
        }

        // Cache the runtime hash of the loaded name so we never do string comparisons later
        m_stateHashes.push_back(Core::RuntimeHash(state.name));

        m_states.push_back(state);
    }
}

REGISTER_COMPONENT(AnimationComponent)