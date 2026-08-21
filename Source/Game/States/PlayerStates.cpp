#include "PlayerStates.h"

namespace {
    [[nodiscard]] bool IsDashInputTriggered() noexcept
    {
        auto& input{ Input::Instance() };

        // Keyboard Check
        const bool isKeyboardDash{ input.GetKeyboard().IsTriggered(VK_SHIFT) };

        // Gamepad Check (LB = Left Shoulder)
        // Use GetButtonDown() so it only triggers exactly on the frame it is pressed
        const bool isGamepadDash{ (input.GetGamePad().GetButtonDown() & GamePad::BTN_LEFT_SHOULDER) != 0 };

        return isKeyboardDash || isGamepadDash;
    }

    [[nodiscard]] bool IsShootInputPressed() noexcept
    {
        auto& input{ Input::Instance() };

        // Keyboard/Mouse Check (Left Click)
        const bool isMouseShoot{ input.GetKeyboard().IsPress(VK_LBUTTON) };

        // Gamepad Check (RT = Right Trigger) 
        // Triggers are analog (0.0f to 1.0f). If we check > 0.0f, resting a finger will fire the gun.
        // We use a 50% deadzone threshold so it acts like a confident, digital button press.
        constexpr float triggerThreshold{ 0.5f };
        const bool isGamepadShoot{ input.GetGamePad().GetTriggerR() > triggerThreshold };

        return isMouseShoot || isGamepadShoot;
    }

	// Common combat action logic for PlayerShoot, PlayerSlash, and PlayerParry states

    [[nodiscard]] bool TryExecuteCombatAction(Player* player, bool allowShoot = true)
    {
        // Anticipate Nullptr Bug
        if (!player || !player->IsInputEnabled()) return false;

        // Centralized Input Check
        if (!IsShootInputPressed()) return false;

        CollisionManager* const colMgr{ player->GetCollisionManager() };
        if (colMgr)
        {
            const DirectX::XMFLOAT3 pPos{ player->GetMovement()->GetPosition() };
            const DirectX::XMFLOAT3 aimPos{ player->GetAimTarget() };

            const float aimDx{ aimPos.x - pPos.x };
            const float aimDz{ aimPos.z - pPos.z };
            const float aimDistSq{ (aimDx * aimDx) + (aimDz * aimDz) };

            DirectX::XMFLOAT3 aimDir{ 0.0f, 0.0f, 1.0f }; // Fallback forward direction

            if (aimDistSq > 0.0001f)
            {
                const float aimDist{ std::sqrt(aimDistSq) };
                aimDir = { aimDx / aimDist, 0.0f, aimDz / aimDist };
            }

			// Slash Priority: Check for enemies in the slash cone first, then check for bullets to parry
            if (Enemy * slashTarget{ colMgr->GetTargetInSlashCone(pPos, aimDir, 0.8f, 0.85f) })
            {
                player->SetLastValidInput({ aimDir.x, aimDir.z });
                player->GetMovement()->SetRotationY(DirectX::XMConvertToDegrees(std::atan2(aimDir.x, aimDir.z)));
                player->SetAimLocked(true);

                player->GetStateMachine()->ChangeState(player, std::make_unique<PlayerSlash>());

                constexpr int MELEE_DAMAGE{ 30 };
                const bool isKamikaze{ slashTarget->GetAttackType() == AttackType::Tracking };
                const int finalDamage{ isKamikaze ? 9999 : MELEE_DAMAGE };

                slashTarget->TakeDamage(finalDamage);

                return true;
            }

            // Parry Priority 
            Bullet* parryBullet{ nullptr };
            Enemy* parryTarget{ nullptr };
        }

        // Default: Shoot 
        if (allowShoot && !player->GetAnimator()->IsUpperPlaying())
        {
            player->GetStateMachine()->ChangeState(player, std::make_unique<PlayerShoot>());
            player->FireProjectile();
            return true;
        }

        return false;
    }
}

using namespace DirectX;

// Idle State
void PlayerIdle::Enter(Player* player)
{
    player->GetAnimator()->SetPlaybackSpeed(1.0f);
    player->GetAnimator()->Play("Idle", true, PlayerConst::AnimBlendDefault);
}

