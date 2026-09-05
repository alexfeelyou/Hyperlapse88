#include <algorithm>
#include <cmath>
#include <imgui.h>
#include "System/Input.h" 
#include "Camera.h"
#include "CameraController.h"

using namespace DirectX;

// =========================================================
// MATH HELPERS
// =========================================================

float CameraController::ApplyEasing(float t, EasingType type)
{
    t = std::clamp(t, 0.0f, 1.0f);

    switch (type)
    {
    case EasingType::EaseInQuad:    return t * t;
    case EasingType::EaseOutQuad:   return t * (2.0f - t);
    case EasingType::EaseInCubic:   return t * t * t;
    case EasingType::EaseOutCubic:  return (--t) * t * t + 1;
    case EasingType::SmoothStep:    return t * t * (3.0f - 2.0f * t);
    case EasingType::Linear: default: return t;
    }
}

XMFLOAT3 CameraController::LerpFloat3(const XMFLOAT3& start, const XMFLOAT3& end, float t)
{
    XMVECTOR vStart = XMLoadFloat3(&start);
    XMVECTOR vEnd = XMLoadFloat3(&end);
    XMVECTOR vResult = XMVectorLerp(vStart, vEnd, t);

    XMFLOAT3 result;
    XMStoreFloat3(&result, vResult);
    return result;
}

XMFLOAT3 CameraController::CalculateHermitePos(const XMFLOAT3& p0, const XMFLOAT3& p1, const XMFLOAT3& p2, const XMFLOAT3& p3, float t, float tension)
{
    // Catmull-Rom / Hermite spline implementation

    XMVECTOR vP0 = XMLoadFloat3(&p0);
    XMVECTOR vP1 = XMLoadFloat3(&p1);
    XMVECTOR vP2 = XMLoadFloat3(&p2);
    XMVECTOR vP3 = XMLoadFloat3(&p3);

    // Calculate segment length for tension scaling
    float segLength = XMVectorGetX(XMVector3Length(XMVectorSubtract(vP2, vP1)));

    // Tangent 1 (at P1) -> Direction from P0 to P2
    XMVECTOR vT1 = XMVector3Normalize(XMVectorSubtract(vP2, vP0)) * segLength * tension;

    // Tangent 2 (at P2) -> Direction from P1 to P3
    XMVECTOR vT2 = XMVector3Normalize(XMVectorSubtract(vP3, vP1)) * segLength * tension;

    // Hermite Basis Functions
    float t2 = t * t;
    float t3 = t2 * t;
    float h1 = 2.0f * t3 - 3.0f * t2 + 1.0f;
    float h2 = -2.0f * t3 + 3.0f * t2;
    float h3 = t3 - 2.0f * t2 + t;
    float h4 = t3 - t2;

    XMVECTOR result = (vP1 * h1) + (vP2 * h2) + (vT1 * h3) + (vT2 * h4);

    XMFLOAT3 finalPos;
    XMStoreFloat3(&finalPos, result);
    return finalPos;
}

// =========================================================
// CORE LOGIC
// =========================================================

CameraController::CameraController() {}

CameraController& CameraController::Instance()
{
    static CameraController instance;
    return instance;
}

void CameraController::SetActiveCamera(std::weak_ptr<Camera> camera)
{
    m_activeCamera = camera;
}

