#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <PxPhysicsAPI.h>
#include "System/CollisionLayer.h"
#include "System/Graphics.h"
#include "System/PhysicsManager.h"
#include "System/ShapeRenderer.h"
#include "CameraController.h"
#include "ComponentRegistry.h"
#include "GameObject.h"
#include "OrbitCameraDriverComponent.h"
#include "VirtualCameraComponent.h"

namespace
{
    [[nodiscard]] GameObject* FindChildRecursive(GameObject* current, const std::string& name) noexcept
    {
        if (!current) return nullptr;
        if (current->GetName() == name) return current;

        for (const auto& child : current->GetChildren())
        {
            if (auto* found = FindChildRecursive(child.get(), name))
            {
                return found;
            }
        }
        return nullptr;
    }

    [[nodiscard]] constexpr float WrapAngle(float angle) noexcept
    {
        while (angle > DirectX::XM_PI)  angle -= DirectX::XM_2PI;
        while (angle < -DirectX::XM_PI) angle += DirectX::XM_2PI;
        return angle;
    }

    [[nodiscard]] inline float CalculateDampingBlend(float damping, float dt) noexcept
    {
        constexpr float epsilon{ 0.001f };
        if (damping <= epsilon) return 1.0f;
        return 1.0f - std::exp(-damping * dt);
    }

    [[nodiscard]] inline bool IsFloat3Equal(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, float ep = 0.0001f) noexcept
    {
        return (std::abs(a.x - b.x) <= ep) && (std::abs(a.y - b.y) <= ep) && (std::abs(a.z - b.z) <= ep);
    }
}

VirtualCameraComponent::VirtualCameraComponent()
{
    s_registry.push_back(this);
    s_globalDirtyFrame++; // Notify the Camera Brain that a new camera was created

    // Locomotion (Default baseline)
    auto& locomotion{ m_profiles[static_cast<std::size_t>(CameraProfileType::locomotion)] };
    locomotion.orbitDistance = 3.3f;
    locomotion.fovDegrees = 48.0f;
    locomotion.pivotOffset = { 0.0f, 1.15f, 0.0f };
    locomotion.shoulderOffset = 0.5f;
    locomotion.rollDegrees = 0.0f;
    locomotion.positionDamping = 5.0f;
    locomotion.rotationDamping = 20.0f;
    locomotion.blendSpeed = 6.0f;

    // Sprint (Close, low, expanded lens)
    auto& sprint{ m_profiles[static_cast<std::size_t>(CameraProfileType::sprint)] };
    sprint.orbitDistance = 2.6f;
    sprint.fovDegrees = 54.0f;
    sprint.pivotOffset = { 0.0f, 0.95f, 0.0f };
    sprint.shoulderOffset = 0.5f;
    sprint.rollDegrees = 0.0f;
    sprint.positionDamping = 7.0f;
    sprint.rotationDamping = 20.0f;
    sprint.blendSpeed = 5.0f;

    // Dash (Lazy trailing inertia pull-out)
    auto& dash{ m_profiles[static_cast<std::size_t>(CameraProfileType::dash)] };
    dash.orbitDistance = 3.6f;
    dash.fovDegrees = 52.0f;
    dash.pivotOffset = { 0.0f, 1.15f, 0.0f };
    dash.shoulderOffset = 0.5f;
    dash.rollDegrees = 0.0f;
    dash.positionDamping = 3.0f; // Loose damping enables realistic drag-out
    dash.rotationDamping = 15.0f;
    dash.blendSpeed = 8.0f;

    // Slide (Ground-skimming intimacy)
    auto& slide{ m_profiles[static_cast<std::size_t>(CameraProfileType::slide)] };
    slide.orbitDistance = 2.3f;
    slide.fovDegrees = 56.0f;
    slide.pivotOffset = { 0.0f, 0.45f, 0.0f };
    slide.shoulderOffset = 0.4f;
    slide.rollDegrees = 0.0f;
    slide.positionDamping = 9.0f;
    slide.rotationDamping = 20.0f;
    slide.blendSpeed = 7.0f;

    // Wall Run (Outward lane bias with Dutch tilt roll)
    auto& wallRun{ m_profiles[static_cast<std::size_t>(CameraProfileType::wall_run)] };
    wallRun.orbitDistance = 3.1f;
    wallRun.fovDegrees = 52.0f;
    wallRun.pivotOffset = { 0.0f, 1.20f, 0.0f };
    wallRun.shoulderOffset = 0.8f;
    wallRun.rollDegrees = 12.0f;
    wallRun.positionDamping = 6.0f;
    wallRun.rotationDamping = 15.0f;
    wallRun.blendSpeed = 6.0f;

    // Roll / Landing (Impact dip and rebound)
    auto& rollLanding{ m_profiles[static_cast<std::size_t>(CameraProfileType::roll_landing)] };
    rollLanding.orbitDistance = 2.7f;
    rollLanding.fovDegrees = 50.0f;
    rollLanding.pivotOffset = { 0.0f, 0.65f, 0.0f };
    rollLanding.shoulderOffset = 0.45f;
    rollLanding.rollDegrees = 0.0f;
    rollLanding.positionDamping = 8.0f;
    rollLanding.rotationDamping = 20.0f;
    rollLanding.blendSpeed = 8.0f;

    m_currentProfile = locomotion;
}

