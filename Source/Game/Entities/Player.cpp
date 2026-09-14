#include "System/AssetManager.h"
#include "System/PhysicsManager.h"
#include "CharacterMovementComponent.h"
#include "GameObject.h"
#include "Player.h"
#include "PlayerControllerComponent.h"

using namespace DirectX;

Player::Player()
    : animator(std::make_unique<AnimationController>())
{
    ID3D11Device* device = Graphics::Instance().GetDevice();

    model = Engine::System::AssetManager::Instance().GetOrLoadModel(device, "Data/Model/Character/TEST_mdl_Player3.glb");
    scale = { 1.0f, 1.0f, 1.0f };

    m_weapons[static_cast<size_t>(WeaponType::Crossbow)] = std::make_unique<Weapon>(device, "Data/Model/Character/WEAPON_mdl_Crossbow.glb");
    m_weapons[static_cast<size_t>(WeaponType::Crossbow)]->SetLocalOffset(
        { 0.000f, 0.000f, 0.000f }, { 90.000f, 99.000f, 0.000f }, { 0.01f, 0.01f, 0.01f }
    );

    m_weapons[static_cast<size_t>(WeaponType::Sword)] = std::make_unique<Weapon>(device, "Data/Model/Character/WEAPON_mdl_Sword.glb");
    m_weapons[static_cast<size_t>(WeaponType::Sword)]->SetLocalOffset(
        { 0.000f, 0.001f, 0.000f }, { 0.000f, 180.000f, 0.000f }, { 0.01f, 0.01f, 0.01f }
    );

    if (model) m_rightHandBoneIndex = model->GetNodeIndex("hand.r");

    m_playerbulletModel = Engine::System::AssetManager::Instance().GetOrLoadModel(device, "Data/Model/Character/PLACEHOLDER_mdl_Paddle.glb");

    animator->Initialize(model);
    animator->SetUpperBodyMaskRoot("body");

    m_capeSimulator = std::make_unique<CapeSimulator>();

    auto GenerateBoneNames = [](const char* prefix, int startIdx, int endIdx) {
        std::vector<std::string> names;
        char buffer[32];
        for (int i = startIdx; i <= endIdx; ++i) {
            snprintf(buffer, sizeof(buffer), "%s%03d", prefix, i);
            names.push_back(std::string(buffer));
        }
        return names;
        };

    m_capeSimulator->AddChain(model, GenerateBoneNames("cape.", 1, 7));
    m_capeSimulator->AddChain(model, GenerateBoneNames("cape.", 8, 15));
    m_capeSimulator->AddChain(model, GenerateBoneNames("cape.", 16, 23));

    color = { 1.0f, 1.0f, 1.0f, 1.0f };
}

Player::~Player()
{
    StopAllVFX();
}

void Player::Update(float elapsedTime, Camera* camera)
{
    if (m_invincibilityTimer > 0.0f)
    {
        m_invincibilityTimer -= elapsedTime;
    }

    SetCamera(camera);
    if (isInputEnabled)
    {
        HandleAimInput(camera);
    }

    // Pull input direction from the new Controller component so legs face the correct way
    if (m_ownerNode)
    {
        if (auto* controller = m_ownerNode->GetComponent<PlayerControllerComponent>())
        {
            DirectX::XMFLOAT2 inputDir = controller->GetIntent().moveVector;
            if (inputDir.x != 0.0f || inputDir.y != 0.0f)
            {
                lastValidInput = inputDir;
            }
        }
    }

    if (m_debugState.forceAnimation && !animator->IsPlaying(m_debugState.animationName))
    {
        animator->Play(m_debugState.animationName, true, 0.2f);
    }

    if (animator) animator->Update(elapsedTime);

    if (!m_debugState.forceAnimation && m_activeWeaponType == WeaponType::Sword && !animator->IsUpperPlaying())
    {
        SetActiveWeapon(WeaponType::Crossbow);
        m_aimLocked = false;
    }

    float smoothedYaw = 0.0f;
    bool  shouldAim = false;
    float relativeAngle = 0.0f;

    UpdateFootRotation(elapsedTime, smoothedYaw);
    UpdateAimConstraint(elapsedTime, smoothedYaw, shouldAim, relativeAngle);

    if (m_debugState.disableAimConstraint)
    {
        shouldAim = false;
        relativeAngle = 0.0f;
    }

    ApplyWorldMatrix(smoothedYaw, shouldAim, relativeAngle);
    UpdateProjectiles(elapsedTime, camera);

    if (m_damageGlitchTimer > 0.0f)
    {
        m_damageGlitchTimer = (std::max)(0.0f, m_damageGlitchTimer - elapsedTime);
    }
}

