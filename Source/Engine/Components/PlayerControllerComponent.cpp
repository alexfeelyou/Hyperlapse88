#include <cmath>
#include <imgui.h>
#include "System/Input.h"
#include "System/TimeManager.h"
#include "AnimationComponent.h"
#include "CapsuleColliderComponent.h"
#include "CharacterMovementComponent.h"
#include "CombatMath.h"
#include "ComponentRegistry.h"
#include "EditorManager.h"
#include "GameObject.h"
#include "HealthComponent.h"
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
    m_states[static_cast<std::size_t>(PlayerStateType::AttackPlunge)] = std::make_unique<PlayerAttackPlunge>();
    m_states[static_cast<std::size_t>(PlayerStateType::ParryCounter)] = std::make_unique<PlayerParryCounter>();
    m_states[static_cast<std::size_t>(PlayerStateType::HitReact)] = std::make_unique<PlayerHitReact>();
    m_states[static_cast<std::size_t>(PlayerStateType::SkillBuff)] = std::make_unique<PlayerSkillBuff>();
    m_states[static_cast<std::size_t>(PlayerStateType::AttackSpeed)] = std::make_unique<PlayerAttackSpeed>();
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
            // PREVENTIVE BUG: Strip stale Editor variables to guarantee a clean Holster startup
            m_blackboard.setFlag(Engine::Animation::AnimFlag::is_combat_active, false);
            m_blackboard.actionIndex = 0;
            m_stateMachine->Initialize(GetState(PlayerStateType::Locomotion), this);
        }
    }
}

