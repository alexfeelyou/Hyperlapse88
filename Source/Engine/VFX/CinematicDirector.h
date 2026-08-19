#pragma once
#include <vector>
#include <string>
#include <DirectXMath.h>
#include "CameraController.h"

struct SceneCameraPoint {
    std::string Name;
    DirectX::XMFLOAT3 StartPos;
    DirectX::XMFLOAT3 StartLookAt;
    DirectX::XMFLOAT3 EndPos;
    DirectX::XMFLOAT3 EndLookAt;
    float Duration;
    EasingType Easing;
};

class CinematicDirector
{
public:
    CinematicDirector();

    // Setup data shot (Now starts completely empty)
    void InitializeScenarios();

    // Cleaned up Update (No more breakout energy/thresholds!)
    void Update(float elapsedTime, DirectX::XMFLOAT3 playerPosition);

    // Helper untuk GUI agar tetap bisa diedit
    std::vector<SceneCameraPoint>& GetScenarios() { return m_camScenarioPoints; }

private:
    DirectX::XMFLOAT3 CalculateRotation(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& target);

    std::vector<SceneCameraPoint> m_camScenarioPoints;

    // Internal helper
    void PlaySequence(int index, bool isJumpCut = false);
};