void CameraController::Update(float elapsedTime)
{
    std::shared_ptr<Camera> camera = m_activeCamera.lock();
    if (!camera) return;

    if (m_trauma > 0.0f) {
        m_trauma -= m_traumaDecay * elapsedTime;
        if (m_trauma < 0.0f) m_trauma = 0.0f;

        float shake = m_trauma * m_trauma;
        float maxOffset = 2.0f;

        float rx = ((rand() % 200) - 100) / 100.0f;
        float rz = ((rand() % 200) - 100) / 100.0f;

        m_shakeOffset.x = rx * maxOffset * shake;
        m_shakeOffset.y = 0.0f;
        m_shakeOffset.z = rz * maxOffset * shake;
    }
    else {
        m_shakeOffset = { 0.0f, 0.0f, 0.0f };
    }

    if (m_controlMode == CameraControlMode::Free)
    {
        // Unity-style Editor Camera: Hold Right Mouse Button to fly/look
        const bool isRightClickDown{ (GetKeyState(VK_RBUTTON) & 0x8000) != 0 };

        // Ensure ImGui Context is ready before querying
        if (ImGui::GetCurrentContext() != nullptr)
        {
            const ImGuiIO& io{ ImGui::GetIO() };

            // If the user right-clicks over an ImGui window, ignore the input
            if (io.WantCaptureMouse && !m_isViewportHovered && !m_toggleCursor)
            {
                m_toggleCursor = false;
            }
            else
            {
                m_toggleCursor = isRightClickDown;
            }
        }
        else
        {
            m_toggleCursor = isRightClickDown;
        }
    }

    // Cursor Lock Logic
    bool shouldLock = (m_controlMode == CameraControlMode::Sequence ||
        m_controlMode == CameraControlMode::FixedStatic ||
        m_controlMode == CameraControlMode::FixedFollow)
        ? false : m_toggleCursor;

    Input::Instance().GetMouse().LockCursor(shouldLock);

    // State Machine 
    switch (m_controlMode)
    {
    case CameraControlMode::Sequence:    UpdateSequence(elapsedTime, camera); break;
    case CameraControlMode::FixedFollow: UpdateFixedFollow(elapsedTime, camera); break;
    case CameraControlMode::FixedStatic: UpdateFixedStatic(camera); break;
    case CameraControlMode::Free:        UpdateFreeCamera(elapsedTime, camera); break;
    case CameraControlMode::GamePad:
    case CameraControlMode::Mouse:       UpdateOrbitCamera(elapsedTime, camera); break;
    }

    // POST-UPDATE MODIFIERS (Shake, Sway, Headbob, etc.)
    XMFLOAT3 shakePos = { 0.0f, 0.0f, 0.0f };
    XMFLOAT3 shakeRot = { 0.0f, 0.0f, 0.0f };

    // Apply Position
    XMFLOAT3 currentPos = camera->GetPosition();
    currentPos.x += shakePos.x;
    currentPos.y += shakePos.y;
    currentPos.z += shakePos.z;
    camera->SetPosition(currentPos);

    // Apply Rotation (Additive)
    XMFLOAT3 currentRot = camera->GetRotation();
    currentRot.x += shakeRot.x;
    currentRot.y += shakeRot.y;
    currentRot.z += shakeRot.z;
    camera->SetRotation(currentRot);
}

// =========================================================
// STATE HANDLERS
// =========================================================