VirtualCameraComponent::~VirtualCameraComponent()
{
    s_registry.erase(std::remove(s_registry.begin(), s_registry.end(), this), s_registry.end());
    s_globalDirtyFrame++; // Notify the Camera Brain if a camera was deleted
}

void VirtualCameraComponent::OnAttach(GameObject* owner) noexcept
{
    IComponent::OnAttach(owner);
    EnsureSharedGizmoLoaded();
    ResolveTargets();

    if (owner)
    {
        m_cachedPos = owner->GetPosition();
        m_cachedRot = owner->GetRotation();
    }

    m_isDirty = true;
    s_globalDirtyFrame++; // Trigger immediate sync once GameObject transform is attached
}

void VirtualCameraComponent::EnsureSharedGizmoLoaded() noexcept
{
    if (!s_sharedGizmoSprite)
    {
        s_sharedGizmoSprite = std::make_shared<Sprite>(Graphics::Instance().GetDevice(), "Data/Icon/Gizmo/Camera.png");
    }
}

GameObject* VirtualCameraComponent::FindTargetByName(const std::string& name) const noexcept
{
    if (name.empty() || !m_owner) return nullptr;

    GameObject* root{ m_owner };
    while (root->GetParent() != nullptr)
    {
        root = root->GetParent();
    }

    return FindChildRecursive(root, name);
}

void VirtualCameraComponent::ResolveTargets() noexcept
{
    m_followTarget = FindTargetByName(m_followTargetName);
    m_lookAtTarget = FindTargetByName(m_lookAtTargetName);
}

void VirtualCameraComponent::SetOrbitYaw(float yawRadians) noexcept
{
    // Reuses the same wraparound guard already used for look-at yaw, so orbit yaw
    // never accumulates past +-pi and loses precision over a long play session.
    m_orbitYaw = WrapAngle(yawRadians);
}

void VirtualCameraComponent::AddOrbitYaw(float deltaRadians) noexcept
{
    SetOrbitYaw(m_orbitYaw + deltaRadians);
}

void VirtualCameraComponent::SetOrbitPitch(float pitchRadians) noexcept
{
    // Pitch is clamped, not wrapped: looking straight up/down and continuing past it
    // should stop, not flip the camera through the pole.
    m_orbitPitch = std::clamp(pitchRadians, m_orbitMinPitch, m_orbitMaxPitch);
}

void VirtualCameraComponent::SetTargetProfile(const CameraProfileType type, const int wallSide) noexcept
{
    m_targetProfileType = type;
    m_wallSide = wallSide;
}

VirtualCameraComponent::OrbitTransformResult VirtualCameraComponent::ResolveOrbitTransform() const noexcept
{
    // Spherical-to-Cartesian conversion matching editor convention
    const float sx{ std::sin(m_orbitPitch) };
    const float cx{ std::cos(m_orbitPitch) };
    const float sy{ std::sin(m_orbitYaw) };
    const float cy{ std::cos(m_orbitYaw) };

    const DirectX::XMVECTOR vFront{ DirectX::XMVectorSet(-cx * sy, -sx, -cx * cy, 0.0f) };
    const DirectX::XMVECTOR vRight{ DirectX::XMVectorSet(cy, 0.0f, -sy, 0.0f) };
    const DirectX::XMVECTOR vCamFwdH{ DirectX::XMVectorSet(-sy, 0.0f, -cy, 0.0f) };

    // Resolve pivot using horizontal camera-relative basis:
    // Y is world/character up, X is camera right, Z is camera forward.
    // This prevents depth/zoom inversion when rotating 180 degrees.
    const DirectX::XMFLOAT3 targetPos{ m_followTarget->GetPosition() };
    const DirectX::XMVECTOR vTargetPos{ DirectX::XMLoadFloat3(&targetPos) };

    DirectX::XMVECTOR vPivot{ DirectX::XMVectorAdd(
        vTargetPos,
        DirectX::XMVectorSet(0.0f, m_currentProfile.pivotOffset.y, 0.0f, 0.0f)
    ) };
    vPivot = DirectX::XMVectorAdd(vPivot, DirectX::XMVectorScale(vRight, m_currentProfile.pivotOffset.x));
    vPivot = DirectX::XMVectorAdd(vPivot, DirectX::XMVectorScale(vCamFwdH, m_currentProfile.pivotOffset.z));

    DirectX::XMFLOAT3 pivotPos{};
    DirectX::XMStoreFloat3(&pivotPos, vPivot);

    // Push eye back by dynamically damped distance, then shift laterally along vRight by shoulder offset
    DirectX::XMVECTOR vEye{ DirectX::XMVectorSubtract(vPivot, DirectX::XMVectorScale(vFront, m_currentProfile.orbitDistance)) };
    vEye = DirectX::XMVectorAdd(vEye, DirectX::XMVectorScale(vRight, m_currentProfile.shoulderOffset));

    DirectX::XMFLOAT3 eyePos{};
    DirectX::XMStoreFloat3(&eyePos, vEye);

    const DirectX::XMVECTOR vLookDir{ DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(vPivot, vEye)) };
    DirectX::XMFLOAT3 lookDir{};
    DirectX::XMStoreFloat3(&lookDir, vLookDir);

    const float horizontalDist{ std::sqrt((lookDir.x * lookDir.x) + (lookDir.z * lookDir.z)) };
    const DirectX::XMFLOAT3 rotationRadians{
        std::atan2(-lookDir.y, horizontalDist),                         // pitch
        std::atan2(lookDir.x, lookDir.z),                               // yaw
        DirectX::XMConvertToRadians(m_currentProfile.rollDegrees)        // roll (Dutch tilt)
    };

    return { eyePos, rotationRadians, pivotPos };
}

