#include "PlayerStates.h"
#include "Player.h"
#include "PlayerConstants.h"
#include "StateMachine.h"
#include "AnimationController.h"
#include "System/Input.h"
#include <memory>
#include <cmath>

#include "System/CollisionManager.h"
#include "Enemy.h"
#include "Bullet.h"
#include "Boss.h"
#include "System/AudioManager.h"

#include "EffectManager.h"

namespace {
    [[nodiscard]] bool IsDashInputTriggered() noexcept
    {
        auto& input{ Input::Instance() };

        // Keyboard Check
        const bool isKeyboardDash{ input.GetKeyboard().IsTriggered(VK_SHIFT) };

        // Gamepad Check (LB = Left Shoulder)
        // Use GetButtonDown() so it only triggers exactly on the frame it is pressed.
        const bool isGamepadDash{ (input.GetGamePad().GetButtonDown() & GamePad::BTN_LEFT_SHOULDER) != 0 };

        return isKeyboardDash || isGamepadDash;
    }

    [[nodiscard]] bool IsShootInputPressed() noexcept
    {
        auto& input{ Input::Instance() };

        // Keyboard/Mouse Check (Left Click)
        const bool isMouseShoot{ input.GetKeyboard().IsPress(VK_LBUTTON) };

        // Gamepad Check (RT = Right Trigger)
        // BUG ANTICIPATION: The "Hair Trigger" Bug. 
        // Triggers are analog (0.0f to 1.0f). If we check > 0.0f, resting a finger will fire the gun.
        // We use a 50% deadzone threshold so it acts like a confident, digital button press.
        constexpr float triggerThreshold{ 0.5f };
        const bool isGamepadShoot{ input.GetGamePad().GetTriggerR() > triggerThreshold };

        return isMouseShoot || isGamepadShoot;
    }

    // --- COMBAT ACTION ROUTINE ---

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

            // Anticipate Math Error: Prevent Divide-by-Zero
            if (aimDistSq > 0.0001f)
            {
                const float aimDist{ std::sqrt(aimDistSq) };
                aimDir = { aimDx / aimDist, 0.0f, aimDz / aimDist };
            }

            // --- Slash Priority ---
            if (Enemy * slashTarget{ colMgr->GetTargetInSlashCone(pPos, aimDir, 0.8f, 0.85f) })
            {
                player->SetLastValidInput({ aimDir.x, aimDir.z });
                player->GetMovement()->SetRotationY(DirectX::XMConvertToDegrees(std::atan2(aimDir.x, aimDir.z)));
                player->SetAimLocked(true);

                player->GetStateMachine()->ChangeState(player, std::make_unique<PlayerSlash>());

                // =========================================================
                // THE FIX: EXECUTION SCALING
                // Standard enemies take normal damage. Kamikazes take fatal 
                // damage (9999) to ensure they cannot survive the counter-attack 
                // and revenge-kill the player. Ternary evaluation ensures zero branching overhead.
                // =========================================================
                constexpr int MELEE_DAMAGE{ 30 };
                const bool isKamikaze{ slashTarget->GetAttackType() == AttackType::Tracking };
                const int finalDamage{ isKamikaze ? 9999 : MELEE_DAMAGE };

                slashTarget->TakeDamage(finalDamage);

                return true;
            }

            // --- Parry Priority ---
            Bullet* parryBullet{ nullptr };
            Enemy* parryTarget{ nullptr };

            if (colMgr->GetParryableProjectile(pPos, 2.0f, &parryBullet, &parryTarget))
            {
                const DirectX::XMFLOAT3 bPos{ parryBullet->GetMovement()->GetPosition() };
                const float dx{ bPos.x - pPos.x };
                const float dz{ bPos.z - pPos.z };
                const float dist{ std::sqrt((dx * dx) + (dz * dz)) };

                if (dist > 0.001f)
                {
                    const float dirX{ dx / dist };
                    const float dirZ{ dz / dist };

                    player->SetLastValidInput({ dirX, dirZ });
                    player->GetMovement()->SetRotationY(DirectX::XMConvertToDegrees(std::atan2(dx, dz)));

                    // Aim THROUGH the bullet so we don't snap backward if we overshoot
                    const DirectX::XMFLOAT3 aimThrough{ pPos.x + (dirX * 50.0f), bPos.y, pPos.z + (dirZ * 50.0f) };
                    player->ForceAimTarget(aimThrough);
                    player->SetAimLocked(true);
                }

                DirectX::XMFLOAT3 tPos{ bPos };
                float speed{ 0.0f };

                if (parryTarget) {
                    parryBullet->SetHomingTarget(parryTarget);
                    tPos = parryTarget->GetPosition();
                    speed = DirectX::XMVectorGetX(DirectX::XMVector3Length(DirectX::XMLoadFloat3(&parryBullet->GetVelocity()))) * 2.5f;
                    if (speed < 10.0f) speed = 30.0f;
                }
                else if (colMgr->GetBoss()) {
                    parryBullet->SetHomingTarget(nullptr);
                    tPos = pPos;
                    speed = 80.0f;
                    AudioManager::Instance().PlaySFX("Data/Sound/SE_Boss_Bijuudama_Shoot.wav", 0.2f);
                }

                const float dirX{ tPos.x - bPos.x };
                const float dirZ{ tPos.z - bPos.z };
                const float distDir{ std::sqrt((dirX * dirX) + (dirZ * dirZ)) };

                DirectX::XMVECTOR vDir{ DirectX::XMVectorSet(0, 0, 1, 0) };
                if (distDir > 0.001f) {
                    vDir = DirectX::XMVectorSet(dirX / distDir, 0.0f, dirZ / distDir, 0.0f);
                }

                DirectX::XMFLOAT3 newVel;
                DirectX::XMStoreFloat3(&newVel, DirectX::XMVectorScale(vDir, speed));
                parryBullet->ApplyMovement(bPos, newVel);

                player->GetStateMachine()->ChangeState(player, std::make_unique<PlayerParry>());
                return true;
            }
        }

        // --- Default: Shoot ---
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

