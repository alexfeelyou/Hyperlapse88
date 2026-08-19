#include "System/CollisionManager.h"
#include "System/Input.h"
#include "System/Graphics.h"
#include "AnimationController.h"
#include "Camera.h"
#include "Framework.h"
#include "NaviAlly.h"
#include "Player.h"
#include "PlayerConstants.h"
#include "PlayerStates.h"
#include "StateMachine.h"
#include <cmath>
#include <imgui.h>
#include "InputHelper.h"
#include "EffectManager.h"
#include "System/AudioManager.h"

using namespace DirectX;

// ============================================================
// LIFECYCLE
// ============================================================

Player::Player()
    : stateMachine(std::make_unique<StateMachine>())
    , animator(std::make_unique<AnimationController>())
{
    ID3D11Device* device = Graphics::Instance().GetDevice();
    model = std::make_shared<Model>(device, "Data/Model/Character/TEST_mdl_Player3.glb");
    scale = { 1.0f, 1.0f, 1.0f };

	// Load weapons and set their local offsets for correct hand positioning
    m_weapons[static_cast<size_t>(WeaponType::Crossbow)] = std::make_unique<Weapon>(device, "Data/Model/Character/WEAPON_mdl_Crossbow.glb");
    m_weapons[static_cast<size_t>(WeaponType::Crossbow)]->SetLocalOffset(
        { 0.000f, 0.000f, 0.000f },
        { 90.000f, 99.000f, 0.000f },
        { 0.900f, 0.900f, 0.400f }
    );

    m_weapons[static_cast<size_t>(WeaponType::Sword)] = std::make_unique<Weapon>(device, "Data/Model/Character/WEAPON_mdl_Sword.glb");
    m_weapons[static_cast<size_t>(WeaponType::Sword)]->SetLocalOffset(
        { 0.000f, 0.001f, 0.000f },
        { 0.000f, 180.000f, 0.000f },
        { 0.350f, 0.350f, 0.350f }
    );

    if (model) {
        m_rightHandBoneIndex = model->GetNodeIndex("hand.r");
    }

    m_playerbulletModel = std::make_shared<Model>(device, "Data/Model/Character/PLACEHOLDER_mdl_Paddle.glb");

    animator->Initialize(model);
    animator->SetUpperBodyMaskRoot("body");
    stateMachine->Initialize(std::make_unique<PlayerIdle>(), this);

    // Log loaded animations to debug output
    OutputDebugStringA("\n=== ANIMATIONS LOADED ===\n");
    const auto& anims = model->GetAnimations();
    for (size_t i = 0; i < anims.size(); ++i)
    {
        std::string msg = "[" + std::to_string(i) + "] " + anims[i].name + "\n";
        OutputDebugStringA(msg.c_str());
    }
    OutputDebugStringA("=========================\n\n");

    m_capeSimulator = std::make_unique<CapeSimulator>();

    auto GenerateBoneNames = [](const char* prefix, int startIdx, int endIdx)
        {
            std::vector<std::string> names;
            char buffer[32];
            for (int i = startIdx; i <= endIdx; ++i)
            {
                // %03d automatically adds the zeros: 1 becomes "001", 15 becomes "015"
                snprintf(buffer, sizeof(buffer), "%s%03d", prefix, i);
                names.push_back(std::string(buffer));
            }
            return names;
        };

    // Chain 1: Center (001 to 007)
    m_capeSimulator->AddChain(model, GenerateBoneNames("cape.", 1, 7));

    // Chain 2: Right (008 to 015)
    m_capeSimulator->AddChain(model, GenerateBoneNames("cape.", 8, 15));

    // Chain 3: Left (016 to 023)
    m_capeSimulator->AddChain(model, GenerateBoneNames("cape.", 16, 23));

    color = { 1.0f, 1.0f, 1.0f, 1.0f };
}

Player::~Player()
{
    if (m_physxController) m_physxController->release();

    if (m_dashReadyVfxHandle != -1) {
        EffectManager::Instance().Stop(m_dashReadyVfxHandle);
    }

    // Bersihkan efek standby jika player mendadak dihapus
    if (m_dashStandbyVfxHandle != -1) {
        EffectManager::Instance().Stop(m_dashStandbyVfxHandle);
    }

    // [BARU] Bersihkan efek overdrive
    if (m_overdriveVfxHandle != -1) {
        EffectManager::Instance().Stop(m_overdriveVfxHandle);
    }
}

void Player::InitPhysics(physx::PxControllerManager* manager, physx::PxMaterial* material, float spawnY)
{
    physx::PxCapsuleControllerDesc desc;
    desc.height = PlayerConst::CapsuleHeight;
    desc.radius = PlayerConst::CapsuleRadius;
    desc.position = physx::PxExtendedVec3(0.0, spawnY, 0.0);
    desc.material = material;
    desc.stepOffset = PlayerConst::CapsuleStep;

    m_physxController = manager->createController(desc);
}

void Player::ApplyConfig(const PlayerConfig& config) noexcept
{
    moveSpeed = config.moveSpeed;
    dashSpeed = config.dashSpeed;
    dashDuration = config.dashDuration;
    dashCooldown = config.dashCooldown;
    acceleration = config.acceleration;
    deceleration = config.deceleration;
    gravityEnabled = config.gravityEnabled;
    invertControls = config.invertControls;
}