void VirtualCameraComponent::AddOrbitPitch(float deltaRadians) noexcept
{
    SetOrbitPitch(m_orbitPitch + deltaRadians);
}

void VirtualCameraComponent::SetOrbitDistance(float distance) noexcept
{
    m_orbitDistance = std::clamp(distance, m_orbitMinDistance, m_orbitMaxDistance);
}

void VirtualCameraComponent::AddOrbitDistance(float delta) noexcept
{
    SetOrbitDistance(m_orbitDistance + delta);
}

void VirtualCameraComponent::Update(float dt)
{
    m_hasSteppedThisFrame = false;
}

void VirtualCameraComponent::LateUpdate(float dt)
{
    Step(dt);
}

void VirtualCameraComponent::Step(float dt)
{
    if (m_hasSteppedThisFrame) return;
    m_hasSteppedThisFrame = true;

    if (!m_owner) return;

    // Lazy resolve targets once connected to a hierarchy
    if (!m_followTarget && !m_followTargetName.empty()) ResolveTargets();
    if (!m_lookAtTarget && !m_lookAtTargetName.empty()) ResolveTargets();

    if (m_followTarget && m_followTarget->IsDestroyed()) m_followTarget = nullptr;
    if (m_lookAtTarget && m_lookAtTarget->IsDestroyed()) m_lookAtTarget = nullptr;

    // Detect if an external tool (ImGuizmo / Transform Inspector) moved the GameObject
    const DirectX::XMFLOAT3 ownerPos{ m_owner->GetPosition() };
    const DirectX::XMFLOAT3 ownerRot{ m_owner->GetRotation() };
    if (!IsFloat3Equal(ownerPos, m_cachedPos) || !IsFloat3Equal(ownerRot, m_cachedRot))
    {
        m_cachedPos = ownerPos;
        m_cachedRot = ownerRot;
        s_globalDirtyFrame++;
    }

    // Freeze rule: exit only if paused, not dirty, and no target tracking is active
    if (dt <= 0.0001f && !m_isDirty) return;

    DirectX::XMFLOAT3 currentPos{ ownerPos };
    DirectX::XMFLOAT3 currentRot{ ownerRot };

    if (m_orbitEnabled && m_followTarget)
    {
        // Continuous profile state damping toward current active preset
        const auto targetIndex{ static_cast<std::size_t>(m_targetProfileType) };
        const CameraProfile& targetPreset{ m_profiles[targetIndex] };
        CameraProfile target{ targetPreset };

        if (m_targetProfileType == CameraProfileType::wall_run && m_wallSide != 0)
        {
            // Wall on left (-1): roll tilts left (-12 deg), shoulder moves right (+0.8m)
            // Wall on right (+1): roll tilts right (+12 deg), shoulder moves left (-0.8m)
            const float sideScalar{ static_cast<float>(m_wallSide) };
            target.rollDegrees = targetPreset.rollDegrees * sideScalar;
            target.shoulderOffset = targetPreset.shoulderOffset * -sideScalar;
        }

        if (m_isDirty || dt <= 0.0001f)
        {
            m_currentProfile = target;
        }
        else
        {
            const float blendAlpha{ CalculateDampingBlend(target.blendSpeed, dt) };
            m_currentProfile.orbitDistance += (target.orbitDistance - m_currentProfile.orbitDistance) * blendAlpha;
            m_currentProfile.fovDegrees += (target.fovDegrees - m_currentProfile.fovDegrees) * blendAlpha;
            m_currentProfile.pivotOffset.x += (target.pivotOffset.x - m_currentProfile.pivotOffset.x) * blendAlpha;
            m_currentProfile.pivotOffset.y += (target.pivotOffset.y - m_currentProfile.pivotOffset.y) * blendAlpha;
            m_currentProfile.pivotOffset.z += (target.pivotOffset.z - m_currentProfile.pivotOffset.z) * blendAlpha;
            m_currentProfile.shoulderOffset += (target.shoulderOffset - m_currentProfile.shoulderOffset) * blendAlpha;

            const float currentRollRad{ DirectX::XMConvertToRadians(m_currentProfile.rollDegrees) };
            const float targetRollRad{ DirectX::XMConvertToRadians(target.rollDegrees) };
            const float rollDiff{ WrapAngle(targetRollRad - currentRollRad) };
            m_currentProfile.rollDegrees = DirectX::XMConvertToDegrees(currentRollRad + (rollDiff * blendAlpha));

            m_currentProfile.positionDamping += (target.positionDamping - m_currentProfile.positionDamping) * blendAlpha;
            m_currentProfile.rotationDamping += (target.rotationDamping - m_currentProfile.rotationDamping) * blendAlpha;
        }

        // Mirror blended values into component state for external readers (e.g. Camera Brain FOV synchronization)
        m_fovDegrees = m_currentProfile.fovDegrees;
        m_orbitDistance = m_currentProfile.orbitDistance;
        m_orbitPivotOffset = m_currentProfile.pivotOffset;
        m_orbitShoulderOffset = m_currentProfile.shoulderOffset;
        m_orbitPositionDamping = m_currentProfile.positionDamping;
        m_orbitRotationDamping = m_currentProfile.rotationDamping;

        const OrbitTransformResult orbitResult{ ResolveOrbitTransform() };
        DirectX::XMFLOAT3 desiredPos = orbitResult.eyePos;
        const DirectX::XMFLOAT3 desiredRotRad = orbitResult.rotationRadians;
        const DirectX::XMFLOAT3 pivot = orbitResult.pivotPos;

        // Camera collision: if enabled, perform a sphere cast from the pivot to the desired eye position
        if (m_cameraCollisionEnabled)
        {
            const DirectX::XMVECTOR vPivot{ DirectX::XMLoadFloat3(&pivot) };
            DirectX::XMVECTOR vDesired{ DirectX::XMLoadFloat3(&desiredPos) };
            const DirectX::XMVECTOR vDir{ DirectX::XMVectorSubtract(vDesired, vPivot) };
            const DirectX::XMVECTOR vLength{ DirectX::XMVector3Length(vDir) };

            float maxDistance = 0.0f;
            DirectX::XMStoreFloat(&maxDistance, vLength);

            if (maxDistance > 0.001f)
            {
                const DirectX::XMVECTOR vDirNorm{ DirectX::XMVectorScale(vDir, 1.0f / maxDistance) };
                DirectX::XMFLOAT3 dir{};
                DirectX::XMStoreFloat3(&dir, vDirNorm);

                float hitDistance = maxDistance;
                bool hit = false;

                auto* scene = PhysicsManager::Instance().GetScene();
                if (scene)
                {
                    const physx::PxSphereGeometry sphereGeom(m_cameraCollisionRadius);
                    const physx::PxTransform startPose(physx::PxVec3(pivot.x, pivot.y, pivot.z));
                    const physx::PxVec3 sweepDir(dir.x, dir.y, dir.z);

                    physx::PxSweepBuffer hitBuffer;

                    physx::PxQueryFilterData filterData;
                    filterData.data.word0 = CollisionLayer::WorldStatic | CollisionLayer::WorldDynamic;
                    filterData.flags = physx::PxQueryFlag::eSTATIC | physx::PxQueryFlag::eDYNAMIC;

                    if (scene->sweep(sphereGeom, startPose, sweepDir, maxDistance, hitBuffer, physx::PxHitFlag::eDEFAULT, filterData))
                    {
                        hit = true;
                        hitDistance = hitBuffer.block.distance;
                    }
                }

                if (hit)
                {
                    hitDistance = (std::max)(0.0f, hitDistance - 0.05f);
                    vDesired = DirectX::XMVectorAdd(vPivot, DirectX::XMVectorScale(vDirNorm, hitDistance));
                    DirectX::XMStoreFloat3(&desiredPos, vDesired);
                }
            }
        }

        // Position: exponential damping toward the orbit-resolved eye position
        const float tPos{ m_isDirty ? 1.0f : CalculateDampingBlend(m_currentProfile.positionDamping, dt) };
        currentPos.x += (desiredPos.x - currentPos.x) * tPos;
        currentPos.y += (desiredPos.y - currentPos.y) * tPos;
        currentPos.z += (desiredPos.z - currentPos.z) * tPos;

        // Rotation: damp Pitch, Yaw, and Roll in wrapped radian space
        const float currentPitchRad{ DirectX::XMConvertToRadians(currentRot.x) };
        const float currentYawRad{ DirectX::XMConvertToRadians(currentRot.y) };
        const float currentRollRad{ DirectX::XMConvertToRadians(currentRot.z) };

        const float pitchDiff{ WrapAngle(desiredRotRad.x - currentPitchRad) };
        const float yawDiff{ WrapAngle(desiredRotRad.y - currentYawRad) };
        const float rollDiff{ WrapAngle(desiredRotRad.z - currentRollRad) };

        const float tRot{ m_isDirty ? 1.0f : CalculateDampingBlend(m_currentProfile.rotationDamping, dt) };

        currentRot.x = DirectX::XMConvertToDegrees(currentPitchRad + (pitchDiff * tRot));
        currentRot.y = DirectX::XMConvertToDegrees(currentYawRad + (yawDiff * tRot));
        currentRot.z = DirectX::XMConvertToDegrees(currentRollRad + (rollDiff * tRot));
    }
    else
    {
        if (m_followTarget)
        {
            const DirectX::XMFLOAT3 targetPos{ m_followTarget->GetPosition() };
            const DirectX::XMFLOAT3 desiredPos{
                targetPos.x + m_followOffset.x,
                targetPos.y + m_followOffset.y,
                targetPos.z + m_followOffset.z
            };

            const float tPos{ m_isDirty ? 1.0f : CalculateDampingBlend(m_positionDamping, dt) };
            currentPos.x += (desiredPos.x - currentPos.x) * tPos;
            currentPos.y += (desiredPos.y - currentPos.y) * tPos;
            currentPos.z += (desiredPos.z - currentPos.z) * tPos;
        }

        if (m_lookAtTarget)
        {
            const DirectX::XMFLOAT3 targetPos{ m_lookAtTarget->GetPosition() };
            const float dx{ targetPos.x - currentPos.x };
            const float dy{ targetPos.y - currentPos.y };
            const float dz{ targetPos.z - currentPos.z };
            const float horizontalDist{ std::sqrt((dx * dx) + (dz * dz)) };

            const float desiredPitch{ std::atan2(-dy, horizontalDist) };
            const float desiredYaw{ std::atan2(dx, dz) };

            const float currentPitchRad{ DirectX::XMConvertToRadians(currentRot.x) };
            const float currentYawRad{ DirectX::XMConvertToRadians(currentRot.y) };

            const float pitchDiff{ WrapAngle(desiredPitch - currentPitchRad) };
            const float yawDiff{ WrapAngle(desiredYaw - currentYawRad) };

            const float tRot{ m_isDirty ? 1.0f : CalculateDampingBlend(m_rotationDamping, dt) };

            currentRot.x = DirectX::XMConvertToDegrees(currentPitchRad + (pitchDiff * tRot));
            currentRot.y = DirectX::XMConvertToDegrees(currentYawRad + (yawDiff * tRot));
            currentRot.z = 0.0f;
        }
    }

    if (!IsFloat3Equal(currentPos, m_cachedPos) || m_isDirty)
    {
        m_owner->SetPosition(currentPos);
        m_cachedPos = currentPos;
    }
    if (!IsFloat3Equal(currentRot, m_cachedRot) || m_isDirty)
    {
        m_owner->SetRotation(currentRot);
        m_cachedRot = currentRot;
    }

    m_isDirty = false;
}

