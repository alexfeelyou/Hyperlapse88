#include "CinematicDirector.h"
#include "JuiceEngine.h" 
#include <cmath>

using namespace DirectX;

CinematicDirector::CinematicDirector()
{
    InitializeScenarios();
}

void CinematicDirector::InitializeScenarios()
{
    m_camScenarioPoints.clear();
    // No hardcoded shots anymore! The list starts empty.
    // You can now freely add shots via the GUI Inspector.
}

void CinematicDirector::Update(float elapsedTime, DirectX::XMFLOAT3 playerPosition)
{
    // Old threshold, formation, and destruction triggers are completely gone!
    // You can add new game-specific camera logic here later.
}

void CinematicDirector::PlaySequence(int index, bool isJumpCut)
{
    if (index < 0 || index >= m_camScenarioPoints.size()) return;

    const auto& shot = m_camScenarioPoints[index];
    std::vector<CameraKeyframe> seq;
    CameraKeyframe key;

    key.isJumpCut = isJumpCut;
    key.StartPosition = shot.StartPos;
    key.StartRotation = CalculateRotation(shot.StartPos, shot.StartLookAt);

    key.TargetPosition = shot.EndPos;
    key.TargetRotation = CalculateRotation(shot.EndPos, shot.EndLookAt);
    key.Duration = shot.Duration;
    key.Easing = shot.Easing;

    seq.push_back(key);

    CameraController::Instance().PlaySequence(seq, false);
}

// Math Helper: Konversi (Posisi + Target) menjadi (Pitch, Yaw)
DirectX::XMFLOAT3 CinematicDirector::CalculateRotation(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& target)
{
    XMVECTOR vPos = XMLoadFloat3(&pos);
    XMVECTOR vTarget = XMLoadFloat3(&target);
    XMVECTOR vDir = XMVectorSubtract(vTarget, vPos);
    vDir = XMVector3Normalize(vDir);

    XMFLOAT3 dir;
    XMStoreFloat3(&dir, vDir);

    float pitch = asinf(-dir.y);
    float yaw = atan2f(dir.x, dir.z);

    return { pitch, yaw, 0.0f };
}