void CameraController::UpdateSequence(float dt, std::shared_ptr<Camera>& camera)
{
    if (m_sequenceQueue.empty())
    {
        SetControlMode(CameraControlMode::FixedStatic);
        return;
    }

    const CameraKeyframe& targetKey = m_sequenceQueue[m_currentKeyframeIdx];
    if (m_seqTimer == 0.0f)
    {
        if (targetKey.isJumpCut)
        {
            m_seqStartPos = targetKey.StartPosition;
            m_seqStartRot = targetKey.StartRotation;

            camera->SetPosition(m_seqStartPos);
            camera->SetRotation(m_seqStartRot);
        }
        else
        {
            m_seqStartPos = camera->GetPosition();
            m_seqStartRot = camera->GetRotation();
        }
    }

    m_seqTimer += dt;

    // Calculate progress (t)
    float t = (targetKey.Duration > 0.0f) ? (m_seqTimer / targetKey.Duration) : 1.0f;

    if (t >= 1.0f)
    {
        // Snap to target
        camera->SetPosition(targetKey.TargetPosition);
        camera->SetRotation(targetKey.TargetRotation);

        // Advance Keyframe
        m_currentKeyframeIdx++;
        m_seqTimer = 0.0f;

        if (m_currentKeyframeIdx >= m_sequenceQueue.size())
        {
            if (m_seqLoop)
            {
                m_currentKeyframeIdx = 0;
                m_seqStartPos = camera->GetPosition();
                m_seqStartRot = camera->GetRotation();
            }
            else
            {
                // Sequence Finished
                m_fixedPos = camera->GetPosition();

                XMFLOAT3 finalRot = camera->GetRotation();
                XMMATRIX rotMat = XMMatrixRotationRollPitchYaw(finalRot.x, finalRot.y, finalRot.z);
                XMVECTOR vForward = XMVectorSet(0, 0, 1, 0); // Forward default
                vForward = XMVector3TransformNormal(vForward, rotMat);
                XMVECTOR vPos = XMLoadFloat3(&m_fixedPos);
                XMVECTOR vNewTarget = XMVectorAdd(vPos, XMVectorScale(vForward, 10.0f));

                XMStoreFloat3(&m_targetPos, vNewTarget);

                SetControlMode(CameraControlMode::FixedStatic);
            }
        }
        else
        {
            // Prepare for next segment
            m_seqStartPos = camera->GetPosition();
            m_seqStartRot = camera->GetRotation();
        }
    }
    else
    {
        XMFLOAT3 newPos;
        XMFLOAT3 newRot;
        // Interpolation
        if (targetKey.Easing == EasingType::Step)
        {
            newPos = targetKey.TargetPosition;
            newRot = targetKey.TargetRotation;
        }
        else
        {
            float easeT = ApplyEasing(t, targetKey.Easing);
            if (m_useSpline)
            {
                // Get P0 (Prev), P1 (Start), P2 (Target), P3 (Next)
                XMFLOAT3 p0 = (m_currentKeyframeIdx > 0) ? m_sequenceQueue[m_currentKeyframeIdx - 1].TargetPosition : m_seqStartPos;
                XMFLOAT3 p3 = (m_currentKeyframeIdx + 1 < m_sequenceQueue.size()) ? m_sequenceQueue[m_currentKeyframeIdx + 1].TargetPosition : targetKey.TargetPosition;

                newPos = CalculateHermitePos(p0, m_seqStartPos, targetKey.TargetPosition, p3, easeT, m_splineTension);
            }
            else
            {
                newPos = LerpFloat3(m_seqStartPos, targetKey.TargetPosition, easeT);
            }

            // Simple Lerp for rotation (Switch to Quaternion Slerp if needed later)
            XMFLOAT3 newRot = LerpFloat3(m_seqStartRot, targetKey.TargetRotation, easeT);

            camera->SetPosition(newPos);
            camera->SetRotation(newRot);

            // Sync internal state
            m_eyePos = newPos;
            m_currentAngle = newRot;
            m_fixedPos = newPos;
        }
    }
}

void CameraController::UpdateFixedFollow(float dt, std::shared_ptr<Camera>& camera)
{
    // DYNAMIC COMBAT ZOOM (Smooth Lerp)
    // How fast the camera zooms in and out during combat
    float zoomLerpSpeed = 2.5f;
    m_currentZoomOffset += (m_targetZoomOffset - m_currentZoomOffset) * zoomLerpSpeed * dt;

    XMFLOAT3 dynamicOffset = m_fixedPos;
    // Lower the Y height based on the zoom
    dynamicOffset.y += m_currentZoomOffset;
    // Push the Z forward slightly as it zooms in to keep the player centered
    dynamicOffset.z -= m_currentZoomOffset * 0.5f;

    // CALCULATE DESIRED POSITION
    XMFLOAT3 desiredPos;
    desiredPos.x = m_targetPos.x + dynamicOffset.x;
    desiredPos.y = m_targetPos.y + dynamicOffset.y;
    desiredPos.z = m_targetPos.z + dynamicOffset.z;

    // SOFT FOLLOW (Lerp)
    float followSpeed = 5.0f;

    XMFLOAT3 currentPos = camera->GetPosition();
    XMFLOAT3 newPos;

    newPos.x = currentPos.x + (desiredPos.x - currentPos.x) * followSpeed * dt;
    newPos.y = currentPos.y + (desiredPos.y - currentPos.y) * followSpeed * dt;
    newPos.z = currentPos.z + (desiredPos.z - currentPos.z) * followSpeed * dt;

    camera->SetPosition(newPos);

    // SMOOTH LOOK AT
    XMFLOAT3 desiredLookAt;
    desiredLookAt.x = m_targetPos.x + m_targetOffset.x;
    desiredLookAt.y = m_targetPos.y + m_targetOffset.y;
    desiredLookAt.z = m_targetPos.z + m_targetOffset.z;

    camera->LookAt(desiredLookAt);
}