void Player::Update(float elapsedTime, Camera* camera)
{
    if (m_invincibilityTimer > 0.0f)
    {
        m_invincibilityTimer -= elapsedTime;
    }

    if (m_isPowerUncapped && m_hp > 0 && m_hp < m_uncapMaxRegenHP) {
        m_uncapRegenAccumulator += m_uncapHealthRegenPerSecond * elapsedTime;
        int healAmount = static_cast<int>(m_uncapRegenAccumulator);
        if (healAmount > 0) {
            Heal(healAmount);
            m_uncapRegenAccumulator -= static_cast<float>(healAmount);
        }
    }
    else {
        m_uncapRegenAccumulator = 0.0f;
    }

    UpdateDashCooldown(elapsedTime);

    SetCamera(camera);
    if (isInputEnabled)
    {
        HandleMovementInput(elapsedTime);
        HandleAimInput(camera); 
    }
    else currentSmoothInput = { 0.0f, 0.0f };

    UpdateHorizontalMovement(elapsedTime);

    // -------------------------------------------------------------
    // ---> DEBUG: BYPASS THE STATE MACHINE FOR WEAPON TUNING <---
    // -------------------------------------------------------------
    if (m_debugState.forceAnimation)
    {
        // Force the animation on loop, bypassing the State Machine entirely!
        if (!animator->IsPlaying(m_debugState.animationName))
        {
            // Note: We play this on the FULL body, ignoring PlayUpper, 
            // so you get a perfectly clean stance for tuning!
            animator->Play(m_debugState.animationName, true, 0.2f);
        }
    }
    else
    {
        // Normal Gameplay
        if (stateMachine) stateMachine->Update(this, elapsedTime);
    }
    // -------------------------------------------------------------

    if (animator) animator->Update(elapsedTime);

    // -------------------------------------------------------------
    // ---> NEW: THE WEAPON STATE MANAGER (Auto-Sheathe) <---
    // -------------------------------------------------------------
    // If the player is holding the Sword, but the upper-body attack 
    // animation has officially finished, automatically revert to the Crossbow.
    // The !m_debugState check ensures the sword doesn't vanish while you are tuning it in the GUI!
    if (!m_debugState.forceAnimation && m_activeWeaponType == WeaponType::Sword && !animator->IsUpperPlaying())
    {
        SetActiveWeapon(WeaponType::Crossbow);
        m_aimLocked = false;
    }
    // -------------------------------------------------------------

    float smoothedYaw = 0.0f;
    bool  shouldAim = false;
    float relativeAngle = 0.0f;

    UpdateFootRotation(elapsedTime, smoothedYaw);
    UpdateAimConstraint(elapsedTime, smoothedYaw, shouldAim, relativeAngle);

    // ---> DEBUG: DISABLE SPINE TWIST <---
    if (m_debugState.disableAimConstraint)
    {
        shouldAim = false;
        relativeAngle = 0.0f;
    }

    ApplyWorldMatrix(smoothedYaw, shouldAim, relativeAngle);
    UpdateProjectiles(elapsedTime, camera);

    // =========================================================
    // [BARU] Update posisi VFX Dash Ready agar menempel ke Player
    // =========================================================
    if (m_dashReadyVfxHandle != -1 && EffectManager::Instance().IsPlaying(m_dashReadyVfxHandle))
    {
        DirectX::XMFLOAT3 vfxPos = movement->GetPosition();

        // Gunakan variabel offset dari GUI
        vfxPos.y += m_dashReadyOffsetY;

        EffectManager::Instance().SetPosition(m_dashReadyVfxHandle, vfxPos);
    }

    if (m_damageGlitchTimer > 0.0f)
    {
        m_damageGlitchTimer = (std::max)(0.0f, m_damageGlitchTimer - elapsedTime);
    }


    // =========================================================
    // Logika Standby Dash VFX (Otomatis & Tracking)
    // =========================================================

    if (m_hp <= 0) return;
    if (canDash)
    {
        // 1. Jika handle kosong atau efek sebelumnya sudah selesai (mati), putar lagi!
        if (m_dashStandbyVfxHandle == -1 || !EffectManager::Instance().IsPlaying(m_dashStandbyVfxHandle))
        {
            DirectX::XMFLOAT3 vfxPos = movement->GetPosition();
            vfxPos.y += m_dashReadyOffsetY;
            m_dashStandbyVfxHandle = EffectManager::Instance().Play("Data/Effect/VFX_Player_Dash_Standby.efk", vfxPos, 0.7f);
        }

        // 2. Selama efeknya hidup, terus update posisinya agar menempel ke player
        if (m_dashStandbyVfxHandle != -1 && EffectManager::Instance().IsPlaying(m_dashStandbyVfxHandle))
        {
            DirectX::XMFLOAT3 trackPos = movement->GetPosition();
            trackPos.y += m_dashReadyOffsetY;
            EffectManager::Instance().SetPosition(m_dashStandbyVfxHandle, trackPos);
        }
    }
    else
    {
        // 3. Jika dash sedang tidak bisa dipakai (cooldown), matikan efek standby!
        if (m_dashStandbyVfxHandle != -1) {
            EffectManager::Instance().Stop(m_dashStandbyVfxHandle);
            m_dashStandbyVfxHandle = -1; // Reset handle agar bisa spawn baru nanti
        }
    }

    // =========================================================
        // [BARU] Update posisi VFX Dash Ready agar menempel ke Player
        // =========================================================
    if (m_dashReadyVfxHandle != -1 && EffectManager::Instance().IsPlaying(m_dashReadyVfxHandle))
    {
        DirectX::XMFLOAT3 vfxPos = movement->GetPosition();
        vfxPos.y += m_dashReadyOffsetY;
        EffectManager::Instance().SetPosition(m_dashReadyVfxHandle, vfxPos);
    }

    // =========================================================
    // Logika VFX Overdrive & Standby Dash (Hierarki & Tracking)
    // =========================================================
    if (IsPowerUncapped())
    {
        // 1. Matikan paksa Dash Standby jika kebetulan masih menyala
        if (m_dashStandbyVfxHandle != -1) {
            EffectManager::Instance().Stop(m_dashStandbyVfxHandle);
            m_dashStandbyVfxHandle = -1;
        }

        // 2. Mainkan efek Overdrive jika belum hidup
        if (m_overdriveVfxHandle == -1 || !EffectManager::Instance().IsPlaying(m_overdriveVfxHandle))
        {
            DirectX::XMFLOAT3 vfxPos = movement->GetPosition();
            vfxPos.y += m_dashReadyOffsetY;
            m_overdriveVfxHandle = EffectManager::Instance().Play("Data/Effect/VFX_Player_Overdrive.efk", vfxPos, 1.0f);
        }

        // 3. Terus update posisinya mengikuti player
        if (m_overdriveVfxHandle != -1 && EffectManager::Instance().IsPlaying(m_overdriveVfxHandle))
        {
            DirectX::XMFLOAT3 trackPos = movement->GetPosition();
            trackPos.y += m_dashReadyOffsetY;
            EffectManager::Instance().SetPosition(m_overdriveVfxHandle, trackPos);
        }
    }
    else
    {
        // 1. Matikan efek Overdrive jika player kehilangan status Uncapped
        if (m_overdriveVfxHandle != -1) {
            EffectManager::Instance().Stop(m_overdriveVfxHandle);
            m_overdriveVfxHandle = -1;
        }

        // 2. Fallback ke logika normal Dash Standby
        if (canDash)
        {
            if (m_dashStandbyVfxHandle == -1 || !EffectManager::Instance().IsPlaying(m_dashStandbyVfxHandle))
            {
                DirectX::XMFLOAT3 vfxPos = movement->GetPosition();
                vfxPos.y += m_dashReadyOffsetY;
                m_dashStandbyVfxHandle = EffectManager::Instance().Play("Data/Effect/VFX_Player_Dash_Standby.efk", vfxPos, 0.7f);
            }

            if (m_dashStandbyVfxHandle != -1 && EffectManager::Instance().IsPlaying(m_dashStandbyVfxHandle))
            {
                DirectX::XMFLOAT3 trackPos = movement->GetPosition();
                trackPos.y += m_dashReadyOffsetY;
                EffectManager::Instance().SetPosition(m_dashStandbyVfxHandle, trackPos);
            }
        }
        else
        {
            if (m_dashStandbyVfxHandle != -1) {
                EffectManager::Instance().Stop(m_dashStandbyVfxHandle);
                m_dashStandbyVfxHandle = -1;
            }
        }
    }
}