void Player::HandleAimInput(Camera* camera)
{
    if (!camera) return;

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
            const float dirZ{ ry * invLength };

            const DirectX::XMFLOAT3 pPos{ movement->GetPosition() };
            constexpr float aimDistance{ 1000.0f };

            DirectX::XMFLOAT3 trueGamepadWorldPos{
                pPos.x + (dirX * aimDistance), pPos.y, pPos.z + (dirZ * aimDistance)
            };

            RotateModelToPoint(trueGamepadWorldPos);
        }
    }
    else
    {
        float mouseX, mouseY;
        SDL_GetMouseState(&mouseX, &mouseY);

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
            const float gunHeight{ movement->GetPosition().y + BulletSpawnY };
            const float t{ (gunHeight - origin.y) / dir.y };

            DirectX::XMFLOAT3 trueMouseWorldPos{
                origin.x + dir.x * t, gunHeight, origin.z + dir.z * t
            };

            RotateModelToPoint(trueMouseWorldPos);
        }
    }
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

    if ((dx * dx + dz * dz) > AimMinDistSq)
    {
        float aimYaw = atan2f(dx, dz);
        float diff = targetYaw - aimYaw;

        while (diff > XM_PI) diff -= XM_2PI;
        while (diff < -XM_PI) diff += XM_2PI;

        if (diff > XM_PIDIV2)
        {
            diff = XM_PI - diff;
            targetYaw = aimYaw + diff;
            m_isBackpedaling = true;
        }
        else if (diff < -XM_PIDIV2)
        {
            diff = -XM_PI - diff;
            targetYaw = aimYaw + diff;
            m_isBackpedaling = true;
        }

        while (targetYaw > XM_PI) targetYaw -= XM_2PI;
        while (targetYaw < -XM_PI) targetYaw += XM_2PI;
    }

    float angleDiff = targetYaw - currentYaw;
    while (angleDiff > XM_PI) angleDiff -= XM_2PI;
    while (angleDiff < -XM_PI) angleDiff += XM_2PI;

    float lerpFactor = min(RotSmoothSpeed * dt, 1.0f);
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

    if ((dx * dx + dz * dz) <= AimMinDistSq) return;

    outShouldAim = true;

    float absoluteAngleToMouse = atan2f(dx, dz);
    float relativeAngle = absoluteAngleToMouse - inOutSmoothedYaw;

    while (relativeAngle > DirectX::XM_PI) relativeAngle -= DirectX::XM_2PI;
    while (relativeAngle < -DirectX::XM_PI) relativeAngle += DirectX::XM_2PI;

    if (std::abs(relativeAngle) > MaxTorsoAngle)
    {
        float sign = (relativeAngle > 0.0f) ? 1.0f : -1.0f;
        relativeAngle = MaxTorsoAngle * sign;
        float targetFootYaw = absoluteAngleToMouse - relativeAngle;
        float diff = targetFootYaw - inOutSmoothedYaw;
        while (diff > DirectX::XM_PI) diff -= DirectX::XM_2PI;
        while (diff < -DirectX::XM_PI) diff += DirectX::XM_2PI;

        inOutSmoothedYaw += diff * (std::min)(RotSmoothSpeed * dt, 1.0f);
    }

    outRelativeAngle = relativeAngle;
}

void Player::ApplyWorldMatrix(float smoothedYaw, bool shouldAim, float relativeAngle)
{
    movement->SetRotationY(XMConvertToDegrees(smoothedYaw));

    XMFLOAT3 pos = movement->GetPosition();
    XMFLOAT3 rot = movement->GetRotation();

    XMMATRIX S = XMMatrixScaling(scale.x, scale.y, scale.z);
    XMMATRIX R = XMMatrixRotationRollPitchYaw(XMConvertToRadians(rot.x), XMConvertToRadians(rot.y), XMConvertToRadians(rot.z));
    XMMATRIX T = XMMatrixTranslation(pos.x, pos.y, pos.z);

    XMFLOAT4X4 worldMatrix;
    XMStoreFloat4x4(&worldMatrix, S * R * T);

    if (shouldAim && model)
    {
        int bodyIndex = model->GetNodeIndex("body");
        if (bodyIndex != -1)
        {
            Model::Node& bodyNode = model->GetNodes()[bodyIndex];
            XMMATRIX parentGlobal = XMMatrixIdentity();
            if (bodyNode.parent != nullptr) {
                parentGlobal = XMLoadFloat4x4(&bodyNode.parent->globalTransform);
            }

            XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
            XMMATRIX parentInverse = XMMatrixInverse(nullptr, parentGlobal);
            XMVECTOR localUpAxis = XMVector3TransformNormal(worldUp, parentInverse);
            localUpAxis = XMVector3Normalize(localUpAxis);

            XMMATRIX twistMatrix = XMMatrixRotationAxis(localUpAxis, relativeAngle);
            XMVECTOR currentLocalRot = XMLoadFloat4(&bodyNode.rotation);
            XMMATRIX localMatrix = XMMatrixRotationQuaternion(currentLocalRot);

            XMVECTOR finalRot = XMQuaternionRotationMatrix(localMatrix * twistMatrix);
            XMStoreFloat4(&bodyNode.rotation, finalRot);
        }
    }

    if (m_capeSimulator)
    {
        DirectX::XMFLOAT3 trueVelocity = { 0,0,0 };
        if (m_ownerNode)
        {
            // Sync cape with true component velocity
            if (auto* motor = m_ownerNode->GetComponent<CharacterMovementComponent>())
            {
                trueVelocity = motor->GetTotalVelocity();
            }
        }

        const float totalYaw{ smoothedYaw + relativeAngle };
        const float sinYaw{ std::sin(totalYaw) };
        const float cosYaw{ std::cos(totalYaw) };

        const float localVz{ (trueVelocity.x * sinYaw) + (trueVelocity.z * cosYaw) };
        const float localVx{ (trueVelocity.x * cosYaw) - (trueVelocity.z * sinYaw) };

        m_capeSimulator->Update(0.016f, { localVx, trueVelocity.y, localVz });
    }

    if (model) model->UpdateTransform(worldMatrix);
    if (animator) animator->SnapshotBones();

    DirectX::XMFLOAT4X4 attachMatrix = worldMatrix;
    if (m_rightHandBoneIndex != -1 && model->GetNodes().size() > m_rightHandBoneIndex)
    {
        attachMatrix = model->GetNodes()[m_rightHandBoneIndex].worldTransform;
    }

    for (const auto& weapon : m_weapons)
    {
        if (weapon) weapon->UpdateTransform(attachMatrix);
    }
}