void CameraController::UpdateFixedStatic(std::shared_ptr<Camera>& camera)
{
    XMVECTOR vTarget = XMLoadFloat3(&m_targetPos);
    XMVECTOR vOffset = XMLoadFloat3(&m_targetOffset);
    XMVECTOR vFinalTarget = XMVectorAdd(vTarget, vOffset);

    XMFLOAT3 finalTargetPos;
    XMStoreFloat3(&finalTargetPos, vFinalTarget);

    camera->SetPosition(m_fixedPos);
    camera->LookAt(finalTargetPos);

    // Apply manual rotation offsets
    if (m_fixedYawOffset != 0.0f || m_fixedRollOffset != 0.0f)
    {
        XMFLOAT3 currentRot = camera->GetRotation();
        currentRot.y += m_fixedYawOffset;
        currentRot.z = m_fixedRollOffset;
        camera->SetRotation(currentRot);
    }
}

void CameraController::UpdateFreeCamera(float dt, std::shared_ptr<Camera>& camera)
{
    // Track the Right Mouse Button state globally across all frames
    static bool s_wasRightBtnDown{ false };
    static int s_ignoreFrames{ 0 };

    const bool rightBtnDown{ (GetKeyState(VK_RBUTTON) & 0x8000) != 0 };
    const bool justPressed{ rightBtnDown && !s_wasRightBtnDown };
    s_wasRightBtnDown = rightBtnDown;

    if (justPressed)
    {
        // Absorb the initial click frame and the subsequent OS warp frame
        s_ignoreFrames = 2;
    }

    // Only allow looking and moving if the cursor is locked (Right-Click held)
    if (!m_toggleCursor) return;

    Mouse& mouse = Input::Instance().GetMouse();
    float deltaX{ static_cast<float>(mouse.GetDeltaX()) };
    float deltaY{ static_cast<float>(mouse.GetDeltaY()) };

    // Discard the massive delta spike while the OS centers the cursor
    if (s_ignoreFrames > 0)
    {
        deltaX = 0.0f;
        deltaY = 0.0f;
        --s_ignoreFrames;
    }

    // Apply rotation purely using the cached angle to prevent matrix read-back drift
    const float sensitivity{ 0.5f };
    const float rotSpeed{ m_rollSpeed * dt };

    m_currentAngle.y += deltaX * sensitivity * rotSpeed;
    m_currentAngle.x += deltaY * sensitivity * rotSpeed;

    // Ensure Pitch does not exceed vertical bounds to prevent screen flip
    constexpr float PITCH_LIMIT{ DirectX::XM_PIDIV2 - 0.01f }; // Slightly less than 90 degrees
    m_currentAngle.x = std::clamp(m_currentAngle.x, -PITCH_LIMIT, PITCH_LIMIT);

    camera->SetRotation(m_currentAngle);

    // Translation
    const float moveAmount{ m_moveSpeed * dt };
    DirectX::XMFLOAT3 moveDir{ 0.0f, 0.0f, 0.0f };

    if (GetKeyState('W') & 0x8000) moveDir.z += moveAmount;
    if (GetKeyState('S') & 0x8000) moveDir.z -= moveAmount;
    if (GetKeyState('A') & 0x8000) moveDir.x -= moveAmount;
    if (GetKeyState('D') & 0x8000) moveDir.x += moveAmount;
    if (GetKeyState('E') & 0x8000) moveDir.y += moveAmount; // E to go Up
    if (GetKeyState('Q') & 0x8000) moveDir.y -= moveAmount; // Q to go Down

    camera->Translate(moveDir);
    m_eyePos = camera->GetPosition();
}

