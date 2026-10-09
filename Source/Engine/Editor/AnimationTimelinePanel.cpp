#include <algorithm>
#include <cmath>
#include <cstdio>
#include <imgui.h>
#include <imgui_internal.h>
#include <string>
#include "AnimationTimelinePanel.h"
#include "AnimationComponent.h"
#include "CapsuleColliderComponent.h"
#include "CharacterMovementComponent.h"
#include "EditorManager.h"
#include "GameObject.h"

namespace
{
    inline constexpr const char* s_eventNames[] = {
         "None", "Hitbox_Active", "Hitbox_Inactive",
         "CancelWindow_Open", "Invincible_Start", "Invincible_End",
         "Play_SFX", "Play_VFX", "Lunge_Impulse", "Lunge_Vertical", "Movement_Halt",
         "Pose_HoldMarker"
    };

    [[nodiscard]] ImU32 GetColorForEvent(std::uint32_t eventId) noexcept
    {
        switch (static_cast<CombatEventId>(eventId))
        {
        case CombatEventId::Hitbox_Active:     return IM_COL32(250, 80, 80, 255);
        case CombatEventId::CancelWindow_Open: return IM_COL32(250, 200, 50, 255);
        case CombatEventId::Lunge_Impulse:     return IM_COL32(50, 150, 250, 255);
        case CombatEventId::Lunge_Vertical:    return IM_COL32(50, 250, 150, 255);
        case CombatEventId::Movement_Halt:     return IM_COL32(250, 120, 50, 255);
        case CombatEventId::Pose_HoldMarker:   return IM_COL32(50, 200, 250, 255);
        case CombatEventId::Play_SFX:
        case CombatEventId::Play_VFX:          return IM_COL32(200, 100, 250, 255);
        default:                               return IM_COL32(100, 200, 100, 255);
        }
    }
}

namespace
{
    // World position of the character captured on the first scrub/preview frame.
    // File-scope (not function-local) so EndPreview() can also reach it from outside Draw().
    DirectX::XMFLOAT3 s_previewAnchor{ 0.0f, 0.0f, 0.0f };
    bool s_hasPreviewAnchor{ false };

    // Puts the owner (Transform AND PhysX capsule) back on the saved anchor, then clears it.
    // The capsule is teleported explicitly because CapsuleColliderComponent::Update() only syncs
    // Transform -> controller in Edit mode; in Play mode the stale controller would win and
    // Move() would write the old scrub position back into the Transform.
    void RestorePreviewAnchor(GameObject* owner) noexcept
    {
        if (!s_hasPreviewAnchor) return;

        if (owner)
        {
            owner->SetPosition(s_previewAnchor);

            if (auto* capsule{ owner->GetComponent<CapsuleColliderComponent>() })
            {
                capsule->Teleport(s_previewAnchor);
            }
        }
        s_hasPreviewAnchor = false;
    }
}

void AnimationTimelinePanel::EndPreview() noexcept
{
    if (!m_targetComponent) return;

    // Drop the isolated scrub pose first, then undo the positional side effects of previewing.
    m_targetComponent->StopPreview();
    RestorePreviewAnchor(m_targetComponent->GetOwner());
}

void AnimationTimelinePanel::SetTarget(AnimationComponent* target, std::size_t stateIndex) noexcept
{
    m_targetComponent = target;
    m_selectedStateIndex = stateIndex;
    m_selectedNodeIndex = 0;
    m_selectedEventIndex = -1;
}