// ============================================================
// UPDATE SUB-STEPS
// ============================================================

void Player::UpdateDashCooldown(float dt)
{
    if (canDash) return;

    dashCooldownTimer -= dt;
    if (dashCooldownTimer <= 0.0f)
    {
        canDash = true;

        // [MODIFIKASI] Play VFX dan simpan handle-nya
        DirectX::XMFLOAT3 pos = movement->GetPosition();
        pos.y += m_dashReadyOffsetY;

        m_dashReadyVfxHandle = EffectManager::Instance().Play("Data/Effect/VFX_Player_Dash_Ready.efk", movement->GetPosition(), 0.5f);
        AudioManager::Instance().PlaySFX("Data/Sound/SE_Player_Dash_Ready_01.wav", 0.3f);
    }
}

void Player::HandleMovementInput(float dt)
{
    // 1. Fast early exit (Zero runtime cost for subsequent logic if disabled)
    if (!isInputEnabled)
    {
        currentSmoothInput = { 0.0f, 0.0f };
        return;
    }

    // 2. Fetch Analog Stick Data (Uniform Brace Initialization to prevent narrowing)
    const GamePad& gamePad{ Input::Instance().GetGamePad() };
    float targetX{ gamePad.GetAxisLX() };
    float targetZ{ gamePad.GetAxisLY() };

    // 3. Epsilon check (Defends against analog stick hardware drift)
    constexpr float inputEpsilon{ 0.01f };
    const bool isGamepadIdle{ std::abs(targetX) < inputEpsilon && std::abs(targetZ) < inputEpsilon };

    // 4. Fallback to Keyboard if Gamepad is idle (Clean input hierarchy)
    if (isGamepadIdle)
    {
        targetX = 0.0f;
        targetZ = 0.0f;
        if (GetAsyncKeyState('W') & 0x8000) targetZ += 1.0f;
        if (GetAsyncKeyState('S') & 0x8000) targetZ -= 1.0f;
        if (GetAsyncKeyState('A') & 0x8000) targetX -= 1.0f;
        if (GetAsyncKeyState('D') & 0x8000) targetX += 1.0f;
    }

    // 5. Apply Inversion cleanly
    if (invertControls)
    {
        targetX = -targetX;
        targetZ = -targetZ;
    }

    // 6. Vector Math & Normalization Guard (Defends against the "Diagonal Speed" Bug)
    const float sqLength{ (targetX * targetX) + (targetZ * targetZ) };
    if (sqLength > 1.0f)
    {
        // Clamp magnitude to 1.0f using reciprocal multiplication (Optimized for compiler fast-math)
        const float invLength{ 1.0f / std::sqrt(sqLength) };
        targetX *= invLength;
        targetZ *= invLength;
    }
    else if (sqLength > 0.0f && isGamepadIdle)
    {
        // Keyboard inputs are purely digital; normalize them perfectly to 1.0
        const float invLength{ 1.0f / std::sqrt(sqLength) };
        targetX *= invLength;
        targetZ *= invLength;
    }
    // (Note: If it's a gamepad and sqLength <= 1.0f, we KEEP the magnitude to allow analog "slow walking")

    // 7. Smooth acceleration / deceleration
    const float smoothX{ (std::abs(targetX) > inputEpsilon) ? acceleration : deceleration };
    const float smoothZ{ (std::abs(targetZ) > inputEpsilon) ? acceleration : deceleration };

    currentSmoothInput.x += (targetX - currentSmoothInput.x) * smoothX * dt;
    currentSmoothInput.y += (targetZ - currentSmoothInput.y) * smoothZ * dt;

    // 8. Snap to zero below threshold (Defends against creeping floating-point drift over time)
    if (std::abs(currentSmoothInput.x) < inputEpsilon) currentSmoothInput.x = 0.0f;
    if (std::abs(currentSmoothInput.y) < inputEpsilon) currentSmoothInput.y = 0.0f;

    // 9. Track last non-zero input direction (Vital for your Dash mechanic)
    if (std::abs(targetX) > inputEpsilon || std::abs(targetZ) > inputEpsilon)
    {
        lastValidInput = { targetX, targetZ };
    }
}

