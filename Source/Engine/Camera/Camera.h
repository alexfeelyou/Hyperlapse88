#pragma once
#include <DirectXMath.h>
#include <cstdint>

class Camera
{
public:
    Camera();
    ~Camera();

    // Transform Operations 
    void SetPosition(const DirectX::XMFLOAT3& position);
    void SetPosition(float x, float y, float z);
    void Translate(const DirectX::XMFLOAT3& translation);
    void Translate(float x, float y, float z);

    void SetRotation(const DirectX::XMFLOAT3& rotation);
    void SetRotation(float x, float y, float z);
    void LookAt(const DirectX::XMFLOAT3& target);

    // Updates position and rotation to look at a target from a specific eye position
    void SetLookAt(const DirectX::XMFLOAT3& eye, const DirectX::XMFLOAT3& focus, const DirectX::XMFLOAT3& up);

    // Projection Settings 
    void SetPerspectiveFov(float fovY, float aspect, float nearZ, float farZ);
    void SetAspectRatio(float aspect);
    void SetOffCenterProjection(float left, float right, float bottom, float top, float nearZ, float farZ);
    void SetOrthographic(float viewWidth, float viewHeight, float nearZ, float farZ);

    // Temporal jitter (TAA)
    void SetJitterEnabled(bool enabled) noexcept { m_jitterEnabled = enabled; }
    [[nodiscard]] bool IsJitterEnabled() const noexcept { return m_jitterEnabled; }

    // Advances the jitter sequence and rebuilds projection with a new sub-pixel offset baked in
    void AdvanceJitter(std::uint32_t frameIndex, float screenWidth, float screenHeight) noexcept;

    // Snapshots this frame's jittered view-projection. Call once per frame, right after CbScene/CbVelocity
    // are uploaded for the current frame, so it holds exactly what was rendered
    void CachePreviousViewProjection() noexcept;

    [[nodiscard]] const DirectX::XMFLOAT4X4& GetPreviousViewProjection() const noexcept { return m_previousViewProjection; }
    [[nodiscard]] const DirectX::XMFLOAT2& GetJitterOffset() const noexcept { return m_currentJitterOffsetUV; }

    // Accessors 
    const DirectX::XMFLOAT3& GetPosition() const { return position; }
    const DirectX::XMFLOAT3& GetRotation() const { return rotation; }
    const DirectX::XMFLOAT4X4& GetView() const { return view; }
    const DirectX::XMFLOAT4X4& GetProjection() const { return projection; }
    [[nodiscard]] float GetNearZ() const noexcept { return nearZ; }
    [[nodiscard]] float GetFarZ() const noexcept { return farZ; }
    [[nodiscard]] float GetFovY() const noexcept { return fovY; }
    [[nodiscard]] float GetAspectRatio() const noexcept { return projection._22 / projection._11; }

    // Helpers 
    DirectX::XMFLOAT3 GetFocus() const;
    const DirectX::XMFLOAT3& GetFront() const { return front; }
    const DirectX::XMFLOAT3& GetRight() const { return right; }
    const DirectX::XMFLOAT3& GetUp() const { return up; }

    bool CheckSphere(float x, float y, float z, float radius);
private:
    void UpdateViewMatrix();

    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 rotation; 

    DirectX::XMFLOAT3 front;
    DirectX::XMFLOAT3 right;
    DirectX::XMFLOAT3 up;

    DirectX::XMFLOAT4X4 view;
    DirectX::XMFLOAT4X4 projection;

    DirectX::XMFLOAT4X4 m_unjitteredProjection{}; 
    DirectX::XMFLOAT4X4 m_previousViewProjection{};
    DirectX::XMFLOAT2   m_currentJitterOffsetUV{ 0.0f, 0.0f };

    bool m_jitterEnabled{ false };

    float fovY = DirectX::XM_PIDIV4;
    float nearZ = 0.1f;
    float farZ = 1000.0f;
};