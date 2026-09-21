#include <algorithm>
#include <cstdio>
#include <imgui.h>
#include <imgui_internal.h>
#include <string>
#include "AnimationTimelinePanel.h"
#include "AnimationComponent.h"
#include "EditorManager.h"

namespace
{
    // Shared compile-time constants for the timeline UI
    inline constexpr const char* s_eventNames[] = {
         "None", "Hitbox_Active", "Hitbox_Inactive",
         "CancelWindow_Open", "Invincible_Start", "Invincible_End",
         "Play_SFX", "Play_VFX", "Lunge_Impulse"
    };

    // Color Palette mappings based on Event ID
    [[nodiscard]] ImU32 GetColorForEvent(std::uint32_t eventId) noexcept
    {
        switch (static_cast<CombatEventId>(eventId))
        {
        case CombatEventId::Hitbox_Active:     return IM_COL32(250, 80, 80, 255);   // Red
        case CombatEventId::CancelWindow_Open: return IM_COL32(250, 200, 50, 255);  // Yellow
        case CombatEventId::Lunge_Impulse:     return IM_COL32(50, 150, 250, 255);  // Blue
        case CombatEventId::Play_SFX:
        case CombatEventId::Play_VFX:          return IM_COL32(200, 100, 250, 255); // Purple
        default:                               return IM_COL32(100, 200, 100, 255); // Green
        }
    }
}

void AnimationTimelinePanel::SetTarget(AnimationComponent* target, std::size_t stateIndex) noexcept
{
    m_targetComponent = target;
    m_selectedStateIndex = stateIndex;
    m_selectedEventIndex = -1;
}