void Player::HandleAimInput(Camera* camera)
{
    // Anticipate Nullptr Bug: If no camera is provided, we cannot calculate 3D aim.
    if (!camera) return;

    // Zero-Cost Branching: Only execute the math for the currently active device.
    const InputDevice activeDevice{ Input::Instance().GetLastUsedDevice() };

    if (activeDevice == InputDevice::Gamepad)
    {
        const GamePad& gamePad{ Input::Instance().GetGamePad() };
        const float rx{ gamePad.GetAxisRX() };
        const float ry{ gamePad.GetAxisRY() };

        constexpr float aimDeadzoneSq{ 0.04f };
        const float sqLength{ (rx * rx) + (ry * ry) };

        if (sqLength > aimDeadzoneSq)
        {
            const float invLength{ 1.0f / std::sqrt(sqLength) };

            const float dirX{ rx * invLength };
            const float dirZ{ ry * invLength }; // Change to -ry if Y-axis is inverted in your world

            const DirectX::XMFLOAT3 pPos{ movement->GetPosition() };
            constexpr float aimDistance{ 1000.0f };

            DirectX::XMFLOAT3 trueGamepadWorldPos{
                pPos.x + (dirX * aimDistance),
                pPos.y,
                pPos.z + (dirZ * aimDistance)
            };

            RotateModelToPoint(trueGamepadWorldPos);
        }
    }
    else
    {
        // Keyboard & Mouse Raycast Logic
        float mouseX, mouseY;
        SDL_GetMouseState(&mouseX, &mouseY);

        // Safely fetch dynamic screen size
        float screenW{ 1920.0f };
        float screenH{ 1080.0f };
        if (auto window{ Framework::Instance()->GetMainWindow() }) {
            screenW = static_cast<float>(window->GetWidth());
            screenH = static_cast<float>(window->GetHeight());
        }

        DirectX::XMMATRIX view{ DirectX::XMLoadFloat4x4(&camera->GetView()) };
        DirectX::XMMATRIX proj{ DirectX::XMLoadFloat4x4(&camera->GetProjection()) };
        DirectX::XMMATRIX world{ DirectX::XMMatrixIdentity() };

        DirectX::XMVECTOR nearPoint{ DirectX::XMVectorSet(mouseX, mouseY, 0.0f, 0.0f) };
        DirectX::XMVECTOR farPoint{ DirectX::XMVectorSet(mouseX, mouseY, 1.0f, 0.0f) };

        nearPoint = DirectX::XMVector3Unproject(nearPoint, 0, 0, screenW, screenH, 0.0f, 1.0f, proj, view, world);
        farPoint = DirectX::XMVector3Unproject(farPoint, 0, 0, screenW, screenH, 0.0f, 1.0f, proj, view, world);

        DirectX::XMVECTOR rayDir{ DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(farPoint, nearPoint)) };
        DirectX::XMFLOAT3 origin, dir;
        DirectX::XMStoreFloat3(&origin, nearPoint);
        DirectX::XMStoreFloat3(&dir, rayDir);

        if (std::abs(dir.y) > 0.001f) {
            // Defends against the "Floating Gun" bug by using the player's dynamic Y position
            const float gunHeight{ movement->GetPosition().y + PlayerConst::BulletSpawnY };
            const float t{ (gunHeight - origin.y) / dir.y };

            DirectX::XMFLOAT3 trueMouseWorldPos{
                origin.x + dir.x * t,
                gunHeight,
                origin.z + dir.z * t
            };

            RotateModelToPoint(trueMouseWorldPos);
        }
    }
}

void Player::UpdateHorizontalMovement(float dt)
{
    float displacementX = 0.0f;
    float displacementZ = 0.0f;

    XMFLOAT3 stateVelocity = movement->GetVelocity();

    // State-driven velocity (e.g. dash) takes priority over walk input
    if (stateVelocity.x != 0.0f || stateVelocity.z != 0.0f)
    {
        displacementX = stateVelocity.x * dt;
        displacementZ = stateVelocity.z * dt;
    }
    else
    {
        displacementX = currentSmoothInput.x * moveSpeed * dt;
        displacementZ = currentSmoothInput.y * moveSpeed * dt;
    }

    // Apply displacement through PhysX capsule controller
    const float gravityY = gravityEnabled ? PlayerConst::Gravity * dt : 0.0f;
    physx::PxVec3 displacement(displacementX, gravityY, displacementZ);
    m_physxController->move(displacement, PlayerConst::PhysXMinDist, dt, physx::PxControllerFilters());

    // Mirror PhysX position back to our movement state (offset by capsule half-height)
    physx::PxExtendedVec3 pxPos = m_physxController->getPosition();
    movement->SetPosition({ (float)pxPos.x, (float)pxPos.y - PlayerConst::CapsuleHalfHeight, (float)pxPos.z });
}