void CameraController::UpdateOrbitCamera(float dt, std::shared_ptr<Camera>& camera)
{
    float ax = 0.0f;
    float ay = 0.0f;

    if (m_controlMode == CameraControlMode::GamePad)
    {
        GamePad& gamePad = Input::Instance().GetGamePad();
        ax = gamePad.GetAxisRX();
        ay = gamePad.GetAxisRY();
    }
    else if (m_controlMode == CameraControlMode::Mouse && m_toggleCursor)
    {
        Mouse& mouse = Input::Instance().GetMouse();
        float sensitivity = 0.5f;
        ax = static_cast<float>(mouse.GetDeltaX()) * sensitivity;
        ay = static_cast<float>(mouse.GetDeltaY()) * sensitivity;
    }

    // Update Angles
    float speed = m_rollSpeed * dt;
    m_currentAngle.x += ay * speed;
    m_currentAngle.y += ax * speed;

    // Clamp Pitch
    m_currentAngle.x = std::clamp(m_currentAngle.x, m_minAngleX, m_maxAngleX);

    // Wrap Yaw
    if (m_currentAngle.y > XM_PI) m_currentAngle.y -= XM_2PI;
    if (m_currentAngle.y < -XM_PI) m_currentAngle.y += XM_2PI;

    // Calculate Position
    XMMATRIX matRot = XMMatrixRotationRollPitchYaw(m_currentAngle.x, m_currentAngle.y, 0.0f);
    XMVECTOR vOffset = XMVectorSet(0, 0, -m_orbitRange, 0);
    vOffset = XMVector3TransformCoord(vOffset, matRot);

    XMVECTOR vTarget = XMLoadFloat3(&m_targetPos);
    XMVECTOR vEye = XMVectorAdd(vTarget, vOffset);

    XMFLOAT3 finalPos;
    XMStoreFloat3(&finalPos, vEye);

    camera->SetPosition(finalPos);
    camera->LookAt(m_targetPos);

    m_eyePos = finalPos;
}

void CameraController::SetTarget(const DirectX::XMFLOAT3& target)
{
    m_targetPos = target;
}

void CameraController::SetFixedSetting(const DirectX::XMFLOAT3& value)
{
    m_fixedPos = value;
}

void CameraController::SnapToTarget()
{
    std::shared_ptr<Camera> camera = m_activeCamera.lock();
    if (!camera) return;

    if (m_controlMode == CameraControlMode::FixedFollow)
    {
        // Calculate exact resting position based on current offsets
        DirectX::XMFLOAT3 desiredPos;
        desiredPos.x = m_targetPos.x + m_fixedPos.x;
        desiredPos.y = m_targetPos.y + m_fixedPos.y + m_currentZoomOffset;
        desiredPos.z = m_targetPos.z + m_fixedPos.z - (m_currentZoomOffset * 0.5f);

        // Teleport instantly 
        camera->SetPosition(desiredPos);

        // Calculate exact look-at angle
        DirectX::XMFLOAT3 desiredLookAt;
        desiredLookAt.x = m_targetPos.x + m_targetOffset.x;
        desiredLookAt.y = m_targetPos.y + m_targetOffset.y;
        desiredLookAt.z = m_targetPos.z + m_targetOffset.z;

        camera->LookAt(desiredLookAt);
    }
}

void CameraController::SyncFromActiveCamera()
{
    if (std::shared_ptr<Camera> camera = m_activeCamera.lock())
    {
        // Pull the live, loaded coordinates directly from the camera
        // to overwrite the controller's stale baseline
        m_eyePos = camera->GetPosition();
        m_currentAngle = camera->GetRotation();
    }
}

void CameraController::SetControlMode(CameraControlMode mode)
{
    if (m_controlMode != CameraControlMode::Mouse && mode == CameraControlMode::Mouse)
        m_toggleCursor = true;

    if (m_controlMode != CameraControlMode::Free && mode == CameraControlMode::Free)
    {
        auto camera = m_activeCamera.lock();
        if (camera)
        {
            m_eyePos = camera->GetPosition();
            m_currentAngle = camera->GetRotation();
        }
        m_toggleCursor = true;
    }

    m_controlMode = mode;
}

void CameraController::PlaySequence(const std::vector<CameraKeyframe>& sequence, bool loop)
{
    if (sequence.empty()) return;

    m_sequenceQueue = sequence;
    m_currentKeyframeIdx = 0;
    m_seqTimer = 0.0f;
    m_seqLoop = loop;

    auto camera = m_activeCamera.lock();
    if (camera)
    {
        m_seqStartPos = camera->GetPosition();
        m_seqStartRot = camera->GetRotation();
    }

    SetControlMode(CameraControlMode::Sequence);
}

