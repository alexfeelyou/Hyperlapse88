#include <imgui.h>
#include <string>
#include "System/Model.h"
#include "AnimationGraphPanel.h"
#include "AnimationComponent.h"
#include "EditorManager.h"
#include "StringHash.h"

void AnimationGraphPanel::SetTarget(AnimationComponent* target) noexcept
{
    m_targetComponent = target;
    m_selectedStateIndex = 0;
    m_selectedNodeForProps = -1;
}

void AnimationGraphPanel::Draw(bool* pOpen) noexcept
{
    if (!ImGui::Begin("Animation Graph", pOpen))
    {
        ImGui::End();
        return;
    }

    if (!m_targetComponent)
    {
        ImGui::TextDisabled("No Animation Component Selected.");
        ImGui::End();
        return;
    }

    auto& states{ m_targetComponent->GetStates() };
    if (states.empty())
    {
        ImGui::TextDisabled("Animation Component has no states initialized.");
        if (ImGui::Button("+ Initialize First State", ImVec2(-1.0f, 30.0f)))
        {
            m_targetComponent->AddState();
        }
        ImGui::End();
        return;
    }

    if (m_selectedStateIndex >= states.size())
    {
        m_selectedStateIndex = 0;
        m_selectedNodeForProps = -1;
    }

    // LEFT COLUMN: MACRO STATES 
    ImGui::BeginChild("MacroStates", ImVec2(200, 0), true);
    ImGui::TextDisabled("MACRO STATES");
    if (ImGui::Button("+ Add State", ImVec2(-1.0f, 0.0f)))
    {
        m_targetComponent->AddState();
        m_selectedStateIndex = states.size() - 1; // Auto-select the new state
        m_selectedNodeForProps = -1;
    }
    ImGui::Separator();

    for (std::size_t i{ 0 }; i < states.size(); ++i)
    {
        const bool isSelected{ m_selectedStateIndex == i };
        if (ImGui::Selectable(states[i].name.c_str(), isSelected))
        {
            m_selectedStateIndex = i;
            m_selectedNodeForProps = -1;
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // RIGHT COLUMN: GRAPH WORKSPACE & PROPERTIES
    AnimationState& state{ states[m_selectedStateIndex] };
    const auto model{ m_targetComponent->GetModel() };
    static const std::vector<Model::Animation> s_emptyAnims{};
    const auto& animations{ model ? model->GetAnimations() : s_emptyAnims };

    const bool showNodeProps = m_selectedNodeForProps >= 0 && m_selectedNodeForProps < static_cast<int>(state.nodes.size());

    float workWidth = ImGui::GetContentRegionAvail().x;
    if (showNodeProps) workWidth -= 320.0f;
    if (workWidth < 200.0f) workWidth = 200.0f;

    ImGui::BeginChild("GraphWorkspace", ImVec2(workWidth, 0), false);

    // STATE RENAMING AND DELETION 
    char nameBuf[64];
    strncpy_s(nameBuf, sizeof(nameBuf), state.name.c_str(), _TRUNCATE);
    ImGui::SetNextItemWidth(250.0f);
    if (ImGui::InputText("##StateName", nameBuf, sizeof(nameBuf)))
    {
        m_targetComponent->RenameState(m_selectedStateIndex, nameBuf);
    }

    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 100.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(180, 40, 40, 255));
    if (ImGui::Button("Delete State", ImVec2(100.0f, 0.0f)))
    {
        m_targetComponent->RemoveState(m_selectedStateIndex);
        m_selectedStateIndex = 0;
        m_selectedNodeForProps = -1;
        ImGui::PopStyleColor();
        ImGui::EndChild(); // Exit early to avoid reading deleted memory this frame
        ImGui::End();
        return;
    }
    ImGui::PopStyleColor();
    ImGui::Separator();

    static constexpr const char* s_typeNames[] = { "Single Clip", "1D Blend Tree (Locomotion)", "Selector (Combos/Multi-Dir)" };
    int currentType = static_cast<int>(state.type);
    if (ImGui::Combo("Graph Type", &currentType, s_typeNames, 3))
    {
        state.type = static_cast<AnimStateType>(currentType);
    }
    ImGui::Spacing();
    ImGui::Separator();

    ImGui::TextDisabled("EVALUATION NODES");
    if (ImGui::Button("+ Add Node"))
    {
        state.nodes.push_back(AnimNode{ 0.0f, -1 });
    }

    ImGui::BeginChild("BlendNodeList", ImVec2(0, 200), true);
    for (std::size_t i{ 0 }; i < state.nodes.size(); ++i)
    {
        ImGui::PushID(static_cast<int>(i));
        auto& node{ state.nodes[i] };

        if (state.type == AnimStateType::Blend1D)
        {
            ImGui::SetNextItemWidth(80.0f);
            ImGui::DragFloat("Threshold", &node.threshold, 0.1f, 0.0f, 100.0f, "%.1f");
        }
        else if (state.type == AnimStateType::Selector)
        {
            ImGui::TextDisabled("Index: %zu", i);
        }

        ImGui::SameLine();

        bool isActiveNode = (m_targetComponent->IsPreviewing() || EditorManager::Instance().GetEditorMode() != EditorMode::Edit) &&
            (m_targetComponent->GetCurrentNodeIndex() == i) &&
            (m_targetComponent->GetCurrentStateIndex() == m_selectedStateIndex);
        if (isActiveNode)
        {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), " >> ");
            ImGui::SameLine();
        }

        ImGui::SameLine();

        std::string currentClipName{ "None" };
        if (node.clipIndex >= 0 && static_cast<std::size_t>(node.clipIndex) < animations.size())
        {
            currentClipName = animations[node.clipIndex].name;
        }

        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::BeginCombo("Clip", currentClipName.c_str()))
        {
            for (std::size_t a{ 0 }; a < animations.size(); ++a)
            {
                const bool isClipSelected{ node.clipIndex == static_cast<int>(a) };
                if (ImGui::Selectable(animations[a].name.c_str(), isClipSelected))
                {
                    node.clipIndex = static_cast<int>(a);
                }
                if (isClipSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        if (ImGui::Button("Props"))
        {
            m_selectedNodeForProps = static_cast<int>(i);
        }

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(180, 40, 40, 255));
        if (ImGui::Button("X"))
        {
            state.nodes.erase(state.nodes.begin() + i);
            if (m_selectedNodeForProps == static_cast<int>(i)) m_selectedNodeForProps = -1;
            ImGui::PopStyleColor();
            ImGui::PopID();
            break;
        }
        ImGui::PopStyleColor();

        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::TextDisabled("LIVE BLACKBOARD PREVIEW");
    ImGui::Separator();

    const bool isGameLive{ EditorManager::Instance().GetEditorMode() != EditorMode::Edit };
    const bool isPreviewing{ m_targetComponent->IsPreviewing() };

    ImGui::BeginDisabled(isGameLive);

    // MACRO PLAYBACK CONTROLS
    if (ImGui::Button(isPreviewing ? "Stop Preview" : "Play Macro State", ImVec2(-1.0f, 30.0f)))
    {
        if (isPreviewing) m_targetComponent->StopPreview();
        else m_targetComponent->TestPlayState(m_selectedStateIndex, -1); // -1 = Evaluate full Blend Tree
    }
    ImGui::Spacing();

    // DYNAMIC BLACKBOARD UI BASED ON GRAPH TYPE 
    if (state.type == AnimStateType::Blend1D)
    {
        // Negative width tells ImGui to fill available space minus X pixels for the label
        ImGui::SetNextItemWidth(-120.0f);
        if (ImGui::SliderFloat("Speed (Mock)", &m_debugSpeed, 0.0f, 10.0f, "%.1f m/s"))
        {
            if (auto* bb = const_cast<Engine::Animation::AnimBlackboard*>(m_targetComponent->GetBlackboard()))
            {
                bb->groundSpeed = m_debugSpeed;
            }
            // Immediately force a pose evaluation so the 3D viewport updates while dragging the slider
            if (!isPreviewing && !isGameLive) m_targetComponent->Update(0.0f);
        }
    }
    else if (state.type == AnimStateType::Selector)
    {
        const int maxIndex = state.nodes.empty() ? 0 : static_cast<int>(state.nodes.size()) - 1;
        if (m_debugActionIndex > maxIndex) m_debugActionIndex = maxIndex;

        ImGui::SetNextItemWidth(-150.0f);
        if (ImGui::SliderInt("Action Index (Mock)", &m_debugActionIndex, 0, maxIndex))
        {
            if (auto* bb = const_cast<Engine::Animation::AnimBlackboard*>(m_targetComponent->GetBlackboard()))
            {
                bb->actionIndex = m_debugActionIndex;
            }
            if (!isPreviewing && !isGameLive) m_targetComponent->Update(0.0f);
        }
    }
    else // Single Clip
    {
        ImGui::TextDisabled("No blackboard parameters required for Single Clip.");
    }

    ImGui::EndDisabled();

    if (isGameLive)
    {
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.0f), "Preview disabled during Play Mode. Using live physics data.");
    }

    ImGui::EndChild();

    // PROPERTIES PANE
    if (showNodeProps)
    {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(25, 25, 30, 255));
        ImGui::BeginChild("NodePropsPane", ImVec2(310.0f, 0), true);

        ImGui::TextDisabled("NODE PROPERTIES");
        ImGui::SameLine(ImGui::GetWindowWidth() - 30.0f);
        if (ImGui::Button("X")) m_selectedNodeForProps = -1;
        ImGui::Separator();

        if (m_selectedNodeForProps >= 0 && m_selectedNodeForProps < static_cast<int>(state.nodes.size()))
        {
            auto& targetNode = state.nodes[m_selectedNodeForProps];

            ImGui::Spacing();
            ImGui::Checkbox("Looping", &targetNode.isLooping);
            ImGui::SameLine();
            ImGui::Checkbox("Sync Phase (On Entry)", &targetNode.syncPhase);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::SliderFloat("Start Offset", &targetNode.startOffset, 0.0f, 2.0f, "%.2f s");
            ImGui::SliderFloat("Speed Multiplier", &targetNode.speedMultiplier, 0.1f, 5.0f);
            ImGui::SliderFloat("Blend Time (Entry)", &targetNode.blendDuration, 0.0f, 1.0f);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Checkbox("In-Place (Lock Root)", &targetNode.rootMotionLock);
            if (targetNode.rootMotionLock)
            {
                ImGui::Spacing();
                ImGui::InputInt("Bone Index", &targetNode.rootBoneIndex);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextDisabled("TRANSITION OVERRIDES (INTO THIS NODE)");
            if (ImGui::Button("+ Add Rule"))
            {
                targetNode.transitionRules.push_back(TransitionRule{ "Dash", Core::RuntimeHash("Dash"), -1, 0.08f, 0.0f });
            }

            for (auto it = targetNode.transitionRules.begin(); it != targetNode.transitionRules.end(); )
            {
                ImGui::PushID(&(*it));
                char srcBuf[64];
                strncpy_s(srcBuf, sizeof(srcBuf), it->sourceStateName.c_str(), _TRUNCATE);

                ImGui::SetNextItemWidth(120.0f);
                if (ImGui::BeginCombo("From State", it->sourceStateName.c_str()))
                {
                    for (const auto& availableState : m_targetComponent->GetStates())
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

                ImGui::SameLine();
                ImGui::SetNextItemWidth(80.0f);
                ImGui::InputInt("Node", &it->sourceNodeIndex);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("-1 = Any Node");

                ImGui::SliderFloat("Blend Time", &it->blendDuration, 0.0f, 1.0f, "%.2f s");
                ImGui::SliderFloat("Start Offset", &it->targetStartOffset, 0.0f, 2.0f, "%.2f s");

                if (ImGui::Button("Remove Rule"))
                {
                    it = targetNode.transitionRules.erase(it);
                    ImGui::PopID();
                }
                else
                {
                    ++it;
                    ImGui::PopID();
                }
            }
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    ImGui::End();
}