void Player::UpdateFootRotation(float dt, float& outSmoothedYaw)
{
    XMFLOAT2 moveInput = GetLastValidInput();
    float currentYaw = XMConvertToRadians(movement->GetRotation().y);
    float targetYaw = currentYaw;

    if (moveInput.x != 0.0f || moveInput.y != 0.0f)
        targetYaw = atan2f(moveInput.x, moveInput.y);

    m_isBackpedaling = false;

    XMFLOAT3 pos = movement->GetPosition();
    float dx = m_aimTarget.x - pos.x;
    float dz = m_aimTarget.z - pos.z;

    if ((dx * dx + dz * dz) > PlayerConst::AimMinDistSq)
    {
        float aimYaw = atan2f(dx, dz);
        float diff = targetYaw - aimYaw;

        while (diff > XM_PI) diff -= XM_2PI;
        while (diff < -XM_PI) diff += XM_2PI;

        if (diff > XM_PIDIV2)
        {
            diff = XM_PI - diff;
            targetYaw = aimYaw + diff;
            m_isBackpedaling = true; // <-- NEW: We are backpedaling!
        }
        else if (diff < -XM_PIDIV2)
        {
            diff = -XM_PI - diff;
            targetYaw = aimYaw + diff;
            m_isBackpedaling = true; // <-- NEW: We are backpedaling!
        }

        while (targetYaw > XM_PI) targetYaw -= XM_2PI;
        while (targetYaw < -XM_PI) targetYaw += XM_2PI;
    }

    float angleDiff = targetYaw - currentYaw;
    while (angleDiff > XM_PI) angleDiff -= XM_2PI;
    while (angleDiff < -XM_PI) angleDiff += XM_2PI;

    float lerpFactor = min(PlayerConst::RotSmoothSpeed * dt, 1.0f);
    outSmoothedYaw = currentYaw + angleDiff * lerpFactor;
}

void Player::UpdateAimConstraint(float dt, float& inOutSmoothedYaw, bool& outShouldAim, float& outRelativeAngle)
{
    outShouldAim = false;
    outRelativeAngle = 0.0f;

    if (!model || !activeCamera) return;

    DirectX::XMFLOAT3 pos = movement->GetPosition();
    float dx = m_aimTarget.x - pos.x;
    float dz = m_aimTarget.z - pos.z;

    if ((dx * dx + dz * dz) <= PlayerConst::AimMinDistSq) return;

    outShouldAim = true;

    float absoluteAngleToMouse = atan2f(dx, dz);
    float relativeAngle = absoluteAngleToMouse - inOutSmoothedYaw;

    while (relativeAngle > DirectX::XM_PI) relativeAngle -= DirectX::XM_2PI;
    while (relativeAngle < -DirectX::XM_PI) relativeAngle += DirectX::XM_2PI;

    // -----------------------------------------------------------------
    // ---> THE FIX: SMOOTH FOOT DRAG (Zero Duplication Math) <---
    // -----------------------------------------------------------------
    // Clamp torso to ±MaxTorsoAngle; if clamped, PULL feet smoothly to compensate
    if (std::abs(relativeAngle) > PlayerConst::MaxTorsoAngle)
    {
        // 1. Get the direction of the twist (1.0f for right, -1.0f for left)
        float sign = (relativeAngle > 0.0f) ? 1.0f : -1.0f;

        // 2. Safely clamp the spine twist
        relativeAngle = PlayerConst::MaxTorsoAngle * sign;

        // 3. Calculate exactly where the feet NEED to be to support this spine twist
        float targetFootYaw = absoluteAngleToMouse - relativeAngle;

        // 4. Find the shortest path for the feet to rotate
        float diff = targetFootYaw - inOutSmoothedYaw;
        while (diff > DirectX::XM_PI) diff -= DirectX::XM_2PI;
        while (diff < -DirectX::XM_PI) diff += DirectX::XM_2PI;

        // 5. Smoothly drag the feet over time instead of teleporting them!
        inOutSmoothedYaw += diff * (std::min)(PlayerConst::RotSmoothSpeed * dt, 1.0f);
    }

    outRelativeAngle = relativeAngle;
}

