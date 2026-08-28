#pragma once

#include <DirectXMath.h>
#include <memory>
#include <vector>
#include <functional>
#include <algorithm>

// Forward declaration
class Camera;
class Enemy;

// Enum for easing transitions
enum class EasingType {
    Linear,
    EaseInQuad,
    EaseOutQuad,
    EaseInCubic,
    EaseOutCubic,
    SmoothStep,
    SequenceAutoCubic,
    Step 
};

// Struct for Camera Waypoints
struct CameraKeyframe {
    // Target (End) State
    DirectX::XMFLOAT3 TargetPosition;
    DirectX::XMFLOAT3 TargetRotation;

    bool isJumpCut = false;
    DirectX::XMFLOAT3 StartPosition;
    DirectX::XMFLOAT3 StartRotation;

    float Duration;
    EasingType Easing = EasingType::SmoothStep;
};

// Control modes
enum class CameraControlMode {
    GamePad,
    Mouse,
    FixedFollow,
    FixedStatic,
    Free,
    Sequence
};

struct CameraShakeSettings {
    float Duration = 0.5f;          
    float AmplitudePos = 0.5f;      
    float AmplitudeRot = 2.0f;      
    float Frequency = 25.0f;        
    float TraumaFalloff = 1.5f;     
};

class CameraController
{
public:
    // Singleton Access
    static CameraController& Instance();

    // Camera Management
    void SetActiveCamera(std::weak_ptr<Camera> camera);
    std::shared_ptr<Camera> GetActiveCamera() const { return m_activeCamera.lock(); }

    // Main Update Loop
    void Update(float elapsedTime);

    // Debug UI
    void DrawDebugGUI();

    // Settings & Controls 
    void SetTarget(const DirectX::XMFLOAT3& target);
    void SetTargetOffset(const DirectX::XMFLOAT3& offset) { m_targetOffset = offset; }
    void SetFixedSetting(const DirectX::XMFLOAT3& position);
    void SetFixedYawOffset(float radians) { m_fixedYawOffset = radians; }
    void SetFixedRollOffset(float radians) { m_fixedRollOffset = radians; }
    void SetDynamicZoomOffset(float zoomOffset) { m_targetZoomOffset = zoomOffset; }

    // Spline Settings
    void SetSplineTension(float tension) { m_splineTension = tension; }
    float GetSplineTension() const { return m_splineTension; }

    // Mode Management
    void SetControlMode(CameraControlMode mode);
    CameraControlMode GetControlMode() const { return m_controlMode; }
    void ClearCamera() { m_activeCamera.reset(); }

    // Sequence / Cutscene System 
    void PlaySequence(const std::vector<CameraKeyframe>& sequence, bool loop = false);

    // Helper to auto-calculate duration based on speed
    void PlaySequenceBySpeed(const std::vector<CameraKeyframe>& targets, float speed, EasingType globalEasing, bool useSplinePath, bool loop = false);

    void AppendKeyframe(const CameraKeyframe& keyframe);
    void StopSequence();
    bool IsSequencing() const { return m_controlMode == CameraControlMode::Sequence; }

    // JUICE: Camera Shake System 
    void TriggerShake(const CameraShakeSettings& settings, float intensityMultiplier = 1.0f);
    void StopShake();

    struct SequenceTimeInfo {
        bool IsPlaying;
        float CurrentTime;
        float TotalDuration;
        size_t CurrentIndex; 
        size_t TotalShots;   
        EasingType CurrentEasing;
    };

    SequenceTimeInfo GetSequenceProgress() const;

    static float ApplyEasing(float t, EasingType type);

    void AddTrauma(float amount) {
        m_trauma = (std::min)(1.0f, (std::max)(m_trauma, amount));
    }

    float GetTrauma() const { return m_trauma; }

    DirectX::XMFLOAT3 GetShakeOffset() const { return m_shakeOffset; }

private:
    // Singleton handling
    CameraController();
    ~CameraController() = default;
    CameraController(const CameraController&) = delete;
    void operator=(const CameraController&) = delete;

    // Update Sub-routines 
    void UpdateSequence(float dt, std::shared_ptr<Camera>& camera);
    void UpdateFixedFollow(float dt, std::shared_ptr<Camera>& camera);
    void UpdateFixedStatic(std::shared_ptr<Camera>& camera);
    void UpdateFreeCamera(float dt, std::shared_ptr<Camera>& camera);
    void UpdateOrbitCamera(float dt, std::shared_ptr<Camera>& camera);

    // Math Helpers (Static/Pure) 
    static DirectX::XMFLOAT3 LerpFloat3(const DirectX::XMFLOAT3& start, const DirectX::XMFLOAT3& end, float t);
    static DirectX::XMFLOAT3 CalculateHermitePos(const DirectX::XMFLOAT3& p0, const DirectX::XMFLOAT3& p1, const DirectX::XMFLOAT3& p2, const DirectX::XMFLOAT3& p3, float t, float tension);

    // Members 
    std::weak_ptr<Camera> m_activeCamera;
    CameraControlMode m_controlMode = CameraControlMode::GamePad;

    // State
    DirectX::XMFLOAT3 m_targetPos = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_currentAngle = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_eyePos = { 0.0f, 5.0f, -10.0f };
    DirectX::XMFLOAT3 m_fixedPos = { 0.0f, 15.0f, -10.0f };
    DirectX::XMFLOAT3 m_targetOffset = { 0.0f, 0.0f, 0.0f };

    float m_fixedYawOffset = 0.0f;
    float m_fixedRollOffset = 0.0f;
    float m_splineTension = 1.0f;

    float m_targetZoomOffset = 0.0f;
    float m_currentZoomOffset = 0.0f;

    float m_targetZoom{ 0.0f };
    int m_zoomFrameCounter{ 0 };
    const Enemy* m_cachedClosestEnemy{ nullptr };

    // Settings
    float m_moveSpeed = 15.0f;
    float m_rollSpeed = DirectX::XMConvertToRadians(90);
    float m_orbitRange = 10.0f;
    float m_maxAngleX = DirectX::XMConvertToRadians(85);
    float m_minAngleX = DirectX::XMConvertToRadians(-85);
    bool m_toggleCursor = true;

    // Sequence State
    std::vector<CameraKeyframe> m_sequenceQueue;
    size_t m_currentKeyframeIdx = 0; 
    float m_seqTimer = 0.0f;
    bool m_seqLoop = false;
    bool m_useSpline = false;

    // Cache for interpolation start points
    DirectX::XMFLOAT3 m_seqStartPos = { 0,0,0 };
    DirectX::XMFLOAT3 m_seqStartRot = { 0,0,0 };

    void UpdateShakeLogic(float dt);

    DirectX::XMFLOAT3 GetShakePosOffset() const { return m_shakeResultPos; }
    DirectX::XMFLOAT3 GetShakeRotOffset() const { return m_shakeResultRot; }

    // Shake State Members 
    CameraShakeSettings m_activeShakeSettings;
    float m_shakeTrauma = 0.0f;     
    float m_shakeTimeCounter = 0.0f;

    DirectX::XMFLOAT3 m_shakeResultPos = { 0,0,0 };
    DirectX::XMFLOAT3 m_shakeResultRot = { 0,0,0 };

    float m_trauma = 0.0f;
    float m_traumaDecay = 1.5f; 
    DirectX::XMFLOAT3 m_shakeOffset = { 0.0f, 0.0f, 0.0f };
};