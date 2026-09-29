#include <cmath>
#include <imgui.h>
#include "System/Input.h"
#include "AnimationComponent.h"
#include "CharacterMovementComponent.h"
#include "ComponentRegistry.h"
#include "EditorManager.h"
#include "GameObject.h"
#include "OrbitCameraDriverComponent.h"
#include "PlayerControllerComponent.h"
#include "PlayerStates.h"

PlayerControllerComponent::PlayerControllerComponent() noexcept
    : m_stateMachine{ std::make_unique<StateMachine>() }
{
    // Allocate the entire moveset pool exactly once (Zero runtime allocations)
    m_states[static_cast<std::size_t>(PlayerStateType::Locomotion)] = std::make_unique<PlayerLocomotion>();
    m_states[static_cast<std::size_t>(PlayerStateType::Stop)] = std::make_unique<PlayerStop>();
    m_states[static_cast<std::size_t>(PlayerStateType::PivotTurn)] = std::make_unique<PlayerPivotTurn>();
    m_states[static_cast<std::size_t>(PlayerStateType::Slide)] = std::make_unique<PlayerSlide>();
    m_states[static_cast<std::size_t>(PlayerStateType::AirTraversal)] = std::make_unique<PlayerAirTraversal>();
    m_states[static_cast<std::size_t>(PlayerStateType::Landing)] = std::make_unique<PlayerLanding>();
    m_states[static_cast<std::size_t>(PlayerStateType::ParkourWall)] = std::make_unique<PlayerParkourWall>();
    m_states[static_cast<std::size_t>(PlayerStateType::DashEvade)] = std::make_unique<PlayerDashEvade>();
    m_states[static_cast<std::size_t>(PlayerStateType::AttackPrimary)] = std::make_unique<PlayerAttackPrimary>();
    m_states[static_cast<std::size_t>(PlayerStateType::AttackContextual)] = std::make_unique<PlayerAttackContextual>();
    m_states[static_cast<std::size_t>(PlayerStateType::AttackDirectional)] = std::make_unique<PlayerAttackDirectional>();
    m_states[static_cast<std::size_t>(PlayerStateType::AttackCharged)] = std::make_unique<PlayerAttackCharged>();
    m_states[static_cast<std::size_t>(PlayerStateType::AttackAerial)] = std::make_unique<PlayerAttackAerial>();
    m_states[static_cast<std::size_t>(PlayerStateType::ParryCounter)] = std::make_unique<PlayerParryCounter>();
    m_states[static_cast<std::size_t>(PlayerStateType::HitReact)] = std::make_unique<PlayerHitReact>();
}

PlayerControllerComponent::~PlayerControllerComponent() = default;

void PlayerControllerComponent::OnAttach(GameObject* owner) noexcept
{
    IComponent::OnAttach(owner);

    if (m_owner)
    {
        m_movement = m_owner->GetComponent<CharacterMovementComponent>();
        m_animation = m_owner->GetComponent<AnimationComponent>();

        if (m_animation)
        {
            m_animation->SetBlackboard(&m_blackboard);
        }

        if (m_stateMachine)
        {
            m_stateMachine->Initialize(GetState(PlayerStateType::Locomotion), this);
        }
    }
}

void PlayerControllerComponent::GatherHardwareInput(const float dt) noexcept
{
    if (!m_inputEnabled)
    {
        m_intent = InputIntent{};
        m_shiftHoldTimer = 0.0f;
        m_wasShiftPressed = false;
        return;
    }

    auto& input{ Input::Instance() };
    const GamePad& pad{ input.GetGamePad() };

    float targetX{ pad.GetAxisLX() };
    float targetZ{ pad.GetAxisLY() };

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

    // KEYBOARD SHIFT: TAP-TO-DASH VS HOLD-TO-SPRINT 
    // Threshold 200ms for discriminates between an intentional tap and a sustained hold
    constexpr float s_shiftHoldThreshold{ 0.20f };

    const bool isShiftDown{ input.GetKeyboard().IsPress(VK_SHIFT) };
    bool isShiftTapDash{ false };
    bool isShiftHoldingSprint{ false };

    if (isShiftDown)
    {
        m_shiftHoldTimer += dt;
        if (m_shiftHoldTimer >= s_shiftHoldThreshold)
        {
            // The key has been held long enough: activate sprinting
            isShiftHoldingSprint = true;
        }
    }
    else
    {
        // Key is not pressed this frame. If it was pressed last frame, the player just released it.
        if (m_wasShiftPressed)
        {
            // Only fire the dash if the key was released before reaching the sprint hold threshold
            if (m_shiftHoldTimer > 0.0f && m_shiftHoldTimer < s_shiftHoldThreshold)
            {
                isShiftTapDash = true;
            }
        }
        m_shiftHoldTimer = 0.0f;
    }
    m_wasShiftPressed = isShiftDown;

    // --- INPUT INTENT ASSIGNMENTS ---

    // Dash / Evade:
    // Keyboard: Quick tap-release on Shift (< 200ms)
    // Gamepad: Single press on B button 
    m_intent.bDashTriggered = isShiftTapDash ||
        ((pad.GetButtonDown() & GamePad::BTN_B) != 0);

    // Sprint:
    // Keyboard: Sustained hold on Shift (>= 200ms)
    // Gamepad: Sustained hold on Right Bumper / RB
    m_intent.bSprintHeld = isShiftHoldingSprint ||
        ((pad.GetButton() & GamePad::BTN_RIGHT_SHOULDER) != 0);

    // Attack: Left Mouse Button or Gamepad X
    m_intent.bAttackPressed = input.GetKeyboard().IsTriggered(VK_LBUTTON) ||
        ((pad.GetButtonDown() & GamePad::BTN_X) != 0);

    // Jump: Spacebar or Gamepad A
    m_intent.bJumpTriggered = input.GetKeyboard().IsTriggered(VK_SPACE) ||
        ((pad.GetButtonDown() & GamePad::BTN_A) != 0);
}

void PlayerControllerComponent::ResolveIntentToWorldSpace() noexcept
{
    const float yaw{ OrbitCameraDriverComponent::GetActiveYawRadians() };
    const float sinYaw{ std::sin(yaw) };
    const float cosYaw{ std::cos(yaw) };

    const float inputX{ m_intent.moveVector.x };
    const float inputY{ m_intent.moveVector.y };

    // Projects input-space strafe/forward onto camera right/front:
    // right = (cos y, 0, -sin y), front = (sin y, 0, cos y) — the exact same vectors
    // Camera::UpdateViewMatrix derives, so "forward" always matches the camera's own
    // idea of forward, not an inverted or offset one.
    m_intent.worldMoveDirection = {
        (inputX * cosYaw) + (inputY * sinYaw),
        (-inputX * sinYaw) + (inputY * cosYaw)
    };
}

void PlayerControllerComponent::Update(const float dt)
{
    if (EditorManager::Instance().GetEditorMode() != EditorMode::Play)
    {
        return;
    }

    GatherHardwareInput(dt);

    // Input Masking
    // If the cursor is released (Shift+F1) and interacting with ImGui (Gizmos, Maximize, etc.),
    // we zero out the hardware intent so the character stops moving and ignores attack clicks.
    if (!OrbitCameraDriverComponent::IsMouseCaptured())
    {
        m_intent.moveVector = { 0.0f, 0.0f };
        m_intent.bAttackPressed = false;
        m_intent.bDashTriggered = false;
        m_intent.bJumpTriggered = false;
        m_intent.bSprintHeld = false;
        m_shiftHoldTimer = 0.0f;   
        m_wasShiftPressed = false; 
    }

    ResolveIntentToWorldSpace();

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