void VirtualCameraComponent::DrawInspector()
{
    ImGui::DragInt("Priority", &m_priority, 1, 0, 999);
    ImGui::Separator();

    // Dynamically check if the Orbit Driver is attached to this exact GameObject
    const bool hasOrbitDriver = m_owner->GetComponent<OrbitCameraDriverComponent>() != nullptr;

    ImGui::TextDisabled("TARGET SETTINGS");
    static char s_followBuf[64];
    strncpy_s(s_followBuf, sizeof(s_followBuf), m_followTargetName.c_str(), _TRUNCATE);
    if (ImGui::InputText("Follow Target", s_followBuf, sizeof(s_followBuf)))
    {
        m_followTargetName = s_followBuf;
        ResolveTargets();
        m_isDirty = true;
        s_globalDirtyFrame++;
        ForceSync();
    }

    // Standard Look-At is ignored if the Orbit Rig is steering the rotation
    if (hasOrbitDriver) ImGui::BeginDisabled();
    static char s_lookBuf[64];
    strncpy_s(s_lookBuf, sizeof(s_lookBuf), m_lookAtTargetName.c_str(), _TRUNCATE);
    if (ImGui::InputText("Look-At Target", s_lookBuf, sizeof(s_lookBuf)))
    {
        m_lookAtTargetName = s_lookBuf;
        ResolveTargets();
        m_isDirty = true;
        s_globalDirtyFrame++;
        ForceSync();
    }
    if (hasOrbitDriver) ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::Separator();

    if (hasOrbitDriver)
    {
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "ORBIT RIG LIMITS & COLLISION");
        if (ImGui::DragFloat("Min Pitch", &m_orbitMinPitch, 0.01f, -DirectX::XM_PI, 0.0f)) m_isDirty = true;
        if (ImGui::DragFloat("Max Pitch", &m_orbitMaxPitch, 0.01f, 0.0f, DirectX::XM_PI)) m_isDirty = true;
        if (ImGui::DragFloat("Min Distance", &m_orbitMinDistance, 0.1f, 0.5f, m_orbitMaxDistance)) m_isDirty = true;
        if (ImGui::DragFloat("Max Distance", &m_orbitMaxDistance, 0.1f, m_orbitMinDistance, 50.0f)) m_isDirty = true;

        ImGui::Spacing();
        if (ImGui::Checkbox("Enable Collision", &m_cameraCollisionEnabled)) m_isDirty = true;
        ImGui::BeginDisabled(!m_cameraCollisionEnabled);
        if (ImGui::DragFloat("Collision Radius", &m_cameraCollisionRadius, 0.01f, 0.01f, 1.0f)) m_isDirty = true;
        ImGui::EndDisabled();

        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "CAMERA PROFILES (TUNING PRESETS)");

        static constexpr const char* s_profileNames[] = {
            "Locomotion (Default)",
            "Sprint",
            "Dash",
            "Slide",
            "Wall Run",
            "Roll / Landing"
        };

        int activeIdx = static_cast<int>(m_targetProfileType);
        if (ImGui::Combo("Active Preview", &activeIdx, s_profileNames, IM_ARRAYSIZE(s_profileNames)))
        {
            SetTargetProfile(static_cast<CameraProfileType>(activeIdx));
            m_isDirty = true;
            s_globalDirtyFrame++;
        }

        for (std::size_t i{ 0 }; i < m_profiles.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));
            auto& p = m_profiles[i];

            if (ImGui::TreeNode(s_profileNames[i]))
            {
                if (ImGui::DragFloat("Orbit Distance", &p.orbitDistance, 0.05f, m_orbitMinDistance, m_orbitMaxDistance, "%.2f m")) m_isDirty = true;
                if (ImGui::DragFloat("Field of View", &p.fovDegrees, 0.1f, 10.0f, 160.0f, "%.1f deg")) m_isDirty = true;
                if (ImGui::DragFloat3("Pivot Offset", &p.pivotOffset.x, 0.02f, -10.0f, 10.0f, "%.2f")) m_isDirty = true;
                if (ImGui::DragFloat("Shoulder Offset", &p.shoulderOffset, 0.02f, -5.0f, 5.0f, "%.2f m")) m_isDirty = true;
                if (ImGui::DragFloat("Dutch Tilt (Roll)", &p.rollDegrees, 0.1f, -89.0f, 89.0f, "%.1f deg")) m_isDirty = true;
                if (ImGui::DragFloat("Position Damping", &p.positionDamping, 0.1f, 0.0f, 50.0f, "%.1f")) m_isDirty = true;
                if (ImGui::DragFloat("Rotation Damping", &p.rotationDamping, 0.1f, 0.0f, 50.0f, "%.1f")) m_isDirty = true;
                if (ImGui::DragFloat("Blend Speed", &p.blendSpeed, 0.1f, 0.1f, 30.0f, "%.1f (1/s)")) m_isDirty = true;

                ImGui::TreePop();
            }
            ImGui::PopID();
        }

        if (m_isDirty) s_globalDirtyFrame++;
    }
    else
    {
        ImGui::TextDisabled("STANDARD FOLLOW SETTINGS");
        if (ImGui::DragFloat3("Follow Offset", &m_followOffset.x, 0.1f)) { m_isDirty = true; s_globalDirtyFrame++; ForceSync(); }
        if (ImGui::DragFloat("Position Damping", &m_positionDamping, 0.1f, 0.0f, 50.0f)) { m_isDirty = true; s_globalDirtyFrame++; }
        if (ImGui::DragFloat("Rotation Damping", &m_rotationDamping, 0.1f, 0.0f, 50.0f)) { m_isDirty = true; s_globalDirtyFrame++; }
    }

    ImGui::Spacing();
    ImGui::Separator();

    ImGui::TextDisabled("LENS SETTINGS");
    if (ImGui::DragFloat("Field of View", &m_fovDegrees, 0.1f, 1.0f, 179.0f)) { m_isDirty = true; s_globalDirtyFrame++; }
    if (ImGui::DragFloat("Near Clip", &m_nearZ, 0.01f, 0.01f, m_farZ - 0.01f)) { m_isDirty = true; s_globalDirtyFrame++; }
    if (ImGui::DragFloat("Far Clip", &m_farZ, 1.0f, m_nearZ + 0.01f, 100000.0f)) { m_isDirty = true; s_globalDirtyFrame++; }

    ImGui::Separator();
    ImGui::DragFloat("Gizmo Draw Distance", &m_gizmoDrawDistance, 0.1f, 0.5f, 100.0f);
}