void PlayerIdle::Update(Player* player, float dt)
{
    if (!player->IsInputEnabled()) return;

    // Dash Priority 
    if (IsDashInputTriggered() && player->canDash)
    {
        player->GetStateMachine()->ChangeState(player, std::make_unique<PlayerDash>());
        return;
    }

    // Combat Priority
    if (TryExecuteCombatAction(player)) return;

    // Movement Fallback
    if (player->IsMoving())
    {
        player->GetStateMachine()->ChangeState(player, std::make_unique<PlayerMoving>());
    }
}

// Moving State
void PlayerMoving::Enter(Player* player)
{
    player->GetAnimator()->Play("RunPistol", true, PlayerConst::AnimBlendDefault);
}

void PlayerMoving::Update(Player* player, float dt)
{
    // Handle Animation direction
    player->GetAnimator()->SetPlaybackSpeed(player->IsBackpedaling() ? -1.0f : 1.0f);

    if (player->IsInputEnabled())
    {
        // Dash Priority
        if (IsDashInputTriggered() && player->canDash)
        {
            player->GetStateMachine()->ChangeState(player, std::make_unique<PlayerDash>());
            return;
        }

        // Combat Priority
        if (TryExecuteCombatAction(player)) return;
    }

    // Idle Fallback
    if (!player->IsMoving())
    {
        player->GetStateMachine()->ChangeState(player, std::make_unique<PlayerIdle>());
    }
}

// Dash State
void PlayerDash::Enter(Player* player)
{
    constexpr float DASH_IFRAME_DURATION = 0.2f;

    timer = player->GetDashDuration();
    dashDir = player->GetLastValidInput();

    player->canDash = false;
    player->dashCooldownTimer = player->GetDashCooldown();
    player->TriggerInvincibility(DASH_IFRAME_DURATION);

	// Dash VFX
    DirectX::XMFLOAT3 pos = player->GetMovement()->GetPosition();
    pos.y += 1.0f; 

    m_dashGoVfxHandle = EffectManager::Instance().Play("Data/Effect/VFX_Player_Dash_Go.efk", pos, 0.2f);

    if (m_dashGoVfxHandle != -1) {
        float yaw = atan2f(dashDir.x, dashDir.y) + DirectX::XM_PI;

        EffectManager::Instance().SetRotation(m_dashGoVfxHandle, { 0.0f, yaw, 0.0f });
    }

    std::string dashSounds[] = {
        "Data/Sound/SE_Dash_01.wav",
        "Data/Sound/SE_Dash_02.wav",
        "Data/Sound/SE_Dash_03.wav"
    };

    int randomIndex = rand() % 3;

    AudioManager::Instance().PlaySFX(dashSounds[randomIndex], 0.1f);
}

void PlayerDash::Update(Player* player, float dt)
{
    timer -= dt;

    player->GetMovement()->SetVelocity({
        dashDir.x * player->GetDashSpeed(),
        0.0f,
        dashDir.y * player->GetDashSpeed()
        });

    if (m_dashGoVfxHandle != -1 && EffectManager::Instance().IsPlaying(m_dashGoVfxHandle))
    {
        DirectX::XMFLOAT3 trackPos = player->GetMovement()->GetPosition();
        trackPos.y += 1.0f;
        EffectManager::Instance().SetPosition(m_dashGoVfxHandle, trackPos);
    }

    if (timer <= 0.0f)
    {
        if (player->IsMoving())
            player->GetStateMachine()->ChangeState(player, std::make_unique<PlayerMoving>());
        else
            player->GetStateMachine()->ChangeState(player, std::make_unique<PlayerIdle>());
    }
}

void PlayerDash::Exit(Player* player)
{
    player->GetMovement()->SetVelocity({ 0.0f, 0.0f, 0.0f });
}

void PlayerSlash::Enter(Player* player)
{
    player->SetActiveWeapon(Player::WeaponType::Sword);
    player->GetAnimator()->PlayUpper("Parry", false);

    // Brace initialization to prevent narrowing conversions
    const float yawRad{ DirectX::XMConvertToRadians(player->GetMovement()->GetRotation().y) };

    player->GetMovement()->SetVelocity({
        std::sin(yawRad) * PlayerConst::SlashLungeForce,
        0.0f,
        std::cos(yawRad) * PlayerConst::SlashLungeForce
        });

    constexpr float SLASH_IFRAME_DURATION{ 0.3f };
    player->TriggerInvincibility(SLASH_IFRAME_DURATION);
}