void Player::ApplyWorldMatrix(float smoothedYaw, bool shouldAim, float relativeAngle)
{
    // Commit smoothed foot yaw to movement state
    movement->SetRotationY(XMConvertToDegrees(smoothedYaw));

    XMFLOAT3 pos = movement->GetPosition();
    XMFLOAT3 rot = movement->GetRotation();

    XMMATRIX S = XMMatrixScaling(scale.x, scale.y, scale.z);
    XMMATRIX R = XMMatrixRotationRollPitchYaw(
        XMConvertToRadians(rot.x),
        XMConvertToRadians(rot.y),
        XMConvertToRadians(rot.z));
    XMMATRIX T = XMMatrixTranslation(pos.x, pos.y, pos.z);

    XMFLOAT4X4 worldMatrix;
    XMStoreFloat4x4(&worldMatrix, S * R * T);

    // Inject torso bone twist for upper-body aiming
    if (shouldAim && model)
    {
        int bodyIndex = model->GetNodeIndex("body");
        if (bodyIndex != -1)
        {
            Model::Node& bodyNode = model->GetNodes()[bodyIndex];

            // Get the Parent's (Hips/Pelvis) Global Matrix
            XMMATRIX parentGlobal = XMMatrixIdentity();
            if (bodyNode.parent != nullptr) {
                parentGlobal = XMLoadFloat4x4(&bodyNode.parent->globalTransform);
            }

            // Find the "True Sky" inside the tilted Hip space
            XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
            XMMATRIX parentInverse = XMMatrixInverse(nullptr, parentGlobal);
            XMVECTOR localUpAxis = XMVector3TransformNormal(worldUp, parentInverse);
            localUpAxis = XMVector3Normalize(localUpAxis);

            // Create a rotation around that specific calculated axis 
            XMMATRIX twistMatrix = XMMatrixRotationAxis(localUpAxis, relativeAngle);

            // Apply it to the animation
            XMVECTOR currentLocalRot = XMLoadFloat4(&bodyNode.rotation);
            XMMATRIX localMatrix = XMMatrixRotationQuaternion(currentLocalRot);

            // Multiply Local * Twist
            XMVECTOR finalRot = XMQuaternionRotationMatrix(localMatrix * twistMatrix);
            XMStoreFloat4(&bodyNode.rotation, finalRot);
        }
    }

    if (m_capeSimulator)
    {
        DirectX::XMFLOAT3 trueVelocity{ movement->GetVelocity() };

        // Fallback to input velocity if true velocity is zero 
        if (trueVelocity.x == 0.0f && trueVelocity.z == 0.0f)
        {
            trueVelocity.x = currentSmoothInput.x * moveSpeed;
            trueVelocity.z = currentSmoothInput.y * moveSpeed;
        }

        const float totalYaw{ smoothedYaw + relativeAngle };
        const float sinYaw{ std::sin(totalYaw) };
        const float cosYaw{ std::cos(totalYaw) };

        // 2D Rotation Matrix projection (World -> Local)
        const float localVz{ (trueVelocity.x * sinYaw) + (trueVelocity.z * cosYaw) };
        const float localVx{ (trueVelocity.x * cosYaw) - (trueVelocity.z * sinYaw) };

        const DirectX::XMFLOAT3 localVelocity{ localVx, trueVelocity.y, localVz };

        m_capeSimulator->Update(0.016f, localVelocity);
    }

    if (model) model->UpdateTransform(worldMatrix);

    DirectX::XMFLOAT4X4 attachMatrix = worldMatrix; // Fallback to feet if hand is missing
    if (m_rightHandBoneIndex != -1 && model->GetNodes().size() > m_rightHandBoneIndex)
    {
        attachMatrix = model->GetNodes()[m_rightHandBoneIndex].worldTransform;
    }

    // Optimization: Range-based for-loop. Updates all weapons so they are ready instantly.
    for (const auto& weapon : m_weapons)
    {
        if (weapon) weapon->UpdateTransform(attachMatrix);
    }
}

void Player::UpdateProjectiles(float dt, Camera* camera)
{
    DirectX::XMFLOAT3 myPos = movement->GetPosition();

    // Max distance before the bullet vanishes
    constexpr float DESPAWN_DISTANCE = 55.0f;
    constexpr float DESPAWN_DIST_SQ = DESPAWN_DISTANCE * DESPAWN_DISTANCE;

    for (auto& bullet : m_projectiles)
    {
        if (!bullet->IsActive()) continue;

        bullet->Update(dt, camera);

        // ---> BUG PREVENTION: The Infinite Flight Guard <---
        DirectX::XMFLOAT3 bPos = bullet->GetMovement()->GetPosition();
        float dx = myPos.x - bPos.x;
        float dz = myPos.z - bPos.z;

        if ((dx * dx + dz * dz) > DESPAWN_DIST_SQ)
        {
            // The bullet missed and flew off-screen.
            // DO NOT ERASE IT! Turn it off so we can recycle its memory later!
            bullet->SetActive(false);
        }
    }
}

// ============================================================
// AIM & PROJECTILES
// ============================================================

void Player::RotateModelToPoint(const DirectX::XMFLOAT3& targetPos)
{
    if (!m_aimLocked) {
        m_aimTarget = targetPos;
    }
}

void Player::FireProjectile()
{
    if (!isInputEnabled) return;

    const DirectX::XMFLOAT3 myPos{ movement->GetPosition() };

    // Calculate purely on the XZ plane
    const float dx{ m_aimTarget.x - myPos.x };
    const float dz{ m_aimTarget.z - myPos.z };
    const float angleToMouse{ std::atan2f(dx, dz) };
    const DirectX::XMFLOAT3 fwd{ std::sinf(angleToMouse), 0.0f, std::cosf(angleToMouse) };

    // Spawn slightly ahead of the player at chest height
    const DirectX::XMFLOAT3 spawnPos{
        myPos.x + fwd.x * PlayerConst::BulletSpawnFwd,
        myPos.y + PlayerConst::BulletSpawnY,
        myPos.z + fwd.z * PlayerConst::BulletSpawnFwd
    };

    // --------------------------------------------------------
    // TRUE OBJECT POOL (Zero Allocation on normal fire)
    // --------------------------------------------------------
    for (const auto& bullet : m_projectiles)
    {
        if (!bullet->IsActive())
        {
            bullet->Fire(spawnPos, fwd, m_bulletSpeed);
            bullet->SetDamage(m_bulletDamage);
            return;
        }
    }

    // Only allocate memory if EVERY bullet is currently flying on-screen.
    auto newBullet{ std::make_unique<Bullet>() };
    newBullet->Fire(spawnPos, fwd, m_bulletSpeed);
    newBullet->SetDamage(m_bulletDamage);
    m_projectiles.push_back(std::move(newBullet));

    // Prevent memory leak compounding if the pool gets ridiculously large.
    for (int i = 0; i < PlayerConst::MaxBullets; ++i) {
        auto b{ std::make_unique<Bullet>() };
        b->SetActive(false);
        m_projectiles.push_back(std::move(b));
    }
}