void VirtualCameraComponent::DrawGizmo(const GizmoContext& ctx) noexcept
{
    if (!(ctx.categoryMask & static_cast<std::uint32_t>(GizmoCategory::Cameras))) return;

    if (!ctx.shapes || !m_owner) return;

    if (m_isActiveShot) return;

    const DirectX::XMFLOAT3 pos{ m_owner->GetPosition() };
    const DirectX::XMFLOAT3 rot{ m_owner->GetRotation() };

    if (ctx.activeCamera)
    {
        const DirectX::XMFLOAT3 camPos = ctx.activeCamera->GetPosition();
        const float dx = pos.x - camPos.x;
        const float dy = pos.y - camPos.y;
        const float dz = pos.z - camPos.z;
        if ((dx * dx + dy * dy + dz * dz) < 0.01f) return;
    }

    const DirectX::XMFLOAT3 rotRad{
        DirectX::XMConvertToRadians(rot.x),
        DirectX::XMConvertToRadians(rot.y),
        DirectX::XMConvertToRadians(rot.z)
    };

    const float currentAspect{ ctx.activeCamera ? ctx.activeCamera->GetAspectRatio() : (16.0f / 9.0f) };

    ctx.shapes->DrawFrustum(
        pos,
        rotRad,
        DirectX::XMConvertToRadians(m_fovDegrees),
        currentAspect,
        m_nearZ,
        m_farZ,
        { 0.2f, 0.8f, 1.0f, 1.0f },
        m_gizmoDrawDistance
    );

    if (ctx.activeCamera && ctx.activeCamera->CheckSphere(pos.x, pos.y, pos.z, 0.5f))
    {
        const DirectX::XMFLOAT3 activeCamRot{ ctx.activeCamera->GetRotation() };

        s_gizmoBatchData.push_back({
            pos.x, pos.y, pos.z,
            0.5f, 0.5f,
            0.0f, 0.0f, 0.0f, 0.0f,
            activeCamRot.x, activeCamRot.y, activeCamRot.z,
            0.2f, 0.8f, 1.0f, 1.0f
            });
    }
}

