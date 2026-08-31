#pragma once

// Forward declaration
class GameObject; 
class ModelRenderer;

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

    // Standard frame update. Left empty by default so derived classes 
    // only override it if they actually need to tick.
    virtual void Update(float dt) {}

    // Executes during the GPU submission phase.
    // Left empty by default so logical components (like Colliders) don't need to implement it.
    virtual void Render(ModelRenderer* renderer) {}

    // Hook for ImGui to draw variables specific to this component
    virtual void DrawInspector() = 0;

    // Returns a string identifier 
    [[nodiscard]] virtual const char* GetTypeName() const noexcept = 0;

    // Access the GameObject this component belongs to
    [[nodiscard]] GameObject* GetOwner() const noexcept { return m_owner; }

protected:
    GameObject* m_owner{ nullptr };
};