// ============================================================
// IDLE
// ============================================================
void PlayerIdle::Enter(Player* player)
{
    player->GetAnimator()->SetPlaybackSpeed(1.0f);
    player->GetAnimator()->Play("Idle", true, PlayerConst::AnimBlendDefault);
}

void PlayerIdle::Update(Player* player, float dt)
{
    if (!player->IsInputEnabled()) return;

    // Dash Priority (Seamlessly checks Keyboard and Gamepad LB)
    if (IsDashInputTriggered() && (player->canDash || player->IsPowerUncapped()))
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

// ============================================================
// MOVING
// ============================================================
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
        if (IsDashInputTriggered() && (player->canDash || player->IsPowerUncapped()))
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
// ============================================================
// DASH
// ============================================================

void PlayerDash::Enter(Player* player)
{
    constexpr float DASH_IFRAME_DURATION = 0.2f;

    timer = player->GetDashDuration();
    dashDir = player->GetLastValidInput();

    player->canDash = false;
    player->dashCooldownTimer = player->GetDashCooldown();
    player->TriggerInvincibility(DASH_IFRAME_DURATION);


    // =========================================================
    // Play VFX Dash Go dan sesuaikan arah rotasinya!
    // =========================================================
    DirectX::XMFLOAT3 pos = player->GetMovement()->GetPosition();
    pos.y += 1.0f; // Naikkan sedikit agar pas di tengah badan

    // [MODIFIKASI] Simpan handle ke variabel class
    m_dashGoVfxHandle = EffectManager::Instance().Play("Data/Effect/VFX_Player_Dash_Go.efk", pos, 0.2f);

    if (m_dashGoVfxHandle != -1) {
        // [FIX] Tambahkan DirectX::XM_PI (180 derajat dalam radian) untuk membalik arahnya!
        float yaw = atan2f(dashDir.x, dashDir.y) + DirectX::XM_PI;

        EffectManager::Instance().SetRotation(m_dashGoVfxHandle, { 0.0f, yaw, 0.0f });
    }

    std::string dashSounds[] = {
        "Data/Sound/SE_Dash_01.wav",
        "Data/Sound/SE_Dash_02.wav",
        "Data/Sound/SE_Dash_03.wav"
    };

    // 2. Pilih index secara acak (0, 1, atau 2)
    int randomIndex = rand() % 3;

    // 3. Mainkan suaranya lewat AudioManager
    // Kita gunakan volume 0.5f agar tidak terlalu memekakkan telinga
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

    // =========================================================
    // [BARU] Terus seret VFX mengikuti posisi player selama Dash berjalan
    // =========================================================
    if (m_dashGoVfxHandle != -1 && EffectManager::Instance().IsPlaying(m_dashGoVfxHandle))
    {
        DirectX::XMFLOAT3 trackPos = player->GetMovement()->GetPosition();
        trackPos.y += 1.0f; // Pastikan offset Y sama dengan saat Enter
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

// ============================================================
// SLASH
// ============================================================

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

    // =========================================================
    // THE FIX: MELEE ARMOR (I-FRAMES)
    // Grant brief invulnerability during the forward lunge.
    // If the Kamikaze explodes on contact, the player is immune.
    // =========================================================
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

// ============================================================
// PARRY
// ============================================================

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

// ============================================================
// SHOOT
// ============================================================

void PlayerShoot::Enter(Player* player)
{
    // Fix the visual gap: We initialize assuming the player WILL hold the button.
    // This ensures the gap between Shot 1 and Shot 2 matches Shot 2 and Shot 3.
    PerformShootInternal(player, true);
}

void PlayerShoot::Update(Player* player, float dt)
{
    // Dash Lockout Prevention
    if (IsDashInputTriggered() && (player->canDash || player->IsPowerUncapped()))
    {
        player->GetStateMachine()->ChangeState(player, std::make_unique<PlayerDash>());
        return;
    }

    m_timer -= dt;
    m_minTapCooldown -= dt;

    const bool isHolding{ IsShootInputPressed() };

    // =========================================================
    // BUG FIX: DECOUPLE MELEE FROM FIRE RATE
    // By checking this outside the m_timer block, the player can 
    // instantly snap into a slash animation the exact frame a 
    // Kamikaze enters the danger zone, bypassing gun cooldowns.
    // =========================================================
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
            // Lower-Body Animation Sync
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

    // --- FIRE RATE LOGIC TREE ---
    if (player->IsPowerUncapped())
    {
        // Absolute Priority: Overdrive bypasses all penalties
        m_minTapCooldown = 0.05f;
        m_timer = 0.05f;
    }
    else
    {
        // Always store the strict minimum delay to prevent spam exploits
        m_minTapCooldown = baseDelay; 

        if (isHeld)
        {
            // Compile-time constant ensures zero runtime cost for the multiplier
            constexpr float HOLD_PENALTY_MULTIPLIER{ 1.5f };
            m_timer = baseDelay * HOLD_PENALTY_MULTIPLIER;
        }
        else
        {
            m_timer = baseDelay;
        }
    }
}

void PlayerShoot::Exit(Player* player)
{
}