void VirtualCameraComponent::ForceSync() noexcept
{
    if (!m_owner) return;
    ResolveTargets();

    if (m_followTarget && m_followTarget->IsDestroyed()) m_followTarget = nullptr;
    if (m_lookAtTarget && m_lookAtTarget->IsDestroyed()) m_lookAtTarget = nullptr;

    DirectX::XMFLOAT3 currentPos{ m_owner->GetPosition() };
    DirectX::XMFLOAT3 currentRot{ m_owner->GetRotation() };

    if (m_followTarget)
    {
        const DirectX::XMFLOAT3 targetPos{ m_followTarget->GetPosition() };
        currentPos = {
            targetPos.x + m_followOffset.x,
            targetPos.y + m_followOffset.y,
            targetPos.z + m_followOffset.z
        };
    }

    if (m_lookAtTarget)
    {
        const DirectX::XMFLOAT3 targetPos{ m_lookAtTarget->GetPosition() };
        const float dx{ targetPos.x - currentPos.x };
        const float dy{ targetPos.y - currentPos.y };
        const float dz{ targetPos.z - currentPos.z };
        const float horizontalDist{ std::sqrt((dx * dx) + (dz * dz)) };

        const float desiredPitch{ std::atan2(-dy, horizontalDist) };
        const float desiredYaw{ std::atan2(dx, dz) };

        currentRot.x = DirectX::XMConvertToDegrees(desiredPitch);
        currentRot.y = DirectX::XMConvertToDegrees(desiredYaw);
        currentRot.z = m_currentProfile.rollDegrees;
    }
    m_owner->SetPosition(currentPos);
    m_owner->SetRotation(currentRot);
    m_cachedPos = currentPos;
    m_cachedRot = currentRot;
}

