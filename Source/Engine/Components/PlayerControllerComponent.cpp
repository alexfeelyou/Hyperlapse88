#include <cmath>
#include <imgui.h>
#include "System/Input.h"
#include "AnimationComponent.h"
#include "CharacterMovementComponent.h"
#include "ComponentRegistry.h"
#include "EditorManager.h"
#include "GameObject.h"
#include "PlayerControllerComponent.h"
#include "PlayerStates.h"

PlayerControllerComponent::PlayerControllerComponent() noexcept
    : m_stateMachine{ std::make_unique<StateMachine>() }
{
    // Allocate states exactly once
    m_states[static_cast<std::size_t>(PlayerStateType::Locomotion)] = std::make_unique<PlayerLocomotion>();
    m_states[static_cast<std::size_t>(PlayerStateType::Dash)] = std::make_unique<PlayerDash>();
    m_states[static_cast<std::size_t>(PlayerStateType::Attack)] = std::make_unique<PlayerAttackState>();
    m_states[static_cast<std::size_t>(PlayerStateType::HitReact)] = std::make_unique<PlayerHitReactState>();
}

// Destructor is defaulted in header but must be declared here where StateMachine is fully defined
PlayerControllerComponent::~PlayerControllerComponent() = default;

void PlayerControllerComponent::OnAttach(GameObject* owner) noexcept
{
    IComponent::OnAttach(owner);

    if (m_owner)
    {
        m_movement = m_owner->GetComponent<CharacterMovementComponent>();
        m_animation = m_owner->GetComponent<AnimationComponent>();

        if (m_stateMachine)
        {
            // Boot directly into the unified locomotion state
            m_stateMachine->Initialize(GetState(PlayerStateType::Locomotion), this);
        }
    }
}

void PlayerControllerComponent::GatherHardwareInput() noexcept
{
    // Fast-Fail: If input is disabled, or we are not actively in Play Mode, wipe intent and exit.
    if (!m_inputEnabled || EditorManager::Instance().GetEditorMode() != EditorMode::Play)
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

    // "Walk" is not a separate button state, it is an input restrictor.
    // If Left Alt is held, cap the input vector to ~35% magnitude.
    if (input.GetKeyboard().IsPress(VK_LMENU))
    {
        targetX *= 0.35f;
        targetZ *= 0.35f;
    }

    m_intent.moveVector = { targetX, targetZ };

    // Gather Triggers & Buttons
	// Dash: Left Shift or Gamepad B or Gamepad Left Shoulder
    m_intent.bDashTriggered = input.GetKeyboard().IsTriggered(VK_SHIFT) ||
        ((pad.GetButtonDown() & GamePad::BTN_B) != 0) || 
        ((pad.GetButtonDown() & GamePad::BTN_LEFT_SHOULDER) != 0);

    // Standard Attack: Left Mouse Button or Gamepad X
    m_intent.bAttackPressed = input.GetKeyboard().IsTriggered(VK_LBUTTON) ||
        ((pad.GetButtonDown() & GamePad::BTN_X) != 0);
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