void AnimationTimelinePanel::Draw(bool* pOpen) noexcept
{
    if (!ImGui::Begin("AnimEvent Timeline", pOpen, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        ImGui::End();
        return;
    }

    if (!m_targetComponent)
    {
        ImGui::TextDisabled("No Animation Component Selected.");
        ImGui::Text("Click 'Open Timeline Sequencer' in the Inspector to bind an entity.");
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

    // Live Runtime Synchronization
    const bool isGameLive = EditorManager::Instance().GetEditorMode() != EditorMode::Edit;
    if (isGameLive)
    {
        const std::size_t runtimeState = m_targetComponent->GetCurrentStateIndex();
        if (runtimeState < states.size() && runtimeState != m_selectedStateIndex)
        {
            m_selectedStateIndex = runtimeState;
            m_selectedEventIndex = -1; // Reset selection so we don't go out of bounds
        }
    }

    // Top Controls Toolbar
    // Disable dropdown during gameplay so the user can't fight the live visualizer
    ImGui::BeginDisabled(isGameLive);
    if (ImGui::BeginCombo("Active State", states[m_selectedStateIndex].name.c_str()))
    {
        for (std::size_t i{ 0 }; i < states.size(); ++i)
        {
            const bool isSelected{ m_selectedStateIndex == i };
            if (ImGui::Selectable(states[i].name.c_str(), isSelected))
            {
                m_selectedStateIndex = i;
                m_selectedEventIndex = -1;
            }
            if (isSelected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();

    AnimationState& state{ states[m_selectedStateIndex] };
    const auto model{ m_targetComponent->GetModel() };
    float duration{ 1.0f };

    if (model && state.clipIndex >= 0 && static_cast<std::size_t>(state.clipIndex) < model->GetAnimations().size())
    {
        duration = model->GetAnimations()[state.clipIndex].secondsLength / state.speedMultiplier;
    }

    ImGui::SameLine();
    if (ImGui::Button("+ Add Event"))
    {
        state.events.push_back(AnimationEvent{ 0.0f, 0, 0.0f, false, 0.1f });
        m_selectedEventIndex = static_cast<int>(state.events.size() - 1);
    }

    ImGui::SameLine();
    if (ImGui::Button("Remove Selected") && m_selectedEventIndex >= 0 && static_cast<std::size_t>(m_selectedEventIndex) < state.events.size())
    {
        state.events.erase(state.events.begin() + m_selectedEventIndex);
        m_selectedEventIndex = -1;
    }

    ImGui::Separator();

    // CANVAS COORDINATE MAPPING
    ImDrawList* drawList{ ImGui::GetWindowDrawList() };
    const ImVec2 canvasPos{ ImGui::GetCursorScreenPos() };
    const ImVec2 canvasSize{ ImGui::GetContentRegionAvail() };

    if (canvasSize.y < 50.0f || duration <= 0.0f)
    {
        ImGui::End();
        return;
    }

    constexpr float headerHeight{ 25.0f };
    constexpr float trackHeight{ 30.0f };
    const float trackStartY{ canvasPos.y + headerHeight };

    // Reserve scrollable space using a Dummy block so ImGui handles clipping naturally
    const float requiredHeight{ headerHeight + (state.events.size() * trackHeight) + 20.0f };
    ImGui::Dummy(ImVec2(canvasSize.x, requiredHeight));

    // Draw Workspace Background
    drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + requiredHeight), IM_COL32(20, 20, 24, 255));
    drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + headerHeight), IM_COL32(40, 40, 45, 255));

    // Draw Ruler & Frame Ticks (Assuming standard 60fps evaluation for display math)
    const int totalFrames{ static_cast<int>(duration * 60.0f) };
    for (int i{ 0 }; i <= totalFrames; ++i)
    {
        const float t{ static_cast<float>(i) / 60.0f };
        const float normT{ t / duration };
        const float xPixel{ canvasPos.x + (normT * canvasSize.x) };

        if (i % 10 == 0) // Major Tick (Frames + Time overlay)
        {
            drawList->AddLine(ImVec2(xPixel, canvasPos.y + headerHeight - 10.0f), ImVec2(xPixel, canvasPos.y + headerHeight), IM_COL32(200, 200, 200, 255));
            char labelBuf[32];
            snprintf(labelBuf, sizeof(labelBuf), "%df", i);
            drawList->AddText(ImVec2(xPixel + 2.0f, canvasPos.y + 2.0f), IM_COL32(150, 150, 150, 255), labelBuf);
        }
        else if (i % 5 == 0) // Medium Tick
        {
            drawList->AddLine(ImVec2(xPixel, canvasPos.y + headerHeight - 6.0f), ImVec2(xPixel, canvasPos.y + headerHeight), IM_COL32(150, 150, 150, 255));
        }
        else // Minor Tick
        {
            drawList->AddLine(ImVec2(xPixel, canvasPos.y + headerHeight - 3.0f), ImVec2(xPixel, canvasPos.y + headerHeight), IM_COL32(100, 100, 100, 255));
        }
    }

    // Draw Track Lanes & Events
    for (std::size_t i{ 0 }; i < state.events.size(); ++i)
    {
        auto& ev{ state.events[i] };
        const float yTop{ trackStartY + (i * trackHeight) };
        const float yBot{ yTop + trackHeight - 2.0f };

        // Background Track Row
        drawList->AddRectFilled(ImVec2(canvasPos.x, yTop), ImVec2(canvasPos.x + canvasSize.x, yBot), IM_COL32(30, 30, 35, 255));

        const float xStartPixel{ canvasPos.x + (ev.normalizedTime * canvasSize.x) };
        const ImU32 baseColor{ GetColorForEvent(ev.eventId) };
        const bool isSelected{ m_selectedEventIndex == static_cast<int>(i) };

        // Alpha fade out unselected tracks so the active one pops
        const ImU32 renderColor{ isSelected ? baseColor :
            ImGui::ColorConvertFloat4ToU32(ImVec4(
                ImGui::ColorConvertU32ToFloat4(baseColor).x,
                ImGui::ColorConvertU32ToFloat4(baseColor).y,
                ImGui::ColorConvertU32ToFloat4(baseColor).z, 0.5f))
        };

        if (ev.isRange)
        {
            // Range Representation (Colored Block)
            const float xEndPixel{ canvasPos.x + (ev.normalizedEndTime * canvasSize.x) };
            const ImRect rectBox{ ImVec2(xStartPixel, yTop + 2.0f), ImVec2(xEndPixel, yBot - 2.0f) };

            // Invisible button captures the mouse hit-test exactly over the drawn block
            ImGui::SetCursorScreenPos(rectBox.Min);
            ImGui::InvisibleButton((std::string("##Range") + std::to_string(i)).c_str(), rectBox.GetSize());
            if (ImGui::IsItemClicked()) m_selectedEventIndex = static_cast<int>(i);

            drawList->AddRectFilled(rectBox.Min, rectBox.Max, renderColor);
            if (isSelected) drawList->AddRect(rectBox.Min, rectBox.Max, IM_COL32(255, 255, 255, 255), 0.0f, 0, 2.0f);

            // Time Dragging Logic
            if (isSelected && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                const float deltaNorm{ ImGui::GetIO().MouseDelta.x / canvasSize.x };
                ev.normalizedTime = std::clamp(ev.normalizedTime + deltaNorm, 0.0f, 1.0f);
                ev.normalizedEndTime = std::clamp(ev.normalizedEndTime + deltaNorm, 0.0f, 1.0f);
            }
        }
        else
        {
            // Instant Trigger Representation (Diamond)
            const ImVec2 p1{ xStartPixel, yTop + 4.0f };
            const ImVec2 p2{ xStartPixel + 8.0f, (yTop + yBot) * 0.5f };
            const ImVec2 p3{ xStartPixel, yBot - 4.0f };
            const ImVec2 p4{ xStartPixel - 8.0f, (yTop + yBot) * 0.5f };

            ImGui::SetCursorScreenPos(ImVec2(xStartPixel - 8.0f, yTop + 4.0f));
            ImGui::InvisibleButton((std::string("##Point") + std::to_string(i)).c_str(), ImVec2(16.0f, trackHeight - 8.0f));
            if (ImGui::IsItemClicked()) m_selectedEventIndex = static_cast<int>(i);

            drawList->AddQuadFilled(p1, p2, p3, p4, renderColor);
            if (isSelected) drawList->AddQuad(p1, p2, p3, p4, IM_COL32(255, 255, 255, 255), 2.0f);

            // Time Dragging Logic
            if (isSelected && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                const float deltaNorm{ ImGui::GetIO().MouseDelta.x / canvasSize.x };
                ev.normalizedTime = std::clamp(ev.normalizedTime + deltaNorm, 0.0f, 1.0f);
            }
        }

        // Draw track identity label
        drawList->AddText(ImVec2(canvasPos.x + 10.0f, yTop + 6.0f), IM_COL32(200, 200, 200, 255), s_eventNames[ev.eventId]);
    }

    // Draw Playhead & Handle Scrubbing Interaction
    const float currentNorm{ std::clamp(m_targetComponent->GetCurrentTimer() / duration, 0.0f, 1.0f) };
    const float playheadPixel{ canvasPos.x + (currentNorm * canvasSize.x) };

    drawList->AddLine(ImVec2(playheadPixel, canvasPos.y), ImVec2(playheadPixel, canvasPos.y + requiredHeight), IM_COL32(250, 50, 50, 255), 2.0f);
    drawList->AddTriangleFilled(
        ImVec2(playheadPixel - 6.0f, canvasPos.y),
        ImVec2(playheadPixel + 6.0f, canvasPos.y),
        ImVec2(playheadPixel, canvasPos.y + 10.0f),
        IM_COL32(250, 50, 50, 255)
    );

    // Creates an invisible hit-test plane covering the entire timeline for manual scrubbing
    ImGui::SetCursorScreenPos(canvasPos);
    ImGui::InvisibleButton("##ScrubPlane", ImVec2(canvasSize.x, requiredHeight));

    // Only allow manual pose hijacking if we are actively authoring in Edit Mode
    if (!isGameLive && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        const float mouseLocalX{ ImGui::GetIO().MousePos.x - canvasPos.x };
        const float newNorm{ std::clamp(mouseLocalX / canvasSize.x, 0.0f, 1.0f) };

        // Tell the engine exactly which state we are trying to scrub
        m_targetComponent->ScrubToTime(m_selectedStateIndex, newNorm * duration);
    }

    // Contextual Float Panel for the Selected Event
    if (m_selectedEventIndex >= 0 && static_cast<std::size_t>(m_selectedEventIndex) < state.events.size())
    {
        ImGui::SetCursorScreenPos(ImVec2(canvasPos.x + canvasSize.x - 300.0f, canvasPos.y + headerHeight + 10.0f));
        ImGui::BeginChild("EventPropertiesPane", ImVec2(290.0f, 180.0f), true, ImGuiWindowFlags_NoScrollbar);

        auto& ev{ state.events[m_selectedEventIndex] };
        ImGui::TextDisabled("EVENT PROPERTIES");
        ImGui::Separator();

        int currentEventId{ static_cast<int>(ev.eventId) };
        if (ImGui::Combo("Type", &currentEventId, s_eventNames, static_cast<int>(std::size(s_eventNames))))
        {
            ev.eventId = static_cast<std::uint32_t>(currentEventId);
        }

        ImGui::Checkbox("Display as Range Window", &ev.isRange);
        ImGui::SliderFloat("Start Time (Norm)", &ev.normalizedTime, 0.0f, 1.0f);
        if (ev.isRange)
        {
            // Constrain end time to never cross backwards over the start time
            ImGui::SliderFloat("End Time (Norm)", &ev.normalizedEndTime, ev.normalizedTime, 1.0f);
        }

        if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Lunge_Impulse))
        {
            ImGui::DragFloat("Lunge Force", &ev.payload, 0.5f, -200.0f, 200.0f);
        }
        else if (ev.eventId == static_cast<std::uint32_t>(CombatEventId::Play_SFX) || ev.eventId == static_cast<std::uint32_t>(CombatEventId::Play_VFX))
        {
            ImGui::DragFloat("Asset ID", &ev.payload, 1.0f, 0.0f, 100.0f);
        }

        ImGui::EndChild();
    }

    ImGui::End();
}