void VirtualCameraComponent::FlushGizmos(ID3D11DeviceContext* dc, const Camera* activeCam) noexcept
{
    if (s_gizmoBatchData.empty()) return;

    if (s_sharedGizmoSprite)
    {
        s_sharedGizmoSprite->Render3DBatch(dc, activeCam, s_gizmoBatchData);
    }

    s_gizmoBatchData.clear();
}

void VirtualCameraComponent::Serialize(nlohmann::json& outJson) const
{
    outJson["Priority"] = m_priority;
    outJson["FollowTarget"] = m_followTargetName;
    outJson["LookAtTarget"] = m_lookAtTargetName;
    outJson["FollowOffsetX"] = m_followOffset.x;
    outJson["FollowOffsetY"] = m_followOffset.y;
    outJson["FollowOffsetZ"] = m_followOffset.z;
    outJson["PositionDamping"] = m_positionDamping;
    outJson["RotationDamping"] = m_rotationDamping;
    outJson["FovDegrees"] = m_fovDegrees;
    outJson["NearZ"] = m_nearZ;
    outJson["FarZ"] = m_farZ;
    outJson["OrbitDistance"] = m_orbitDistance;
    outJson["OrbitPivotOffsetX"] = m_orbitPivotOffset.x;
    outJson["OrbitPivotOffsetY"] = m_orbitPivotOffset.y;
    outJson["OrbitPivotOffsetZ"] = m_orbitPivotOffset.z;
    outJson["OrbitShoulderOffset"] = m_orbitShoulderOffset;
    outJson["OrbitMinPitch"] = m_orbitMinPitch;
    outJson["OrbitMaxPitch"] = m_orbitMaxPitch;
    outJson["CameraCollisionEnabled"] = m_cameraCollisionEnabled;
    outJson["CameraCollisionRadius"] = m_cameraCollisionRadius;

    // Serialize tuning presets array
    nlohmann::json profilesArray = nlohmann::json::array();
    for (std::size_t i{ 0 }; i < m_profiles.size(); ++i)
    {
        const auto& p{ m_profiles[i] };
        nlohmann::json pJson{};
        pJson["Distance"] = p.orbitDistance;
        pJson["Fov"] = p.fovDegrees;
        pJson["PivotX"] = p.pivotOffset.x;
        pJson["PivotY"] = p.pivotOffset.y;
        pJson["PivotZ"] = p.pivotOffset.z;
        pJson["Shoulder"] = p.shoulderOffset;
        pJson["Roll"] = p.rollDegrees;
        pJson["PosDamping"] = p.positionDamping;
        pJson["RotDamping"] = p.rotationDamping;
        pJson["BlendSpeed"] = p.blendSpeed;
        profilesArray.push_back(std::move(pJson));
    }
    outJson["Profiles"] = std::move(profilesArray);
}