void AnimationTimelinePanel::Draw(bool* pOpen) noexcept
{
    if (!ImGui::Begin("AnimEvent Timeline", pOpen, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        ImGui::End();
        return;
    }

    // PREVENTIVE BUG FIX: Directional Mode Safety
    static bool s_wasGameLive = EditorManager::Instance().GetEditorMode() != EditorMode::Edit;
    const bool isGameLive = EditorManager::Instance().GetEditorMode() != EditorMode::Edit;

    if (s_wasGameLive && !isGameLive)
    {
        // Play -> Stop: Runtime scene is destroyed. Drop pointer to prevent accessing dead memory.
        m_targetComponent = nullptr;
    }
    else if (!s_wasGameLive && isGameLive)
    {
        // Stop -> Play: Keep target connected for live debugging, but cancel any isolated previewing.
        if (m_targetComponent)
        {
            m_targetComponent->StopPreview();
        }
    }
    s_wasGameLive = isGameLive;

    if (!m_targetComponent)
    {
        ImGui::TextDisabled("No Animation Component Selected.");
        ImGui::TextDisabled("Please re-select the GameObject in the Inspector.");
        ImGui::End();
        return;
    }

    auto& states{ m_targetComponent->GetStates() };
    if (states.empty())
    {
        ImGui::TextDisabled("Animation Component has no states initialized.");
        ImGui::End();
        return;
    }

    if (m_selectedStateIndex >= states.size())
    {
        m_selectedStateIndex = 0;
        m_selectedNodeIndex = 0;
        m_selectedEventIndex = -1;
    }

    // Define both variables so the rest of the UI buttons work
    const bool isPreviewing = m_targetComponent->IsPreviewing();
    const bool isEnginePlaying = isGameLive || isPreviewing;
    if (isGameLive)
    {
        const std::size_t runtimeState = m_targetComponent->GetCurrentStateIndex();
        if (runtimeState < states.size() && runtimeState != m_selectedStateIndex)
        {
            m_selectedStateIndex = runtimeState;
            m_selectedNodeIndex = 0;
            m_selectedEventIndex = -1;
        }
    }

    // Move Dynamic Sync OUTSIDE the play check
    if (m_targetComponent->GetIsolatedNodeIndex() == -1)
    {
        const std::size_t runtimeState = m_targetComponent->GetCurrentStateIndex();
        const std::size_t runtimeNode = m_targetComponent->GetCurrentNodeIndex();

        if (runtimeState == m_selectedStateIndex && runtimeNode < states[m_selectedStateIndex].nodes.size() && runtimeNode != m_selectedNodeIndex)
        {
            m_selectedNodeIndex = runtimeNode;
            m_selectedEventIndex = -1;
        }
    }

    // Physics Preview Anchor: put the character back whenever we are not previewing (or the game is live)
    if (isGameLive || !isPreviewing)
    {
        RestorePreviewAnchor(m_targetComponent->GetOwner());
    }
    else if (isPreviewing && !s_hasPreviewAnchor && m_targetComponent->GetOwner())
    {
        s_previewAnchor = m_targetComponent->GetOwner()->GetPosition();
        s_hasPreviewAnchor = true;
    }

    ImGui::BeginDisabled(isEnginePlaying && !isPreviewing);
    ImGui::SetNextItemWidth(250.0f);
    if (ImGui::BeginCombo("Active State", states[m_selectedStateIndex].name.c_str()))
    {
        for (std::size_t i{ 0 }; i < states.size(); ++i)
        {
            const bool isSelected{ m_selectedStateIndex == i };
            if (ImGui::Selectable(states[i].name.c_str(), isSelected))
            {
                m_selectedStateIndex = i;
                m_selectedNodeIndex = 0;
                m_selectedEventIndex = -1;

                // Force the 3D viewport to instantly pose to the newly selected state, 
                // regardless of whether the preview harness was previously running or stopped.
                if (!isGameLive)
                {
                    m_targetComponent->ScrubNodeToTime(m_selectedStateIndex, 0, 0.0f);
                }
            }
            if (isSelected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();

    AnimationState& state{ states[m_selectedStateIndex] };
    if (state.nodes.empty())
    {
        ImGui::TextDisabled("This state has no nodes to evaluate.");
        ImGui::End();
        return;
    }

    if (m_selectedNodeIndex >= state.nodes.size()) m_selectedNodeIndex = 0;

    if (state.nodes.size() > 1)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180.0f);

        std::string comboPreview;
        if (state.type == AnimStateType::Selector) comboPreview = "Combo Step " + std::to_string(m_selectedNodeIndex);
        else comboPreview = "Node " + std::to_string(m_selectedNodeIndex) + " (Thr: " + std::to_string(state.nodes[m_selectedNodeIndex].threshold).substr(0, 3) + ")";

        if (ImGui::BeginCombo("Node", comboPreview.c_str()))
        {
            for (std::size_t n = 0; n < state.nodes.size(); ++n)
            {
                std::string itemName;
                if (state.type == AnimStateType::Selector) itemName = "Combo Step " + std::to_string(n);
                else itemName = "Node " + std::to_string(n) + " (Thr: " + std::to_string(state.nodes[n].threshold).substr(0, 3) + ")";

                if (ImGui::Selectable(itemName.c_str(), m_selectedNodeIndex == n))
                {
                    m_selectedNodeIndex = n;
                    m_selectedEventIndex = -1;
                    m_targetComponent->ScrubNodeToTime(m_selectedStateIndex, m_selectedNodeIndex, 0.0f);
                }
            }
            ImGui::EndCombo();
        }
    }
    else
    {
        m_selectedNodeIndex = 0;
    }

    AnimNode& targetNode{ state.nodes[m_selectedNodeIndex] };
    const auto model{ m_targetComponent->GetModel() };

    float baseDuration{ 1.0f };
    if (model)
    {
        const int clipIdx{ targetNode.clipIndex };
        if (clipIdx >= 0 && static_cast<std::size_t>(clipIdx) < model->GetAnimations().size())
        {
            baseDuration = model->GetAnimations()[clipIdx].secondsLength;
        }
    }
    if (baseDuration <= 0.001f) baseDuration = 1.0f;

    // Live Physics Preview: Apply Impulse and Vertical Lunge events to the GameObject's position in real-time
    if (s_hasPreviewAnchor && m_targetComponent->GetOwner())
    {
        DirectX::XMFLOAT3 offset{ 0.0f, 0.0f, 0.0f };
        if (auto* motor = m_targetComponent->GetOwner()->GetComponent<CharacterMovementComponent>())
        {
            const float drag = motor->GetConfig().impulseDrag;
            const float gravity = motor->GetConfig().gravity;

            const float yaw = DirectX::XMConvertToRadians(m_targetComponent->GetOwner()->GetRotation().y);
            const DirectX::XMFLOAT2 fwd{ std::sin(yaw), std::cos(yaw) };

            const float currentTime = m_targetComponent->GetCurrentTimer();

            for (const auto& ev : targetNode.events)
            {
                const float evTime = ev.normalizedTime * baseDuration;
                if (currentTime > evTime)
                {
                    const float t = currentTime - evTime;
                    if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Lunge_Impulse))
                    {
                        const float dist = (drag > 0.001f) ? ((ev.payload / drag) * (1.0f - std::exp(-drag * t))) : (ev.payload * t);
                        offset.x += fwd.x * dist;
                        offset.z += fwd.y * dist;
                    }
                    else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Lunge_Vertical))
                    {
                        float dist = (ev.payload * t) + (0.5f * gravity * t * t);
                        if (dist < 0.0f && motor->isGrounded()) dist = 0.0f; // Basic floor collision approximation
                        offset.y += dist;
                    }
                }
            }
        }

        m_targetComponent->GetOwner()->SetPosition({
            s_previewAnchor.x + offset.x,
            s_previewAnchor.y + offset.y,
            s_previewAnchor.z + offset.z
            });
    }

    ImGui::SameLine();
    if (ImGui::Button("+ Add Event"))
    {
        targetNode.events.push_back(AnimationEvent{ 0.0f, 0, 0.0f, false, 0.1f });
        m_selectedEventIndex = static_cast<int>(targetNode.events.size() - 1);
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(isGameLive);
    if (ImGui::Button("Play"))
    {
        m_targetComponent->TestPlayState(m_selectedStateIndex, static_cast<int>(m_selectedNodeIndex));
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop"))
    {
        m_targetComponent->StopPreview();
    }
    ImGui::EndDisabled();

    // Synchronize Stance state directly with the shared AnimBlackboard
    if (auto* bb = const_cast<Engine::Animation::AnimBlackboard*>(m_targetComponent->GetBlackboard()))
    {
        ImGui::SameLine();
        bool isCombat = bb->getFlag(Engine::Animation::AnimFlag::is_combat_active);

        // Auto-assert combat stance for slots and specific node offsets that inherently require weapons drawn
        auto EvaluatesToCombat = [](Engine::Animation::AnimSlot slot, std::size_t nodeIdx) constexpr -> bool {
            using namespace Engine::Animation;
            switch (slot) {
            case AnimSlot::Attack_Primary:
            case AnimSlot::Attack_Contextual:
            case AnimSlot::Attack_Directional:
            case AnimSlot::Attack_Charged:
            case AnimSlot::Attack_Aerial:
            case AnimSlot::Attack_Plunge:
            case AnimSlot::Parry_Counter:
            case AnimSlot::HitReact:
            case AnimSlot::SkillBuff:
            case AnimSlot::Attack_Speed_Ground:
            case AnimSlot::Attack_Speed_Aerial:
            case AnimSlot::Locomotion_Combat:
                return true;
            case AnimSlot::Locomotion_Start: return nodeIdx >= 3;
            case AnimSlot::Locomotion_Stop:  return nodeIdx >= 5;
            case AnimSlot::PivotTurn:        return nodeIdx >= 2;
            case AnimSlot::DashEvade:        return nodeIdx >= 2;
            case AnimSlot::Landing:          return nodeIdx >= 3;
            case AnimSlot::AirTraversal:     return nodeIdx >= 3;
            case AnimSlot::Jump_Acrobatic:   return false; // Jump Acrobatic demotes to peaceful
            default:                         return false;
            }
            };

        const bool requiresCombatStance{ EvaluatesToCombat(state.slot, m_selectedNodeIndex) };

        // Edge-triggered auto-sync ensures unified defaults while keeping the checkbox interactive
        static std::size_t s_lastTimelineState = SIZE_MAX;
        static std::size_t s_lastTimelineNode = SIZE_MAX;

        if (m_selectedStateIndex != s_lastTimelineState || m_selectedNodeIndex != s_lastTimelineNode)
        {
            s_lastTimelineState = m_selectedStateIndex;
            s_lastTimelineNode = m_selectedNodeIndex;

            if (isCombat != requiresCombatStance)
            {
                isCombat = requiresCombatStance;
                bb->setFlag(Engine::Animation::AnimFlag::is_combat_active, isCombat);
                if (!isEnginePlaying) m_targetComponent->Update(0.0f);
            }
        }

        if (ImGui::Checkbox("Combat Stance", &isCombat))
        {
            // Mutate the shared memory contract so SocketComponent and AnimGraph update simultaneously
            bb->setFlag(Engine::Animation::AnimFlag::is_combat_active, isCombat);

            // Re-evaluate current pose immediately if paused in editor
            if (!isEnginePlaying)
            {
                m_targetComponent->Update(0.0f);
            }
        }
    }

    char speedBuf[64];
    snprintf(speedBuf, sizeof(speedBuf), "Base: %.2fs | Speed: %.1fx", baseDuration, targetNode.speedMultiplier);
    const float textWidth{ ImGui::CalcTextSize(speedBuf).x };

    ImGui::SameLine();
    const float currentX = ImGui::GetCursorPosX();
    const float targetX = ImGui::GetWindowWidth() - textWidth - 16.0f;

    // Dynamically shift the text to avoid overlapping the Combat Stance checkbox
    if (targetX > currentX + 10.0f) ImGui::SetCursorPosX(targetX);
    ImGui::TextDisabled("%s", speedBuf);

    ImGui::Separator();

    const bool showProperties{ m_selectedEventIndex >= 0 && static_cast<std::size_t>(m_selectedEventIndex) < targetNode.events.size() };

    float canvasWidth{ ImGui::GetContentRegionAvail().x };
    if (showProperties) canvasWidth -= 320.0f;
    if (canvasWidth < 100.0f) canvasWidth = 100.0f;

    ImDrawList* drawList{ ImGui::GetWindowDrawList() };
    const ImVec2 canvasPos{ ImGui::GetCursorScreenPos() };

    constexpr float headerHeight{ 36.0f };
    constexpr float trackHeight{ 30.0f };
    const float trackStartY{ canvasPos.y + headerHeight };

    const float availableY{ ImGui::GetContentRegionAvail().y };
    const float minimumContentHeight{ headerHeight + (targetNode.events.size() * trackHeight) + 20.0f };
    const float requiredHeight{ (std::max)(availableY, minimumContentHeight) };

    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(canvasWidth, requiredHeight));

    drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasWidth, canvasPos.y + requiredHeight), IM_COL32(20, 20, 24, 255));
    drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasWidth, canvasPos.y + headerHeight), IM_COL32(40, 40, 45, 255));

    for (std::size_t i{ 0 }; i < targetNode.events.size(); ++i)
    {
        const float yTop{ trackStartY + (i * trackHeight) };
        const float yBot{ yTop + trackHeight - 2.0f };
        drawList->AddRectFilled(ImVec2(canvasPos.x, yTop), ImVec2(canvasPos.x + canvasWidth, yBot), IM_COL32(30, 30, 35, 255));
    }

    const float offsetNorm{ std::clamp<float>(targetNode.startOffset / baseDuration, 0.0f, 1.0f) };
    if (offsetNorm > 0.001f)
    {
        const float offsetPixelX{ canvasPos.x + (offsetNorm * canvasWidth) };
        const float hatchAreaHeight{ requiredHeight - headerHeight };

        drawList->AddRectFilled(
            ImVec2(canvasPos.x, canvasPos.y + headerHeight),
            ImVec2(offsetPixelX, canvasPos.y + requiredHeight),
            IM_COL32(0, 0, 0, 180)
        );

        const ImVec2 clipMin{ canvasPos.x, canvasPos.y + headerHeight };
        const ImVec2 clipMax{ offsetPixelX, canvasPos.y + requiredHeight };
        drawList->PushClipRect(clipMin, clipMax, true);

        for (float x = canvasPos.x; x < offsetPixelX + hatchAreaHeight; x += 14.0f)
        {
            drawList->AddLine(
                ImVec2(x, canvasPos.y + headerHeight),
                ImVec2(x - hatchAreaHeight, canvasPos.y + requiredHeight),
                IM_COL32(255, 50, 50, 40), 2.0f
            );
        }
        drawList->PopClipRect();

        char skipBuf[32];
        snprintf(skipBuf, sizeof(skipBuf), "SKIPPED (%.2fs)", targetNode.startOffset);
        const ImVec2 textSize{ ImGui::CalcTextSize(skipBuf) };
        const float skipWidth{ offsetPixelX - canvasPos.x };

        if (skipWidth > textSize.x + 8.0f)
        {
            const float textX{ canvasPos.x + (skipWidth * 0.5f) - (textSize.x * 0.5f) };
            const float textY{ canvasPos.y + 18.0f };
            drawList->AddText(ImVec2(textX, textY), IM_COL32(255, 100, 100, 220), skipBuf);
        }
    }

    const int totalFrames{ static_cast<int>(std::round(baseDuration * 60.0f)) };

    // Prevent text overlap on extremely long clips (e.g. 10.0s Idle)
    const float pixelsPerFrame = canvasWidth / (totalFrames > 0 ? static_cast<float>(totalFrames) : 1.0f);
    int majorTickStep = 10;
    if (pixelsPerFrame * 10.0f < 60.0f) majorTickStep = 30;
    if (pixelsPerFrame * 30.0f < 60.0f) majorTickStep = 60;
    if (pixelsPerFrame * 60.0f < 60.0f) majorTickStep = 120;
    int minorTickStep = majorTickStep / 2;

    for (int i{ 0 }; i <= totalFrames; ++i)
    {
        const float t{ static_cast<float>(i) / 60.0f };
        const float normT{ t / baseDuration };
        const float xPixel{ canvasPos.x + (normT * canvasWidth) };

        if (i % majorTickStep == 0)
        {
            drawList->AddLine(ImVec2(xPixel, canvasPos.y + headerHeight - 10.0f), ImVec2(xPixel, canvasPos.y + headerHeight), IM_COL32(200, 200, 200, 255));
            char labelBuf[32];
            snprintf(labelBuf, sizeof(labelBuf), "%df (%.2fs)", i, t);
            drawList->AddText(ImVec2(xPixel + 2.0f, canvasPos.y + 2.0f), IM_COL32(150, 150, 150, 255), labelBuf);
        }
        else if (i % minorTickStep == 0)
        {
            drawList->AddLine(ImVec2(xPixel, canvasPos.y + headerHeight - 6.0f), ImVec2(xPixel, canvasPos.y + headerHeight), IM_COL32(150, 150, 150, 255));
        }
        else
        {
            drawList->AddLine(ImVec2(xPixel, canvasPos.y + headerHeight - 3.0f), ImVec2(xPixel, canvasPos.y + headerHeight), IM_COL32(100, 100, 100, 255));
        }
    }

    for (std::size_t i{ 0 }; i < targetNode.events.size(); ++i)
    {
        auto& ev{ targetNode.events[i] };
        const float yTop{ trackStartY + (i * trackHeight) };
        const float yBot{ yTop + trackHeight - 2.0f };

        const float xStartPixel{ canvasPos.x + (ev.normalizedTime * canvasWidth) };
        const ImU32 baseColor{ GetColorForEvent(ev.eventId) };
        const bool isSelected{ m_selectedEventIndex == static_cast<int>(i) };

        const ImU32 renderColor{ isSelected ? baseColor :
            ImGui::ColorConvertFloat4ToU32(ImVec4(
                ImGui::ColorConvertU32ToFloat4(baseColor).x,
                ImGui::ColorConvertU32ToFloat4(baseColor).y,
                ImGui::ColorConvertU32ToFloat4(baseColor).z, 0.5f))
        };

        if (ev.isRange)
        {
            const float xEndPixel{ canvasPos.x + (ev.normalizedEndTime * canvasWidth) };
            const ImRect rectBox{ ImVec2(xStartPixel, yTop + 2.0f), ImVec2(xEndPixel, yBot - 2.0f) };

            ImGui::SetCursorScreenPos(rectBox.Min);
            ImGui::InvisibleButton((std::string("##Range") + std::to_string(i)).c_str(), rectBox.GetSize());
            if (ImGui::IsItemClicked()) m_selectedEventIndex = static_cast<int>(i);

            drawList->AddRectFilled(rectBox.Min, rectBox.Max, renderColor);
            if (isSelected) drawList->AddRect(rectBox.Min, rectBox.Max, IM_COL32(255, 255, 255, 255), 0.0f, 0, 2.0f);

            if (isSelected && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                const float deltaNorm{ ImGui::GetIO().MouseDelta.x / canvasWidth };
                const float rangeWidth{ ev.normalizedEndTime - ev.normalizedTime };

                ev.normalizedTime = std::clamp<float>(ev.normalizedTime + deltaNorm, 0.0f, 1.0f - rangeWidth);
                ev.normalizedEndTime = ev.normalizedTime + rangeWidth;
            }
        }
        else
        {
            const ImVec2 p1{ xStartPixel, yTop + 4.0f };
            const ImVec2 p2{ xStartPixel + 8.0f, (yTop + yBot) * 0.5f };
            const ImVec2 p3{ xStartPixel, yBot - 4.0f };
            const ImVec2 p4{ xStartPixel - 8.0f, (yTop + yBot) * 0.5f };

            ImGui::SetCursorScreenPos(ImVec2(xStartPixel - 8.0f, yTop + 4.0f));
            ImGui::InvisibleButton((std::string("##Point") + std::to_string(i)).c_str(), ImVec2(16.0f, trackHeight - 8.0f));
            if (ImGui::IsItemClicked()) m_selectedEventIndex = static_cast<int>(i);

            drawList->AddQuadFilled(p1, p2, p3, p4, renderColor);
            if (isSelected) drawList->AddQuad(p1, p2, p3, p4, IM_COL32(255, 255, 255, 255), 2.0f);

            if (isSelected && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                const float deltaNorm{ ImGui::GetIO().MouseDelta.x / canvasWidth };
                ev.normalizedTime = std::clamp<float>(ev.normalizedTime + deltaNorm, 0.0f, 1.0f);
            }
        }

        drawList->AddText(ImVec2(canvasPos.x + 10.0f, yTop + 6.0f), IM_COL32(200, 200, 200, 255), s_eventNames[ev.eventId]);
    }

    const float currentNorm{ std::clamp<float>(m_targetComponent->GetCurrentTimer() / baseDuration, 0.0f, 1.0f) };
    const float playheadPixel{ canvasPos.x + (currentNorm * canvasWidth) };

    drawList->AddLine(ImVec2(playheadPixel, canvasPos.y), ImVec2(playheadPixel, canvasPos.y + requiredHeight), IM_COL32(250, 50, 50, 255), 2.0f);
    drawList->AddTriangleFilled(
        ImVec2(playheadPixel - 6.0f, canvasPos.y),
        ImVec2(playheadPixel + 6.0f, canvasPos.y),
        ImVec2(playheadPixel, canvasPos.y + 10.0f),
        IM_COL32(250, 50, 50, 255)
    );

    ImGui::SetCursorScreenPos(canvasPos);
    ImGui::InvisibleButton("##ScrubPlane", ImVec2(canvasWidth, requiredHeight));

    // Allow instant clicking anywhere on the track (IsItemActive), not just dragging
    if (!isGameLive && ImGui::IsItemActive())
    {
        const float mouseLocalX{ ImGui::GetIO().MousePos.x - canvasPos.x };

        // PREVENTIVE BUG: Clamp slightly below 1.0f. If the time hits 1.0f perfectly, 
        // the underlying AnimationComponent evaluates it as finished and fires StopPreview(), 
        // breaking the pose and resetting the character back to Idle.
        const float newNorm{ std::clamp<float>(mouseLocalX / canvasWidth, 0.0f, 0.999f) };

        m_targetComponent->ScrubNodeToTime(m_selectedStateIndex, m_selectedNodeIndex, newNorm * baseDuration);
    }

    ImGui::EndGroup();

    if (showProperties)
    {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(25, 25, 30, 255));
        ImGui::BeginChild("EventPropertiesPane", ImVec2(310.0f, requiredHeight), true, ImGuiWindowFlags_NoScrollbar);

        ImGui::TextDisabled("EVENT PROPERTIES");
        ImGui::SameLine(ImGui::GetWindowWidth() - 30.0f);

        if (ImGui::Button("X")) m_selectedEventIndex = -1;

        ImGui::Separator();

        if (m_selectedEventIndex != -1 && m_selectedEventIndex < targetNode.events.size())
        {
            auto& ev{ targetNode.events[m_selectedEventIndex] };

            int currentEventId{ static_cast<int>(ev.eventId) };
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::Combo("Type", &currentEventId, s_eventNames, static_cast<int>(std::size(s_eventNames))))
            {
                ev.eventId = static_cast<std::uint32_t>(currentEventId);
                // UX: Default to 1.0 (Full Stop) when newly selecting the Halt event so it works immediately
                if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Movement_Halt) && ev.payload == 0.0f)
                {
                    ev.payload = 1.0f;
                }
            }

            ImGui::Spacing();
            ImGui::Checkbox("Display as Range", &ev.isRange);
            ImGui::Spacing();

            const float startSec{ ev.normalizedTime * baseDuration };
            const int startFrame{ static_cast<int>(std::round(startSec * 60.0f)) };

            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Start: %df (%.2fs)", startFrame, startSec);
            ImGui::SliderFloat("##StartTime", &ev.normalizedTime, 0.0f, 1.0f, "Norm: %.3f");

            if (ev.isRange)
            {
                ImGui::Spacing();
                const float endSec{ ev.normalizedEndTime * baseDuration };
                const int endFrame{ static_cast<int>(std::round(endSec * 60.0f)) };

                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "End: %df (%.2fs)", endFrame, endSec);
                ImGui::SliderFloat("##EndTime", &ev.normalizedEndTime, ev.normalizedTime, 1.0f, "Norm: %.3f");

                if (ev.normalizedEndTime <= ev.normalizedTime)
                {
                    ev.normalizedEndTime = ev.normalizedTime + 0.01f;
                }
            }

            ImGui::Spacing();

            if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Lunge_Impulse) ||
                ev.eventId == static_cast<std::uint32_t>(CombatEventId::Lunge_Vertical) ||
                ev.eventId == static_cast<std::uint32_t>(CombatEventId::Movement_Halt) ||
                ev.eventId == static_cast<std::uint32_t>(CombatEventId::Play_SFX) ||
                ev.eventId == static_cast<std::uint32_t>(CombatEventId::Play_VFX))
            {
                ImGui::Separator();
                if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Lunge_Impulse)) ImGui::DragFloat("Horizontal Lunge", &ev.payload, 0.5f, -200.0f, 200.0f);
                else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Lunge_Vertical)) ImGui::DragFloat("Vertical Lift (m/s)", &ev.payload, 0.5f, -50.0f, 50.0f);
                else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Movement_Halt)) ImGui::DragFloat("Braking Factor", &ev.payload, 0.05f, 0.0f, 1.0f, "%.2f (1 = Stop)");
                else ImGui::DragFloat("Asset ID", &ev.payload, 1.0f, 0.0f, 100.0f);
                ImGui::Spacing();
            }

            ImGui::Separator();
            ImGui::Spacing();

            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(180, 40, 40, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(220, 50, 50, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(250, 60, 60, 255));

            if (ImGui::Button("Delete Event", ImVec2(-1.0f, 30.0f)))
            {
                targetNode.events.erase(targetNode.events.begin() + m_selectedEventIndex);
                m_selectedEventIndex = -1;
            }
            ImGui::PopStyleColor(3);
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    ImGui::End();
}