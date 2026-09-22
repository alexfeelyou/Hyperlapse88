#pragma once

#include "IComponent.h"

class VirtualCameraComponent;

// Drives a sibling VirtualCameraComponent's orbit yaw/pitch from per-frame device input.
// Owns nothing about the orbit math itself (that lives on VirtualCameraComponent) and
// nothing about mouse/stick specifics (that lives in CameraInput) — this component's only
// job is wiring the two together once per frame, plus the cursor-lock lifecycle that comes
// with an active third-person orbit camera.
class OrbitCameraDriverComponent final : public IComponent
{
public:
    OrbitCameraDriverComponent() noexcept = default;
    ~OrbitCameraDriverComponent() override;

    OrbitCameraDriverComponent(const OrbitCameraDriverComponent&) = delete;
    OrbitCameraDriverComponent& operator=(const OrbitCameraDriverComponent&) = delete;

    void OnAttach(GameObject* owner) noexcept override;
    void Update(float dt) override;
    void DrawInspector() override;

    // Releases the cursor lock immediately if this component is toggled off mid-play,
    // rather than leaving the cursor hidden with nothing driving the camera anymore.
    void OnDisable() noexcept override;

    // Global read access to the camera yaw gameplay code should resolve movement/facing
    // against. Self-registering, single-active-instance pattern
    [[nodiscard]] static float GetActiveYawRadians() noexcept;

    [[nodiscard]] const char* GetTypeName() const noexcept override { return "OrbitCameraDriverComponent"; }

private:
    VirtualCameraComponent* m_orbitCamera{ nullptr };
    bool m_isCaptured{ true };

    // Non-owning. Set in OnAttach, cleared in the destructor — mirrors
    // VirtualCameraComponent's own s_registry add/remove-on-destroy pattern.
    static inline OrbitCameraDriverComponent* s_activeInstance{ nullptr };
};