void PlayerControllerComponent::GatherHardwareInput(const float dt) noexcept
{
    // Global Buff Decay
    if (m_blackboard.skillBuffTimer > 0.0f)
    {
        m_blackboard.skillBuffTimer -= dt;
        if (m_blackboard.skillBuffTimer <= 0.0f)
        {
            m_blackboard.setFlag(Engine::Animation::AnimFlag::is_speed_buff_active, false);
        }
    }

    if (!m_inputEnabled)
    {
        m_intent = InputIntent{};
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

    // Snapshot last frame's raw input BEFORE overwriting it, so we can judge where an "up" press came from
    const DirectX::XMFLOAT2 prevMove{ m_intent.moveVector };
    m_intent.moveVector = { targetX, targetZ };

    // Maintain Temporal Input Freshness for Directional Command Normals (Up Attack).
    // forwardIntentTimer = how long the current "up" run has been held, and EvaluateCommandNormal only
    // accepts node 0 while it is <= 0.15s. Seeding it with a stale value permanently disqualifies that run,
    // so node 0 fires only when "up" is pressed from a neutral stick/keyboard and stays straight up.
    constexpr float s_upThreshold{ 0.4f };           // Must match the up threshold in EvaluateCommandNormal 
    constexpr float s_neutralAxisThreshold{ 0.35f }; // Lateral and backward deflection threshold for neutral origin
    constexpr float s_lateralTaintThreshold{ 0.70f }; // Sideways push beyond ~45 deg off vertical spoils the run
    constexpr float s_staleForwardIntent{ 1.0f };    // Any value above the 0.25s freshness window disqualifies the run

    if (targetZ > s_upThreshold)
    {
        // Rising edge: up was not held last frame.
        // A flick beginning from neutral must have minimal lateral deflection and not originate from a backward pull.
        // prevMove.y is checked against the negative threshold to avoid penalizing analog stick travel passing between 0.0 and 0.4.
        if (prevMove.y <= s_upThreshold)
        {
            const bool cameFromNeutral{ (std::abs(prevMove.x) < s_neutralAxisThreshold) && (prevMove.y >= -s_neutralAxisThreshold) };
            m_intent.forwardIntentTimer = cameFromNeutral ? 0.0f : s_staleForwardIntent;
        }

        // Holding A/D (or pushing the stick sideways beyond 45 deg) at any point during the run spoils it as well
        if ((std::abs(targetX) > s_lateralTaintThreshold) && (m_intent.forwardIntentTimer < s_staleForwardIntent))
        {
            m_intent.forwardIntentTimer = s_staleForwardIntent;
        }

        m_intent.forwardIntentTimer += dt;
    }
    else
    {
        // Run ended: the next up press is evaluated from scratch
        m_intent.forwardIntentTimer = 0.0f;
    }

    // INPUT INTENT ASSIGNMENTS

    // Contextual Parry / Dash Trigger:
    // Keyboard: F | Gamepad: B
    // States evaluate TryStartParry first when grounded; if no parry window is active, execution falls through to DashEvade.
    const bool isParryOrDashTriggered{ input.GetKeyboard().IsTriggered('F') ||
        ((pad.GetButtonDown() & GamePad::BTN_B) != 0) };

    m_intent.bParryPressed = isParryOrDashTriggered;
    m_intent.bDashTriggered = isParryOrDashTriggered;

    // Slide:
    // Keyboard: Sustained hold on Left Control
    // Gamepad: Sustained hold on Left Bumper / LB
    m_intent.bSlideHeld = input.GetKeyboard().IsPress(VK_CONTROL) ||
        ((pad.GetButton() & GamePad::BTN_LEFT_SHOULDER) != 0);

    m_intent.bSlideTriggered = input.GetKeyboard().IsTriggered(VK_CONTROL) ||
        ((pad.GetButtonDown() & GamePad::BTN_LEFT_SHOULDER) != 0);

    // Sprint:
    // Keyboard: Sustained hold on Shift
    // Gamepad: Sustained hold on Right Bumper / RB
    m_intent.bSprintHeld = input.GetKeyboard().IsPress(VK_SHIFT) ||
        ((pad.GetButton() & GamePad::BTN_RIGHT_SHOULDER) != 0);

    // Light Attack: Left Mouse Button or Gamepad X
    m_intent.bAttackPressed = input.GetKeyboard().IsTriggered(VK_LBUTTON) ||
        ((pad.GetButtonDown() & GamePad::BTN_X) != 0);
    m_intent.bAttackHeld = input.GetKeyboard().IsPress(VK_LBUTTON) ||
        ((pad.GetButton() & GamePad::BTN_X) != 0);

    // Heavy/Charged Attack: Right Mouse Button or Gamepad Y
    m_intent.bHeavyAttackPressed = input.GetKeyboard().IsTriggered(VK_RBUTTON) ||
        ((pad.GetButtonDown() & GamePad::BTN_Y) != 0);
    m_intent.bHeavyAttackHeld = input.GetKeyboard().IsPress(VK_RBUTTON) ||
        ((pad.GetButton() & GamePad::BTN_Y) != 0);

    // Jump: Spacebar or Gamepad A
    m_intent.bJumpTriggered = input.GetKeyboard().IsTriggered(VK_SPACE) ||
        ((pad.GetButtonDown() & GamePad::BTN_A) != 0);

    // Buff Activation: Keyboard Q or Gamepad RT (Right Trigger)
    m_intent.bSkillTriggered = input.GetKeyboard().IsTriggered('Q') ||
        ((pad.GetButtonDown() & GamePad::BTN_RIGHT_TRIGGER) != 0);

    // Sustain Combat Stance on Aggressive Actions (Parry sets combat stance only when a valid parry window is claimed)
    if (m_intent.bAttackPressed || m_intent.bAttackHeld || m_intent.bHeavyAttackPressed || m_intent.bHeavyAttackHeld || m_intent.bSkillTriggered)
    {
        m_blackboard.setFlag(Engine::Animation::AnimFlag::is_combat_active, true);
    }
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
    const bool isPlayMode = EditorManager::Instance().GetEditorMode() == EditorMode::Play;
    static bool s_wasPlayMode = false;

    // PREVENTIVE BUG: Live memory isn't destroyed when entering Play mode in this engine.
    // We must intercept the transition frame to manually strip stale Editor variables 
    // and reset the state machine so the player always starts cleanly in Holster/Locomotion.
    if (isPlayMode && !s_wasPlayMode)
    {
        m_blackboard.setFlag(Engine::Animation::AnimFlag::is_combat_active, false);
        m_blackboard.actionIndex = 0;
        m_lastHitReaction = PlayerHitReactionData{};
        SetInvincible(false);
        SetInputEnabled(true);
        if (m_owner)
        {
            if (auto* capsule{ m_owner->GetComponent<CapsuleColliderComponent>() })
            {
                capsule->SetCollisionLayer(CollisionLayer::Player, CollisionLayer::Mask::Player);
            }
        }
        if (m_stateMachine) m_stateMachine->ChangeState(this, GetState(PlayerStateType::Locomotion));
    }
    s_wasPlayMode = isPlayMode;

    if (!isPlayMode)
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
        m_intent.bAttackHeld = false;
        m_intent.bHeavyAttackPressed = false;
        m_intent.bHeavyAttackHeld = false;
        m_intent.bDashTriggered = false;
        m_intent.bJumpTriggered = false;
        m_intent.bSprintHeld = false;
        m_intent.bParryPressed = false;
    }

    ResolveIntentToWorldSpace();

    if (m_stateMachine)
    {
        m_stateMachine->Update(this, dt);
    }
}

void PlayerControllerComponent::SetInvincible(const bool invincible) noexcept
{
    m_isInvincible = invincible;
    if (m_owner)
    {
        if (auto* health{ m_owner->GetComponent<HealthComponent>() })
        {
            health->SetInvincible(invincible);
        }
    }
}

