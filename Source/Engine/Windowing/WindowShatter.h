#pragma once

#include <memory>
#include <vector>
#include <string> // Tambahkan string
#include <DirectXMath.h>
#include "BeyondWindow.h"
#include "Camera.h"

// Physics parameters for the shatter effect
struct ShatterPhysics
{
    DirectX::XMFLOAT2 velocity{ 0.0f, 0.0f };
    float deceleration = 0.98f;
    float bounceDamping = 0.7f;
    int bounceCount = 0;
    int maxBounces = 3;
};

class WindowShatter
{
public:
    WindowShatter(
        const char* title,
        DirectX::XMFLOAT2 startPos,
        DirectX::XMFLOAT2 velocity,
        int size,
        int priority
    );
    ~WindowShatter();

    void Update(float dt);

    [[nodiscard]] bool IsNativeWindow() const { return m_isNativeWindow; }
    [[nodiscard]] bool ShouldDestroy() const { return m_markedForDestroy; }

    [[nodiscard]] DirectX::XMFLOAT3 GetVirtualWorldPos() const { return m_virtualWorldPos; }
    [[nodiscard]] DirectX::XMFLOAT2 GetSize() const { return { m_width, m_height }; }
    [[nodiscard]] Beyond::Window* GetWindow() const { return m_window; }
    [[nodiscard]] Camera* GetCamera() const { return m_camera.get(); }

    // [TAMBAH] Flag untuk safe destruction
    void PrepareForDestruction() { m_preparedForDestroy = true; }
    [[nodiscard]] bool IsPreppedForDestroy() const { return m_preparedForDestroy; }

    // [TAMBAH] Manual cleanup method
    void Cleanup();

    void SetSleeping(bool sleep) { m_isSleeping = sleep; }
    bool IsSleeping() const { return m_isSleeping; }
    void WakeUp();

private:
    void UpdateVirtualState(float dt);
    void UpdateNativeState(float dt);
    void TransitionToNativeWindow();

    void EnforceScreenBounds();
    void ConvertWorldToScreen(const DirectX::XMFLOAT3& worldPos, float& outX, float& outY) const;

private:
    Beyond::Window* m_window = nullptr;
    std::shared_ptr<Camera> m_camera;
    ShatterPhysics m_physics;

    std::string m_title; // [FIX] Simpan title unik

    bool m_isNativeWindow = false;
    bool m_markedForDestroy = false;
    DirectX::XMFLOAT3 m_virtualWorldPos;

    float m_width = 0.0f;
    float m_height = 0.0f;
    int m_screenWidth = 0;
    int m_screenHeight = 0;

    static constexpr float MIN_SIZE = 150.0f;
    static constexpr float PIXEL_TO_UNIT_RATIO = 40.0f;

    float m_shrinkRate = 90.0f;
    float m_timeAlive = 0.0f;

    bool m_preparedForDestroy = false;

    bool m_isSleeping = false;
};

// Manager definition (tidak berubah, copy paste saja bagian class WindowShatterManager seperti sebelumnya)
class WindowShatterManager
{
public:
    static WindowShatterManager& Instance()
    {
        static WindowShatterManager instance;
        return instance;
    }

    void TriggerExplosion(DirectX::XMFLOAT2 centerWorldPos, int count = 8);
    void Update(float dt);
    void Clear();
    [[nodiscard]] const std::vector<std::unique_ptr<WindowShatter>>& GetShatters() const { return m_shatters; }
    [[nodiscard]] int GetActiveCount() const { return static_cast<int>(m_shatters.size()); }

    void PreloadExplosion(DirectX::XMFLOAT2 centerWorldPos, int count = 5);
    void WakeUpAll();

private:
    WindowShatterManager() = default;
    ~WindowShatterManager() { Clear(); }
    void SpawnSingleInstance(DirectX::XMFLOAT2 centerPos, int index, int totalCount);

private:
    std::vector<std::unique_ptr<WindowShatter>> m_shatters;
};