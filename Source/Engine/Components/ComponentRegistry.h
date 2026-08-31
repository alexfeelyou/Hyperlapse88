#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "IComponent.h"

// Centralized factory registry to enable dynamic "Add Component" in the Inspector
// Designed as a static-only utility class
class ComponentRegistry
{
public:
    using FactoryFunc = std::function<std::unique_ptr<IComponent>()>;

    ComponentRegistry() = delete;
    ~ComponentRegistry() = delete;

    // Registers a component type with its creation lambda
    static void Register(std::string_view name, FactoryFunc factory)
    {
        auto& registry{ GetRegistry() };
        auto& names{ GetRegisteredNames() };

        const std::string nameStr{ name };

        if (registry.find(nameStr) == registry.end())
        {
            registry.emplace(nameStr, std::move(factory));
            names.push_back(nameStr);
        }
    }

    // Instantiates a component by name. Returns nullptr if not found.
    [[nodiscard]] static std::unique_ptr<IComponent> Create(std::string_view name)
    {
        const auto& registry{ GetRegistry() };
        if (const auto it{ registry.find(std::string{ name }) }; it != registry.end())
        {
            return it->second(); // Invoke the factory lambda
        }
        return nullptr;
    }

    // Returns a read-only list of all registered component names for the ImGui dropdown
    [[nodiscard]] static const std::vector<std::string>& GetAvailableNames() noexcept
    {
        return GetRegisteredNames();
    }

private:
    [[nodiscard]] static std::unordered_map<std::string, FactoryFunc>& GetRegistry() noexcept
    {
        static std::unordered_map<std::string, FactoryFunc> s_registry{};
        return s_registry;
    }

    [[nodiscard]] static std::vector<std::string>& GetRegisteredNames() noexcept
    {
        static std::vector<std::string> s_names{};
        return s_names;
    }
};

// Helper macro 
// Place this inside the .cpp file of any component you want exposed to the Editor.
#define REGISTER_COMPONENT(Type) \
    namespace { \
        const bool s_##Type##_Registered = []() { \
            ComponentRegistry::Register(#Type, []() -> std::unique_ptr<IComponent> { \
                return std::make_unique<Type>(); \
            }); \
            return true; \
        }(); \
    }