void Player::UpdateProjectiles(float dt, Camera* camera)
{
    DirectX::XMFLOAT3 myPos = movement->GetPosition();
    constexpr float DESPAWN_DIST_SQ = 55.0f * 55.0f;

    for (auto& bullet : m_projectiles)
    {
        if (!bullet->IsActive()) continue;

        bullet->Update(dt, camera);
        DirectX::XMFLOAT3 bPos = bullet->GetMovement()->GetPosition();
        float dx = myPos.x - bPos.x;
        float dz = myPos.z - bPos.z;

        if ((dx * dx + dz * dz) > DESPAWN_DIST_SQ)
        {
            bullet->SetActive(false);
        }
    }
}

void Player::RotateModelToPoint(const DirectX::XMFLOAT3& targetPos)
{
    if (!m_aimLocked) { m_aimTarget = targetPos; }
}

void Player::FireProjectile()
{
    if (!isInputEnabled) return;

    const DirectX::XMFLOAT3 myPos{ movement->GetPosition() };
    const float dx{ m_aimTarget.x - myPos.x };
    const float dz{ m_aimTarget.z - myPos.z };
    const float angleToMouse{ std::atan2f(dx, dz) };
    const DirectX::XMFLOAT3 fwd{ std::sinf(angleToMouse), 0.0f, std::cosf(angleToMouse) };

    const DirectX::XMFLOAT3 spawnPos{
        myPos.x + fwd.x * BulletSpawnFwd,
        myPos.y + BulletSpawnY,
        myPos.z + fwd.z * BulletSpawnFwd
    };

    for (const auto& bullet : m_projectiles)
    {
        if (!bullet->IsActive())
        {
            bullet->Fire(spawnPos, fwd, BulletSpeed);
            bullet->SetDamage(m_bulletDamage);
            return;
        }
    }

    auto newBullet{ std::make_unique<Bullet>() };
    newBullet->Fire(spawnPos, fwd, BulletSpeed);
    newBullet->SetDamage(m_bulletDamage);
    m_projectiles.push_back(std::move(newBullet));

    for (int i = 0; i < MaxBullets; ++i) {
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

            DirectX::XMMATRIX S = DirectX::XMMatrixScaling(m_playerbulletOffsetScale.x, m_playerbulletOffsetScale.y, m_playerbulletOffsetScale.z);
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

            renderer->Draw(m_playerbulletModel, m_playerbulletColor, worldMatrix);
        }
        else
        {
            renderer->Draw(bullet->GetModel(), { 1.0f, 1.0f, 1.0f, 1.0f });
        }
    }
}

void Player::RenderWeapon(ModelRenderer* renderer)
{
    if (Weapon* activeWpn = GetActiveWeapon()) activeWpn->Render(renderer);
}

void Player::StopAllVFX()
{
    if (m_dashReadyVfxHandle != -1) {
        EffectManager::Instance().Stop(m_dashReadyVfxHandle);
        m_dashReadyVfxHandle = -1;
    }
    if (m_dashStandbyVfxHandle != -1) {
        EffectManager::Instance().Stop(m_dashStandbyVfxHandle);
        m_dashStandbyVfxHandle = -1;
    }
}

void Player::TakeDamage(float damage)
{
    if (m_hp <= 0.0f || IsInvincible()) return;

    m_hp -= damage;
    if (m_enableIFrames) TriggerInvincibility(m_iFrameDuration);
    m_damageGlitchTimer = DAMAGE_GLITCH_DURATION;

    if (m_hp <= 0.0f)
    {
        m_hp = 0.0f;
        StopAllVFX();
    }
}

float Player::GetDamageGlitchIntensity() const noexcept
{
    if (m_damageGlitchTimer <= 0.0f) return 0.0f;
    const float t{ m_damageGlitchTimer / DAMAGE_GLITCH_DURATION };
    return DAMAGE_GLITCH_MAX_INTENSITY * (t * t);
}

void Player::SetPosition(const DirectX::XMFLOAT3& pos) noexcept
{
    if (movement) movement->SetPosition(pos);
}