void CameraController::PlaySequenceBySpeed(const std::vector<CameraKeyframe>& targets, float speed, EasingType globalEasing, bool useSplinePath, bool loop)
{
    if (targets.empty() || speed <= 0.0f) return;

    m_useSpline = useSplinePath;
    std::vector<CameraKeyframe> processedSequence = targets;

    XMFLOAT3 currentPos;
    auto camera = m_activeCamera.lock();
    if (camera) currentPos = camera->GetPosition();
    else currentPos = { 0,0,0 };

    for (size_t i = 0; i < processedSequence.size(); ++i)
    {
        CameraKeyframe& key = processedSequence[i];

        // Determine Easing
        if (globalEasing == EasingType::SequenceAutoCubic && processedSequence.size() > 1)
        {
            if (i == 0) key.Easing = EasingType::EaseInCubic;
            else if (i == processedSequence.size() - 1) key.Easing = EasingType::EaseOutCubic;
            else key.Easing = EasingType::Linear;
        }
        else
        {
            key.Easing = (globalEasing == EasingType::SequenceAutoCubic) ? EasingType::SmoothStep : globalEasing;
        }

        // Calculate Duration based on Distance & Speed
        XMVECTOR vStart = XMLoadFloat3(&currentPos);
        XMVECTOR vEnd = XMLoadFloat3(&key.TargetPosition);
        float distance = XMVectorGetX(XMVector3Length(XMVectorSubtract(vEnd, vStart)));

        key.Duration = (distance > 0.001f) ? (distance / speed) : 0.0f;
        currentPos = key.TargetPosition;
    }

    PlaySequence(processedSequence, loop);
}

void CameraController::AppendKeyframe(const CameraKeyframe& keyframe)
{
    m_sequenceQueue.push_back(keyframe);
}

void CameraController::StopSequence()
{
    m_sequenceQueue.clear();
    m_currentKeyframeIdx = 0;
    SetControlMode(CameraControlMode::FixedStatic);
}

void CameraController::DrawDebugGUI()
{
    auto camera = m_activeCamera.lock();
    if (!camera) return;

    ImGui::Begin("Camera Controller");

    // Helper lambda for radio buttons
    auto ModeRadio = [&](const char* label, CameraControlMode mode) {
        if (ImGui::RadioButton(label, m_controlMode == mode)) SetControlMode(mode);
        };

    ModeRadio("Fixed Follow", CameraControlMode::FixedFollow);
    ModeRadio("Fixed Static", CameraControlMode::FixedStatic);
    ImGui::SameLine();
    ModeRadio("GamePad", CameraControlMode::GamePad);
    ModeRadio("Mouse", CameraControlMode::Mouse);
    ImGui::SameLine();
    ModeRadio("Free (Debug)", CameraControlMode::Free);

    ImGui::Separator();

    XMFLOAT3 p = camera->GetPosition();
    XMFLOAT3 r = camera->GetRotation();
    ImGui::Text("Pos: %.2f, %.2f, %.2f", p.x, p.y, p.z);
    ImGui::Text("Rot: %.2f, %.2f, %.2f", r.x, r.y, r.z);

    if (m_controlMode == CameraControlMode::Sequence)
    {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0, 1, 0, 1), "SEQUENCER RUNNING");
        if (!m_sequenceQueue.empty() && m_currentKeyframeIdx < m_sequenceQueue.size())
        {
            ImGui::Text("Keyframe: %llu / %llu", m_currentKeyframeIdx + 1, m_sequenceQueue.size());
            float prog = m_seqTimer / m_sequenceQueue[m_currentKeyframeIdx].Duration;
            ImGui::ProgressBar(prog);
        }
        if (ImGui::Button("Stop")) StopSequence();
    }

    ImGui::End();
}

CameraController::SequenceTimeInfo CameraController::GetSequenceProgress() const
{
    SequenceTimeInfo info;
    info.IsPlaying = (m_controlMode == CameraControlMode::Sequence) && !m_sequenceQueue.empty();

    if (info.IsPlaying && m_currentKeyframeIdx < m_sequenceQueue.size())
    {
        info.CurrentTime = m_seqTimer;
        info.TotalDuration = m_sequenceQueue[m_currentKeyframeIdx].Duration;
        info.CurrentIndex = m_currentKeyframeIdx;
        info.TotalShots = m_sequenceQueue.size();

        info.CurrentEasing = m_sequenceQueue[m_currentKeyframeIdx].Easing;
    }
    else
    {
        info.CurrentTime = 0.0f;
        info.TotalDuration = 0.0f;
        info.CurrentIndex = 0;
        info.TotalShots = 0;
        info.CurrentEasing = EasingType::Linear; 
    }

    return info;
}