void VirtualCameraComponent::Deserialize(const nlohmann::json& inJson)
{
    m_priority = inJson.value("Priority", m_priority);
    m_followTargetName = inJson.value("FollowTarget", m_followTargetName);
    m_lookAtTargetName = inJson.value("LookAtTarget", m_lookAtTargetName);
    m_followOffset.x = inJson.value("FollowOffsetX", m_followOffset.x);
    m_followOffset.y = inJson.value("FollowOffsetY", m_followOffset.y);
    m_followOffset.z = inJson.value("FollowOffsetZ", m_followOffset.z);
    m_positionDamping = inJson.value("PositionDamping", m_positionDamping);
    m_rotationDamping = inJson.value("RotationDamping", m_rotationDamping);
    m_fovDegrees = inJson.value("FovDegrees", m_fovDegrees);
    m_nearZ = inJson.value("NearZ", m_nearZ);
    m_farZ = inJson.value("FarZ", m_farZ);
    m_orbitDistance = inJson.value("OrbitDistance", m_orbitDistance);
    m_orbitPivotOffset.x = inJson.value("OrbitPivotOffsetX", m_orbitPivotOffset.x);
    m_orbitPivotOffset.y = inJson.value("OrbitPivotOffsetY", m_orbitPivotOffset.y);
    m_orbitPivotOffset.z = inJson.value("OrbitPivotOffsetZ", m_orbitPivotOffset.z);
    m_orbitShoulderOffset = inJson.value("OrbitShoulderOffset", m_orbitShoulderOffset);
    m_orbitMinPitch = inJson.value("OrbitMinPitch", m_orbitMinPitch);
    m_orbitMaxPitch = inJson.value("OrbitMaxPitch", m_orbitMaxPitch);
    m_cameraCollisionEnabled = inJson.value("CameraCollisionEnabled", m_cameraCollisionEnabled);
    m_cameraCollisionRadius = inJson.value("CameraCollisionRadius", m_cameraCollisionRadius);

    if (inJson.contains("Profiles"))
    {
        const auto& profilesArray = inJson["Profiles"];
        for (std::size_t i{ 0 }; i < profilesArray.size() && i < m_profiles.size(); ++i)
        {
            const auto& pJson = profilesArray[i];
            auto& p = m_profiles[i];
            p.orbitDistance = pJson.value("Distance", p.orbitDistance);
            p.fovDegrees = pJson.value("Fov", p.fovDegrees);
            p.pivotOffset.x = pJson.value("PivotX", p.pivotOffset.x);
            p.pivotOffset.y = pJson.value("PivotY", p.pivotOffset.y);
            p.pivotOffset.z = pJson.value("PivotZ", p.pivotOffset.z);
            p.shoulderOffset = pJson.value("Shoulder", p.shoulderOffset);
            p.rollDegrees = pJson.value("Roll", p.rollDegrees);
            p.positionDamping = pJson.value("PosDamping", p.positionDamping);
            p.rotationDamping = pJson.value("RotDamping", p.rotationDamping);
            p.blendSpeed = pJson.value("BlendSpeed", p.blendSpeed);
        }
    }
    else
    {
        // Seed (locomotion) from legacy root values
        auto& loc = m_profiles[static_cast<std::size_t>(CameraProfileType::locomotion)];
        loc.orbitDistance = m_orbitDistance;
        loc.fovDegrees = m_fovDegrees;
        loc.pivotOffset = m_orbitPivotOffset;
        loc.shoulderOffset = m_orbitShoulderOffset;
        loc.positionDamping = m_orbitPositionDamping;
        loc.rotationDamping = m_orbitRotationDamping;
    }

    m_currentProfile = m_profiles[static_cast<std::size_t>(m_targetProfileType)];
    ResolveTargets();
}

REGISTER_COMPONENT(VirtualCameraComponent)