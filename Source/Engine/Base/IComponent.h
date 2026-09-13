#pragma once
#include <json.hpp>
#include <cstdint>

// Forward declaration
class GameObject; 
class ModelRenderer;
class ShapeRenderer;
class PrimitiveRenderer;
class Camera;

// Bitmask flags for filtering which gizmos to draw in the scene view
enum class GizmoCategory : std::uint32_t
{
    None = 0,
    Cameras = 1 << 0, // Bit 0: Camera Frustums & Icons
    StaticPhysics = 1 << 1, // Bit 1: Static Scene Colliders
    DynamicPhysics = 1 << 2, // Bit 2: Player/Enemy Capsules
    Hitboxes = 1 << 3, // Bit 3: Swords, Attack Sweeps
    Navigation = 1 << 4, // Bit 4: NavMesh / Waypoints
    All = ~0u     // All bits set to 1
};

struct GizmoContext
{
    ShapeRenderer* shapes{ nullptr };
    PrimitiveRenderer* primitives{ nullptr };
    const Camera* activeCamera{ nullptr };

    // The active filter mask for this frame
    std::uint32_t categoryMask{ static_cast<std::uint32_t>(GizmoCategory::All) };
};

// Base class for all components that can be attached to a GameObject
class IComponent
{
public:
    // Virtual destructor is mandatory for polymorphic base classes
    virtual ~IComponent() = default;

    // Called immediately when the component is added to a GameObject
    virtual void OnAttach(GameObject* owner) noexcept
    {
        m_owner = owner;
    }

    // Lifecycle hooks for the Active Checkbox
    virtual void OnEnable() noexcept {}
    virtual void OnDisable() noexcept {}

    // Standard frame update. Left empty by default so derived classes 
    // only override it if they actually need to tick.
    virtual void Update(float dt) {}

    // Executes during the GPU submission phase
    virtual void Render(ModelRenderer* renderer) {}

    // Hook for drawing Editor-only debug visuals (frustums, radii, etc.) in the scene view.
    virtual void DrawGizmo(const GizmoContext& ctx) noexcept {}

    // Hook for ImGui to draw variables specific to this component
    virtual void DrawInspector() = 0;

    // Returns a string identifier 
    [[nodiscard]] virtual const char* GetTypeName() const noexcept = 0;

    // Access the GameObject this component belongs to
    [[nodiscard]] GameObject* GetOwner() const noexcept { return m_owner; }

    // Polymorphic Serialization Hooks 
    virtual void Serialize(nlohmann::json& outJson) const {}
    virtual void Deserialize(const nlohmann::json& inJson) {}

protected:
    GameObject* m_owner{ nullptr };
};