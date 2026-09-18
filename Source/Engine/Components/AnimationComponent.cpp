#include <algorithm>
#include <imgui.h>
#include "AnimationComponent.h"
#include "ComponentRegistry.h"
#include "GameObject.h"
#include "MeshComponent.h"
#include "StringHash.h"

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
    m_previousLocalPoses.resize(nodeCount);
    m_blendedLocalPoses.resize(nodeCount);

    m_currentNodeGlobals.resize(nodeCount);
    m_previousNodeGlobals.resize(nodeCount);

    // Initialize buffers to the model's true bind pose 
    // If we don't do this, local translations default to (0,0,0), collapsing the mesh to the root
    const auto& nodes{ m_model->GetNodes() };
    for (std::size_t i{ 0 }; i < nodeCount; ++i)
    {
        m_currentLocalPoses[i].position = nodes[i].position;
        m_currentLocalPoses[i].rotation = nodes[i].rotation;
        m_currentLocalPoses[i].scale = nodes[i].scale;

        m_previousLocalPoses[i] = m_currentLocalPoses[i];
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
    if (m_currentStateIndex == stateIndex) return; // Prevent restarting the same state

    m_previousStateIndex = m_currentStateIndex;
    m_currentStateIndex = stateIndex;

    m_previousTimer = m_currentTimer;
    m_currentTimer = 0.0f; // Reset playhead for the new state

    const float blendTime{ m_states[m_currentStateIndex].blendDuration };
    if (blendTime > 0.001f)
    {
        m_isBlending = true;
        m_blendTimer = 0.0f;
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

    // Advance Target Playhead 
    if (targetState.clipIndex >= 0 && static_cast<std::size_t>(targetState.clipIndex) < m_model->GetAnimations().size())
    {
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

		// Root Motion Lock
        if (targetState.rootMotionLock && !m_currentLocalPoses.empty())
        {
            // Safely clamp the index so we don't crash if the bone doesn't exist
            const int boneIdx = std::clamp(targetState.rootBoneIndex, 0, static_cast<int>(m_currentLocalPoses.size() - 1));

            m_currentLocalPoses[boneIdx].position.x = 0.0f;
            // We keep Y intact so the character can still bounce vertically (e.g., breathing/bobbing)
            m_currentLocalPoses[boneIdx].position.z = 0.0f;
        }
    }

    // Crossfade Evaluation (
    if (m_isBlending && m_previousStateIndex < m_states.size())
    {
        m_blendTimer += evalDt;
        const float blendDuration{ targetState.blendDuration };

        float t = (blendDuration > 0.001f) ? (m_blendTimer / blendDuration) : 1.0f;

        if (t >= 1.0f)
        {
            t = 1.0f;
            m_isBlending = false;
        }

        const AnimationState& sourceState{ m_states[m_previousStateIndex] };

        if (sourceState.clipIndex >= 0 && static_cast<std::size_t>(sourceState.clipIndex) < m_model->GetAnimations().size())
        {
            m_previousTimer += (evalDt * sourceState.speedMultiplier);
            const float srcDuration{ m_model->GetAnimations()[sourceState.clipIndex].secondsLength };
            if (sourceState.isLooping && srcDuration > 0.001f)
            {
                while (m_previousTimer >= srcDuration) m_previousTimer -= srcDuration;
            }
            m_model->ComputeAnimation(sourceState.clipIndex, m_previousTimer, m_previousLocalPoses);

            // Root Motion Lock
            if (targetState.rootMotionLock && !m_currentLocalPoses.empty())
            {
                // Safely clamp the index so we don't crash if the bone doesn't exist
                const int boneIdx = std::clamp(targetState.rootBoneIndex, 0, static_cast<int>(m_currentLocalPoses.size() - 1));

                m_currentLocalPoses[boneIdx].position.x = 0.0f;
                // We keep Y intact so the character can still bounce vertically (e.g., breathing/bobbing)
                m_currentLocalPoses[boneIdx].position.z = 0.0f;
            }
        }

        // Mathematical Interpolation
        for (std::size_t i = 0; i < m_blendedLocalPoses.size(); ++i)
        {
            const DirectX::XMVECTOR s0{ DirectX::XMLoadFloat3(&m_previousLocalPoses[i].scale) };
            const DirectX::XMVECTOR r0{ DirectX::XMLoadFloat4(&m_previousLocalPoses[i].rotation) };
            const DirectX::XMVECTOR t0{ DirectX::XMLoadFloat3(&m_previousLocalPoses[i].position) };

            const DirectX::XMVECTOR s1{ DirectX::XMLoadFloat3(&m_currentLocalPoses[i].scale) };
            const DirectX::XMVECTOR r1{ DirectX::XMLoadFloat4(&m_currentLocalPoses[i].rotation) };
            const DirectX::XMVECTOR t1{ DirectX::XMLoadFloat3(&m_currentLocalPoses[i].position) };

            DirectX::XMStoreFloat3(&m_blendedLocalPoses[i].scale, DirectX::XMVectorLerp(s0, s1, t));
            DirectX::XMStoreFloat4(&m_blendedLocalPoses[i].rotation, DirectX::XMQuaternionSlerp(r0, r1, t));
            DirectX::XMStoreFloat3(&m_blendedLocalPoses[i].position, DirectX::XMVectorLerp(t0, t1, t));
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

    // Detect if the playhead crossed the normalized event marker this frame
    const float duration{ m_model->GetAnimations()[state.clipIndex].secondsLength };
    if (duration <= 0.001f) return;

    const float prevNorm{ previousTimer / duration };
    const float currNorm{ currentTimer / duration };

    for (const auto& ev : state.events)
    {
        if (prevNorm <= ev.normalizedTime && currNorm > ev.normalizedTime)
        {
            m_eventQueue.push_back(ev.eventId);
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
            ImGui::SliderFloat("Speed", &state.speedMultiplier, 0.1f, 5.0f);
            ImGui::SliderFloat("Blend Time", &state.blendDuration, 0.0f, 1.0f);

            ImGui::Spacing();

            // The Test Play Button 
            const float halfWidth = (ImGui::GetContentRegionAvail().x * 0.5f) - 4.0f;

            if (ImGui::Button("Test Play State", ImVec2(halfWidth, 0.0f)))
            {
                PlayState(i);
                m_editorPreview = true; // Force time to flow
            }

            ImGui::SameLine();

            // Split the button row to allow Stop Preview 
            if (ImGui::Button("Stop Preview", ImVec2(halfWidth, 0.0f)))
            {
                m_editorPreview = false; // Freeze the animation again
            }

            ImGui::Spacing();
            ImGui::Separator();

            // The Event Track Editor 
            ImGui::TextDisabled("EVENTS");
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60.0f);
            if (ImGui::Button("+ Event"))
            {
                state.events.push_back(AnimationEvent{ 0.0f, 0, 0.0f });
            }

            static constexpr const char* const s_eventNames[] = {
                "None", "Hitbox_Active", "Hitbox_Inactive",
                "CancelWindow_Open", "Invincible_Start", "Invincible_End",
                "Play_SFX", "Play_VFX"
            };

            // Using iterators to allow safe deletion while looping
            for (auto it = state.events.begin(); it != state.events.end(); )
            {
                // Unique ID based on memory address so ImGui doesn't mix sliders up
                ImGui::PushID(&(*it));

                ImGui::BeginGroup();

                // Normalized Time Slider (0.0 = Start of Animation, 1.0 = End)
                ImGui::SliderFloat("Time", &it->normalizedTime, 0.0f, 1.0f, "%.2f");

                // Event Type Dropdown
                int currentEventId{ static_cast<int>(it->eventId) };
                if (ImGui::Combo("Type", &currentEventId, s_eventNames, IM_ARRAYSIZE(s_eventNames)))
                {
                    it->eventId = static_cast<std::uint32_t>(currentEventId);
                }

                // Delete Event Button
                bool deleteTriggered{ false };
                if (ImGui::Button("Remove"))
                {
                    deleteTriggered = true;
                }

                ImGui::EndGroup();
                ImGui::PopID();
                ImGui::Spacing();

                if (deleteTriggered)
                {
                    it = state.events.erase(it);
                }
                else
                {
                    ++it;
                }
            }

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
            eventsArray.push_back(evJson);
        }
        stateJson["Events"] = eventsArray;

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
                state.events.push_back(ev);
            }
        }
        // Cache the runtime hash of the loaded name so we never do string comparisons later
        m_stateHashes.push_back(Core::RuntimeHash(state.name));

        m_states.push_back(state);
    }
}

REGISTER_COMPONENT(AnimationComponent)