void PlayerControllerComponent::NotifyHitReaction(
    const DirectX::XMFLOAT3& attackerPos,
    const DirectX::XMFLOAT3& slashDir,
    const bool isHeavyKnockdown) noexcept
{
    if (!m_owner || !m_stateMachine || m_isInvincible)
    {
        return;
    }

    const auto* health{ m_owner->GetComponent<HealthComponent>() };
    const bool isLethal{ health && health->IsDead() };

    // PlayerHitReact::Enter routes to Dead, AirFlinch, Knockdown, or 4-way GroundedFlinch
    m_lastHitReaction.attackerPos = attackerPos;
    m_lastHitReaction.directionIndex = Engine::Combat::CalculateHitDirectionIndex(m_owner, attackerPos, slashDir);
    m_lastHitReaction.isHeavyKnockdown = isHeavyKnockdown;
    m_lastHitReaction.isLethal = isLethal;

    // Route through ChangeState so Exit() runs on the interrupted state and Enter() initializes/re-triggers HitReact
    m_stateMachine->ChangeState(this, GetState(PlayerStateType::HitReact));
}

bool PlayerControllerComponent::TryConsumeParry(
    const DirectX::XMFLOAT3& attackerPos,
    const DirectX::XMFLOAT3& /*slashDir*/) noexcept
{
    if (!m_owner || !m_stateMachine)
    {
        return false;
    }

    auto* parryState{ static_cast<PlayerParryCounter*>(GetState(PlayerStateType::ParryCounter)) };
    if (!parryState || m_stateMachine->GetCurrentState() != parryState)
    {
        return false;
    }

    if (!parryState->IsParryWindowActive())
    {
        return false;
    }

    parryState->NotifyParrySuccess(this, attackerPos);

    if (m_parrySlowMo.enabled)
    {
        // Engage slow-motion immediately with zero hit-stop delay so the enemy stagger reads clearly
        Engine::Time::TimeManager::Instance().TriggerSlowMotion(
            m_parrySlowMo.timeScale,
            m_parrySlowMo.holdDuration,
            m_parrySlowMo.recoveryDuration
        );
    }

    return true;
}

void PlayerControllerComponent::DrawInspector()
{
    ImGui::TextDisabled("Player Input & State Orchestrator");
    ImGui::Separator();
    ImGui::Checkbox("Input Enabled", &m_inputEnabled);

    ImGui::Spacing();
    ImGui::TextDisabled("PARRY TIME DILATION");
    ImGui::Checkbox("Enable Parry Slow-Mo", &m_parrySlowMo.enabled);

    ImGui::BeginDisabled(!m_parrySlowMo.enabled);
    ImGui::SliderFloat("Slow-Mo Scale", &m_parrySlowMo.timeScale, 0.05f, 1.0f, "%.2fx");
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Target world playback speed during parry slow-motion (0.22x = 22%% speed)");
    }

    ImGui::DragFloat("Hold Duration", &m_parrySlowMo.holdDuration, 0.01f, 0.0f, 1.5f, "%.2f s");
    ImGui::DragFloat("Ease-Out Recovery", &m_parrySlowMo.recoveryDuration, 0.01f, 0.0f, 1.5f, "%.2f s");

    const bool isPlayMode{ EditorManager::Instance().GetEditorMode() == EditorMode::Play };
    ImGui::BeginDisabled(!isPlayMode);
    if (ImGui::Button("Test Parry Slow-Mo (Play Mode)", ImVec2{ -1.0f, 24.0f }))
    {
        Engine::Time::TimeManager::Instance().TriggerSlowMotion(
            m_parrySlowMo.timeScale,
            m_parrySlowMo.holdDuration,
            m_parrySlowMo.recoveryDuration
        );
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
}

void PlayerControllerComponent::Serialize(nlohmann::json& outJson) const
{
    outJson["ParrySlowMoEnabled"] = m_parrySlowMo.enabled;
    outJson["ParrySlowMoScale"] = m_parrySlowMo.timeScale;
    outJson["ParrySlowMoHoldDuration"] = m_parrySlowMo.holdDuration;
    outJson["ParrySlowMoRecoveryDuration"] = m_parrySlowMo.recoveryDuration;
}

void PlayerControllerComponent::Deserialize(const nlohmann::json& inJson)
{
    m_parrySlowMo.enabled = inJson.value("ParrySlowMoEnabled", true);
    m_parrySlowMo.timeScale = std::clamp(inJson.value("ParrySlowMoScale", 0.22f), 0.05f, 1.0f);
    m_parrySlowMo.holdDuration = (std::max)(0.0f, inJson.value("ParrySlowMoHoldDuration", 0.18f));
    m_parrySlowMo.recoveryDuration = (std::max)(0.0f, inJson.value("ParrySlowMoRecoveryDuration", 0.15f));
}

REGISTER_COMPONENT(PlayerControllerComponent)