void Player::RenderProjectiles(ModelRenderer* renderer)
{
    for (auto& bullet : m_projectiles)
    {
        if (!bullet->IsActive()) continue;

        if (m_playerbulletModel)
        {
            DirectX::XMFLOAT3 bPos = bullet->GetMovement()->GetPosition();
            DirectX::XMFLOAT3 bVel = bullet->GetVelocity();
            float yaw = atan2f(bVel.x, bVel.z);

            DirectX::XMMATRIX S = DirectX::XMMatrixScaling(
                m_playerbulletOffsetScale.x,
                m_playerbulletOffsetScale.y,
                m_playerbulletOffsetScale.z
            );

            DirectX::XMMATRIX R = DirectX::XMMatrixRotationRollPitchYaw(
                DirectX::XMConvertToRadians(m_playerbulletOffsetRot.x),
                DirectX::XMConvertToRadians(m_playerbulletOffsetRot.y),
                DirectX::XMConvertToRadians(m_playerbulletOffsetRot.z)
            );
            DirectX::XMMATRIX T = DirectX::XMMatrixTranslation(m_playerbulletOffsetPos.x, m_playerbulletOffsetPos.y, m_playerbulletOffsetPos.z);

            DirectX::XMMATRIX bulletRot = DirectX::XMMatrixRotationY(yaw);
            DirectX::XMMATRIX bulletTrans = DirectX::XMMatrixTranslation(bPos.x, bPos.y, bPos.z);

            DirectX::XMFLOAT4X4 worldMatrix;
            DirectX::XMStoreFloat4x4(&worldMatrix, S * R * T * bulletRot * bulletTrans);

            renderer->Draw(ShaderId::Phong, m_playerbulletModel, m_playerbulletColor, worldMatrix);
        }
        else
        {
            renderer->Draw(ShaderId::Phong, bullet->GetModel(), { 1.0f, 1.0f, 1.0f, 1.0f });
        }
    }
}

void Player::RenderWeapon(ModelRenderer* renderer)
{
    if (Weapon* activeWpn = GetActiveWeapon())
    {
        activeWpn->Render(renderer);
    }
}

void Player::StopAllVFX()
{
    // Clear Dash Ready Effect
    if (m_dashReadyVfxHandle != -1) {
        EffectManager::Instance().Stop(m_dashReadyVfxHandle);
        m_dashReadyVfxHandle = -1;
    }
    // Clear Dash Standby Effect
    if (m_dashStandbyVfxHandle != -1) {
        EffectManager::Instance().Stop(m_dashStandbyVfxHandle);
        m_dashStandbyVfxHandle = -1;
    }
    // Clear Overdrive Effect
    if (m_overdriveVfxHandle != -1) {
        EffectManager::Instance().Stop(m_overdriveVfxHandle);
        m_overdriveVfxHandle = -1;
    }
}

// ============================================================
// DAMAGE SYSTEM
// ============================================================
void Player::TakeDamage(float damage) 
{
    if (m_hp <= 0.0f || IsInvincible()) return;

    m_hp -= damage;

    if (m_enableIFrames) {
        TriggerInvincibility(m_iFrameDuration);
    }
    m_damageGlitchTimer = DAMAGE_GLITCH_DURATION;

    if (m_hp <= 0.0f)
    {
        m_hp = 0.0f;
        StopAllVFX();
    }
}

void Player::Heal(float amount) {
    if (amount <= 0.0f || m_hp <= 0.0f) return;
    m_hp += amount;
    if (m_hp > m_maxHp) m_hp = m_maxHp; // FIXED
}

void Player::Heal(int amount) {
    if (amount <= 0 || m_hp <= 0) return;
    m_hp += static_cast<float>(amount);
    if (m_hp > m_maxHp) m_hp = m_maxHp; // FIXED

}
// ============================================================
// GAME FEEL & JUICE
// ============================================================
float Player::GetDamageGlitchIntensity() const noexcept
{
    if (m_damageGlitchTimer <= 0.0f) return 0.0f;
    const float t{ m_damageGlitchTimer / DAMAGE_GLITCH_DURATION };

    return DAMAGE_GLITCH_MAX_INTENSITY * (t * t);
}

// ============================================================
// HELPERS
// ============================================================

void Player::SetPosition(float x, float y, float z)
{
    if (movement) movement->SetPosition({ x, y, z });
}

void Player::SetPosition(const DirectX::XMFLOAT3& pos)
{
    // Update komponen movement logical
    if (movement) movement->SetPosition(pos);

    // PENTING: Update juga komponen fisika PhysX
    if (m_physxController)
    {
        // PxExtendedVec3 membutuhkan offset Y karena posisi 'movement' 
        // berada di dasar kaki, sedangkan PxController (kapsul) 
        // posisinya dihitung dari tengah kapsul (titik pusat/centroid).
        m_physxController->setPosition(physx::PxExtendedVec3(
            pos.x,
            pos.y + PlayerConst::CapsuleHalfHeight,
            pos.z
        ));
    }
}

