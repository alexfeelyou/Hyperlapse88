#include <json.hpp>
#include <filesystem>
#include <fstream>
#include "System/Graphics.h"
#include "System/Logger.h"
#include "ComponentRegistry.h"
#include "EnemyManager.h"
#include "GameObject.h"
#include "ItemManager.h"
#include "SceneSerializer.h"

namespace
{
    // Evaluates if a specific node should be saved to disk
    [[nodiscard]] bool ShouldSaveNode(const std::string& name) noexcept
    {
        // Future data-driven exclusion logic (e.g., checking a m_serialize flag) can go here
        return true;
    }

    // Recursively serializes a GameObject, its components, and its children
    [[nodiscard]] nlohmann::json SerializeGameObject(const GameObject* node)
    {
        nlohmann::json objJson{};
        objJson["Name"] = node->GetName();
        objJson["IsActive"] = node->IsActive();

        // Always save the Transform
        objJson["PosX"] = node->transform.position.x;
        objJson["PosY"] = node->transform.position.y;
        objJson["PosZ"] = node->transform.position.z;

        objJson["RotX"] = node->transform.rotation.x;
        objJson["RotY"] = node->transform.rotation.y;
        objJson["RotZ"] = node->transform.rotation.z;

        objJson["ScaleX"] = node->transform.scale.x;
        objJson["ScaleY"] = node->transform.scale.y;
        objJson["ScaleZ"] = node->transform.scale.z;

        // Serialize Attached Components
        nlohmann::json componentsArray = nlohmann::json::array();
        for (const auto& comp : node->GetComponents())
        {
            nlohmann::json compJson{};
            compJson["Type"] = comp->GetTypeName();
            comp->Serialize(compJson);
            componentsArray.push_back(compJson);
        }
        objJson["Components"] = componentsArray;

        // Recursively serialize surviving children to maintain the exact hierarchy
        nlohmann::json childrenArray = nlohmann::json::array();
        for (const auto& child : node->GetChildren())
        {
            if (!ShouldSaveNode(child->GetName())) continue;
            childrenArray.push_back(SerializeGameObject(child.get()));
        }
        objJson["Children"] = childrenArray;

        return objJson;
    }

    // Recursively deserializes JSON into GameObjects and reconstructs the parent-child hierarchy
    void DeserializeGameObject(const nlohmann::json& objJson, GameObject* parentNode)
    {
        const std::string name{ objJson.value("Name", "GameObject") };

        // Check if the node already exists (e.g., hardcoded "Player")
        GameObject* targetNode{ nullptr };
        for (const auto& child : parentNode->GetChildren())
        {
            if (child->GetName() == name)
            {
                targetNode = child.get();
                break;
            }
        }

        // Instantiate dynamically if it doesn't exist
        if (!targetNode)
        {
            auto newNode = std::make_unique<GameObject>(name);
            targetNode = newNode.get(); // Cache raw pointer for population
            parentNode->AddChild(std::move(newNode));
        }

        // Load Transform state
        targetNode->SetActive(objJson.value("IsActive", true));
        targetNode->transform.position = {
            objJson.value("PosX", 0.0f),
            objJson.value("PosY", 0.0f),
            objJson.value("PosZ", 0.0f)
        };
        targetNode->transform.rotation = {
            objJson.value("RotX", 0.0f),
            objJson.value("RotY", 0.0f),
            objJson.value("RotZ", 0.0f)
        };
        targetNode->transform.scale = {
            objJson.value("ScaleX", 1.0f),
            objJson.value("ScaleY", 1.0f),
            objJson.value("ScaleZ", 1.0f)
        };

        // Instantiate and load Components via the Registry Factory
        if (objJson.contains("Components"))
        {
            for (const auto& compJson : objJson["Components"])
            {
                const std::string type{ compJson.value("Type", "") };

                // SEARCH for existing component first to prevent data loss
                bool componentExists = false;
                for (const auto& existingComp : targetNode->GetComponents())
                {
                    if (existingComp->GetTypeName() == type)
                    {
                        existingComp->Deserialize(compJson);
                        componentExists = true;
                        break;
                    }
                }

                // If it doesn't exist, create and attach a fresh one
                if (!componentExists)
                {
                    if (auto newComp{ ComponentRegistry::Create(type) })
                    {
                        newComp->Deserialize(compJson);
                        targetNode->AddComponent(std::move(newComp));
                    }
                }
            }
        }

        // Recurse downwards to build the children
        if (objJson.contains("Children"))
        {
            for (const auto& childJson : objJson["Children"])
            {
                DeserializeGameObject(childJson, targetNode);
            }
        }
    }
}

void SceneSerializer::Save(std::string_view filepath, GameObject* sceneRoot)
{
    nlohmann::json root{};

    if (sceneRoot)
    {
        nlohmann::json sceneObjects = nlohmann::json::array();

        // Trigger the recursive serialization starting from the root's children
        for (const auto& child : sceneRoot->GetChildren())
        {
            if (!ShouldSaveNode(child->GetName())) continue;
            sceneObjects.push_back(SerializeGameObject(child.get()));
        }

        root["SceneObjects"] = sceneObjects;
    }

    // Save Environment Illumination via global LightManager
    nlohmann::json envJson{};
    Graphics::Instance().GetLightManager().Serialize(envJson);
    root["EnvironmentIllumination"] = envJson;

    const std::filesystem::path pathObj{ filepath };
    const std::filesystem::path directory{ pathObj.parent_path() };

    if (!directory.empty() && !std::filesystem::exists(directory))
    {
        std::filesystem::create_directories(directory);
    }

    std::ofstream file{ std::string{ filepath } };
    if (file.is_open())
    {
        file << root.dump(4, ' ', false, nlohmann::json::error_handler_t::replace);
        Log::Success("Saved Scene to: " + std::string{ filepath });
    }
    else
    {
        Log::Error("Failed to open file for saving: " + std::string{ filepath });
    }
}

void SceneSerializer::Load(std::string_view filepath, GameObject* sceneRoot)
{
    std::ifstream file{ std::string{ filepath } };
    if (!file.is_open())
    {
        Log::Warn("Scene file not found (Starting empty): " + std::string{ filepath });
        return;
    }

    try
    {
        nlohmann::json root{};
        file >> root;

        if (sceneRoot && root.contains("SceneObjects"))
        {
            // Mark existing dynamic objects for destruction prior to load
            for (const auto& child : sceneRoot->GetChildren())
            {
                const std::string& name{ child->GetName() };

                // Player are preserved so we can attach saved children/components to them
                if (name != "Player")
                {
                    child->Destroy();
                }
                else
                {
                    // Flag all children of persistent nodes for destruction 
                    // This prevents Socket Anchors and Lights from duplicating on reload
                    for (const auto& subChild : child->GetChildren())
                    {
                        subChild->Destroy();
                    }
                }
            }

            // Force the GameObject to process the deletions immediately
            sceneRoot->Update(0.0f);

            // Trigger the recursive deserialization
            for (const auto& objJson : root["SceneObjects"])
            {
                DeserializeGameObject(objJson, sceneRoot);
            }
        }

        // Load Environment Illumination into global LightManager
        if (root.contains("EnvironmentIllumination"))
        {
            Graphics::Instance().GetLightManager().Deserialize(root["EnvironmentIllumination"]);
        }

        Log::Success("Loaded Scene from: " + std::string{ filepath });
    }
    catch (const std::exception& e)
    {
        Log::Error("JSON parse error in " + std::string{ filepath } + ": " + e.what());
    }
}