#include <cmath>
#include <imgui.h>
#include "System/Input.h"
#include "CharacterMovementComponent.h"
#include "ComponentRegistry.h"
#include "GameObject.h"
#include "PlayerControllerComponent.h"
#include "PlayerStates.h"

PlayerControllerComponent::PlayerControllerComponent() noexcept
    : m_stateMachine{ std::make_unique<StateMachine>() }
{}

// Destructor is defaulted in header but must be declared here where StateMachine is fully defined
PlayerControllerComponent::~PlayerControllerComponent() = default;

void PlayerControllerComponent::OnAttach(GameObject* owner) noexcept
{
    IComponent::OnAttach(owner);

    if (m_owner)
    {
        // Wire up the sibling motor
        m_movement = m_owner->GetComponent<CharacterMovementComponent>();

        // Initialize the state machine targeting this controller
        if (m_stateMachine)
        {
            m_stateMachine->Initialize(std::make_unique<PlayerIdle>(), this);
        }
    }
}

void PlayerControllerComponent::GatherHardwareInput() noexcept
{
    // If input is disabled (pause, cutscene, death), wipe the intent clean and return
    if (!m_inputEnabled)
    {
        m_intent = InputIntent{};
        return;
    }

    auto& input{ Input::Instance() };
    const GamePad& pad{ input.GetGamePad() };

    // Gather Movement
    float targetX{ pad.GetAxisLX() };
    float targetZ{ pad.GetAxisLY() };

    // Fallback to keyboard if gamepad is idle
    constexpr float deadzone{ 0.05f };
    if (std::abs(targetX) < deadzone && std::abs(targetZ) < deadzone)
    {
        targetX = 0.0f;
        targetZ = 0.0f;
        if (input.GetKeyboard().IsPress('W')) targetZ += 1.0f;
        if (input.GetKeyboard().IsPress('S')) targetZ -= 1.0f;
        if (input.GetKeyboard().IsPress('D')) targetX += 1.0f;
        if (input.GetKeyboard().IsPress('A')) targetX -= 1.0f;
    }

    m_intent.moveVector = { targetX, targetZ };

    // Gather Triggers & Buttons
    m_intent.bDashTriggered = input.GetKeyboard().IsTriggered(VK_SHIFT) ||
        ((pad.GetButtonDown() & GamePad::BTN_LEFT_SHOULDER) != 0);

    constexpr float triggerThreshold{ 0.5f };
    m_intent.bAttackPressed = input.GetKeyboard().IsPress(VK_LBUTTON) ||
        (pad.GetTriggerR() > triggerThreshold);
}

void PlayerControllerComponent::Update(const float dt)
{
    GatherHardwareInput();

    // Drive the State Machine.
    // The state machine reads GetIntent() and fires commands to GetMovement().
    if (m_stateMachine)
    {
        m_stateMachine->Update(this, dt);
    }
}

void PlayerControllerComponent::DrawInspector()
{
    ImGui::TextDisabled("Player Input & State Orchestrator");
    ImGui::Separator();
    ImGui::Checkbox("Input Enabled", &m_inputEnabled);
}

REGISTER_COMPONENT(PlayerControllerComponent)