void Player::ReleasePowerCap()
{
    if (m_isPowerUncapped) return;
    m_isPowerUncapped = true;
    m_uncapRegenAccumulator = 0.0f;

    // Simpan nilai saat ini sebelum ditimpa, agar bisa dikembalikan nanti
    m_normalMoveSpeed = moveSpeed;
    m_normalDashSpeed = dashSpeed;
    m_normalColor = color;

    // Terapkan atribut Overdrive
    moveSpeed = m_uncapMoveSpeed;
    dashSpeed = m_uncapDashSpeed;
    color = m_uncapColor;
}

void Player::RestorePowerCap()
{
    if (!m_isPowerUncapped) return;
    m_isPowerUncapped = false;
    m_uncapRegenAccumulator = 0.0f;

    // Kembalikan atribut ke nilai normal
    moveSpeed = m_normalMoveSpeed;
    dashSpeed = m_normalDashSpeed;
    color = m_normalColor;
}


void Player::DrawDebugGUI()
{
    if (ImGui::CollapsingHeader("Movement & Physics", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Status: %s", isInputEnabled ? "Input ON" : "Input OFF");
        ImGui::Checkbox("Invert Controls", &invertControls);
        ImGui::DragFloat("Walk Speed", &moveSpeed, 0.1f, 0.0f, 100.0f, "%.1f");
        ImGui::DragFloat("Acceleration", &acceleration, 0.1f, 0.1f, 100.0f, "%.1f");
        ImGui::DragFloat("Deceleration", &deceleration, 0.1f, 0.1f, 100.0f, "%.1f");
    }

    if (ImGui::CollapsingHeader("Dash Settings", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::DragFloat("Dash Speed", &dashSpeed, 0.5f, 10.0f, 200.0f, "%.1f");
        ImGui::DragFloat("Dash Duration", &dashDuration, 0.01f, 0.01f, 1.0f, "%.2f sec");
        ImGui::DragFloat("Dash Cooldown", &dashCooldown, 0.01f, 0.0f, 5.0f, "%.2f sec");
    }

    if (ImGui::CollapsingHeader("Combat & Projectiles", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "[ General Combat ]");
        float hp = m_hp;
        if (ImGui::DragFloat("Player HP", &hp, 1.0f, 0.0f, m_maxHp)) m_hp = hp;
        float maxHp = m_maxHp;
        if (ImGui::DragFloat("Player Max HP", &maxHp, 1.0f, 1.0f, 9999.0f)) SetMaxHP(maxHp);

        // --- 無敵時間 (I-Frames) のコントロール ---
        ImGui::Checkbox("Enable I-Frames (Invincibility on hit)", &m_enableIFrames);
        if (m_enableIFrames) {
            ImGui::Indent();
            ImGui::DragFloat("I-Frame Duration", &m_iFrameDuration, 0.1f, 0.1f, 5.0f, "%.1f sec");
            ImGui::Unindent();
        }
        ImGui::Separator();

        // --- Toggle Uncap (Overdrive) ---
        bool powerUncapped = IsPowerUncapped();
        if (ImGui::Checkbox("Uncap Power (Overdrive)", &powerUncapped)) {
            if (powerUncapped) ReleasePowerCap();
            else RestorePowerCap();
        }

        // --- Parameter Uncap Muncul Jika Aktif ---
        if (powerUncapped) {
            ImGui::Indent();
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), ">> Uncap Tuning <<");

            // Jika slider digeser saat Uncap aktif, langsung terapkan nilainya secara real-time
            if (ImGui::DragFloat("Uncap Walk Speed", &m_uncapMoveSpeed, 0.1f, 10.0f, 100.0f, "%.1f")) moveSpeed = m_uncapMoveSpeed;
            if (ImGui::DragFloat("Uncap Dash Speed", &m_uncapDashSpeed, 0.5f, 10.0f, 200.0f, "%.1f")) dashSpeed = m_uncapDashSpeed;
            ImGui::DragFloat("Uncap HP Regen / Sec", &m_uncapHealthRegenPerSecond, 0.1f, 0.0f, 100.0f, "%.1f");
            ImGui::DragFloat("Uncap Regen Max HP", &m_uncapMaxRegenHP, 1.0f, 1.0f, 9999.0f);
            if (ImGui::ColorEdit4("Uncap Glow Color", (float*)&m_uncapColor)) color = m_uncapColor;

            ImGui::Unindent();
        }

        ImGui::Separator();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "[ Crossbow Bullet ]");
        ImGui::DragFloat("Bullet Speed", &m_bulletSpeed, 0.5f, 1.0f, 150.0f, "%.1f");
        ImGui::SliderInt("Bullet Damage", &m_bulletDamage, 1, 500); // [BARU] Slider Damage Player
        ImGui::ColorEdit4("Bullet Tint Color", (float*)&m_playerbulletColor);

        if (ImGui::TreeNode("Bullet Model Transform (Offset)"))
        {
            ImGui::DragFloat3("Position", (float*)&m_playerbulletOffsetPos, 0.01f);
            ImGui::DragFloat3("Rotation", (float*)&m_playerbulletOffsetRot, 0.5f);
            ImGui::DragFloat3("Scale", (float*)&m_playerbulletOffsetScale, 0.1f);
            if (ImGui::Button("Reset Offsets", ImVec2(-1.0f, 25.0f))) ResetPlayerBulletOffsets();
            ImGui::TreePop();
        }
    }
}