void PlayerSlash::Update(Player* player, float dt)
{
    timer -= dt;

    // Decelerate lunge over time
    XMFLOAT3 vel = player->GetMovement()->GetVelocity();
    vel.x *= PlayerConst::SlashDrag;
    vel.z *= PlayerConst::SlashDrag;
    player->GetMovement()->SetVelocity(vel);

    if (timer <= 0.0f)
    {
        if (player->IsMoving())
            player->GetStateMachine()->ChangeState(player, std::make_unique<PlayerMoving>());
        else
            player->GetStateMachine()->ChangeState(player, std::make_unique<PlayerIdle>());
    }
}

void PlayerSlash::Exit(Player* player)
{
    player->GetMovement()->SetVelocity({ 0.0f, 0.0f, 0.0f });
}

// Parry State
void PlayerParry::Enter(Player* player)
{
    player->GetMovement()->SetVelocity({ 0.0f, 0.0f, 0.0f });
    player->GetAnimator()->PlayUpper("Parry", false);
    player->SetActiveWeapon(Player::WeaponType::Sword);
}

void PlayerParry::Update(Player* player, float dt)
{
    timer -= dt;

    if (timer <= 0.0f)
    {
        if (player->IsMoving())
            player->GetStateMachine()->ChangeState(player, std::make_unique<PlayerMoving>());
        else
            player->GetStateMachine()->ChangeState(player, std::make_unique<PlayerIdle>());
    }
}

void PlayerParry::Exit(Player* player)
{
}

// Shoot State
void PlayerShoot::Enter(Player* player)
{
    PerformShootInternal(player, true);
}

void PlayerShoot::Update(Player* player, float dt)
{
    // Dash Lockout Prevention
    if (IsDashInputTriggered() && player->canDash)
    {
        player->GetStateMachine()->ChangeState(player, std::make_unique<PlayerDash>());
        return;
    }

    m_timer -= dt;
    m_minTapCooldown -= dt;

    const bool isHolding{ IsShootInputPressed() };

    if (isHolding)
    {
        // allowShoot = false ensures we only check for slashes/parries here
        if (TryExecuteCombatAction(player, false)) return;
    }

    // The Tap-Fire Reward Logic (Early Exit)
    if (!isHolding && m_minTapCooldown <= 0.0f)
    {
        if (player->IsMoving())
            player->GetStateMachine()->ChangeState(player, std::make_unique<PlayerMoving>());
        else
            player->GetStateMachine()->ChangeState(player, std::make_unique<PlayerIdle>());
        return;
    }

    // The Continuous Hold Logic (For Bullets Only)
    if (m_timer <= 0.0f)
    {
        if (isHolding)
        {
            // Lower Body Animation Sync
            if (player->IsMoving()) {
                player->GetAnimator()->SetPlaybackSpeed(player->IsBackpedaling() ? -1.0f : 1.0f);
                if (!player->GetAnimator()->IsPlaying("RunPistol")) {
                    player->GetAnimator()->Play("RunPistol", true, PlayerConst::AnimBlendDefault);
                }
            }
            else {
                player->GetAnimator()->SetPlaybackSpeed(1.0f);
                if (!player->GetAnimator()->IsPlaying("Idle")) {
                    player->GetAnimator()->Play("Idle", true, PlayerConst::AnimBlendDefault);
                }
            }

            player->FireProjectile();
            PerformShootInternal(player, true);
        }
    }
}

void PlayerShoot::PerformShootInternal(Player* player, bool isHeld)
{
    const std::string shootSounds[]{
        "Data/Sound/SE_Player_Shoot_01.wav",
        "Data/Sound/SE_Player_Shoot_02.wav",
        "Data/Sound/SE_Player_Shoot_03.wav"
    };

    const int randomIndex{ rand() % 3 };
    AudioManager::Instance().PlaySFX(shootSounds[randomIndex], 0.1f);

    const float baseDelay{ player->GetShootDelay() };

    m_minTapCooldown = baseDelay;

    if (isHeld)
    {
        constexpr float HOLD_PENALTY_MULTIPLIER{ 1.5f };
        m_timer = baseDelay * HOLD_PENALTY_MULTIPLIER;
    }
    else
    {
        m_timer = baseDelay;
    }
}

void PlayerShoot::Exit(Player* player)
{
}