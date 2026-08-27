#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "IComponent.h"
#include "Transform.h"

// Represents a node in the Scene Graph. Owns its components and children.
class GameObject
{
public:
    // Mark explicit to prevent accidental implicit conversions from string literals.
    explicit GameObject(std::string_view name = "GameObject") noexcept;
    ~GameObject() = default;

    // Delete copy semantics. A GameObject owns unique resources (children/components),
    // so it cannot be trivially copied.
    GameObject(const GameObject&) = delete;
    GameObject& operator=(const GameObject&) = delete;

    // Move semantics are allowed
    GameObject(GameObject&&) = default;
    GameObject& operator=(GameObject&&) = default;

    // Recursively updates this object, its components, and its children
    void Update(float dt);

    // Iterates over components to draw their ImGui Inspector UI
    void DrawInspector();

    // Hierarchy / Tree Operations 
    void AddChild(std::unique_ptr<GameObject> child);

    // Changing parents requires reparenting logic to maintain tree integrity.
    void SetParent(GameObject* newParent) noexcept;

    [[nodiscard]] const std::vector<std::unique_ptr<GameObject>>& GetChildren() const noexcept { return m_children; }
    [[nodiscard]] GameObject* GetParent() const noexcept { return m_parent; }

    // Component Operations 

    // Variadic template function to forward arguments perfectly to the component constructor
    template<typename T, typename... Args>
    T* AddComponent(Args&&... args);

    // Retrieves the first component of type T, or nullptr if none exists
    template<typename T>
    [[nodiscard]] T* GetComponent() const noexcept;

    // Getters / Setters 
    [[nodiscard]] const std::string& GetName() const noexcept { return m_name; }
    void SetName(std::string_view name) { m_name = name; }

    [[nodiscard]] bool IsActive() const noexcept { return m_isActive; }
    void SetActive(bool active) noexcept { m_isActive = active; }

    // Transform is deliberately public for direct access 
    Transform transform{};

private:
    std::string m_name{};
    bool m_isActive{ true };
    GameObject* m_parent{ nullptr }; // Non-owning raw pointer (parent outlives child)

    std::vector<std::unique_ptr<GameObject>> m_children{};
    std::vector<std::unique_ptr<IComponent>> m_components{};
};

// Template Implementations 

template<typename T, typename... Args>
T* GameObject::AddComponent(Args&&... args)
{
    // Construct the component via std::make_unique, passing any arguments directly to T's constructor
    auto component{ std::make_unique<T>(std::forward<Args>(args)...) };

    T* rawPtr{ component.get() };
    component->OnAttach(this);

    m_components.push_back(std::move(component));
    return rawPtr;
}

template<typename T>
T* GameObject::GetComponent() const noexcept
{
    for (const auto& component : m_components)
    {
        // dynamic_cast safely checks if the component is of type T (or derived from T)
        if (T * casted{ dynamic_cast<T*>(component.get()) })
        {
            return casted;
        }
    }
    return nullptr;
}