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

void SceneSerializer::Save(std::string_view filepath, GameObject* sceneRoot, const EnemyManager* enemyMgr, const ItemManager* itemMgr)
{
    nlohmann::json root{};

    if (sceneRoot)
    {
        nlohmann::json sceneObjects = nlohmann::json::array();
        for (const auto& child : sceneRoot->GetChildren())
        {
            const std::string& name = child->GetName();

            // Skip runtime spawned entities managed by specialized systems
            if (name.find("Mushroom") != std::string::npos ||
                name.find("Item") != std::string::npos ||
                name.find("Paddle") != std::string::npos ||
                name.find("Ball") != std::string::npos ||
                name.find("FakeBoss") != std::string::npos ||
                name.find("Player") != std::string::npos) // Let player load from specific logic
            {
                continue;
            }

            nlohmann::json objJson{};
            objJson["Name"] = name;
            objJson["IsActive"] = child->IsActive();

            // ALWAYS save the Transform, regardless of what components are attached
            objJson["PosX"] = child->transform.position.x;
            objJson["PosY"] = child->transform.position.y;
            objJson["PosZ"] = child->transform.position.z;

            objJson["RotX"] = child->transform.rotation.x;
            objJson["RotY"] = child->transform.rotation.y;
            objJson["RotZ"] = child->transform.rotation.z;

            objJson["ScaleX"] = child->transform.scale.x;
            objJson["ScaleY"] = child->transform.scale.y;
            objJson["ScaleZ"] = child->transform.scale.z;

            // Serialize Attached Components
            nlohmann::json componentsArray = nlohmann::json::array();
            for (const auto& comp : child->GetComponents())
            {
                nlohmann::json compJson{};
                compJson["Type"] = comp->GetTypeName();
                comp->Serialize(compJson);
                componentsArray.push_back(compJson);
            }
            objJson["Components"] = componentsArray;

            sceneObjects.push_back(objJson);
        }
        root["SceneObjects"] = sceneObjects;
    }

    if (enemyMgr) enemyMgr->Serialize(root);
    if (itemMgr)  itemMgr->Serialize(root);

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

void SceneSerializer::Load(std::string_view filepath, GameObject* sceneRoot, EnemyManager* enemyMgr, ItemManager* itemMgr)
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
            // Mark existing dynamic objects for destruction 
            for (const auto& child : sceneRoot->GetChildren())
            {
                const std::string& name = child->GetName();
                if (name != "Player" && name != "Stage")
                {
                    child->Destroy();
                }
            }

            // Force the GameObject to process the deletions immediately
            sceneRoot->Update(0.0f);

            for (const auto& objJson : root["SceneObjects"])
            {
                std::string name = objJson.value("Name", "GameObject");

                // Check if the node already exists (e.g., hardcoded "Stage" node)
                GameObject* targetNode = nullptr;
                for (const auto& child : sceneRoot->GetChildren())
                {
                    if (child->GetName() == name)
                    {
                        targetNode = child.get();
                        break;
                    }
                }

                // If it doesn't exist, we must instantiate a brand new GameObject dynamically
                if (!targetNode)
                {
                    auto newNode = std::make_unique<GameObject>(name);
                    targetNode = newNode.get(); // Keep a pointer to populate it
                    sceneRoot->AddChild(std::move(newNode));
                }

                // ALWAYS load the Transform for the node
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

                // Deserialize Components dynamically via the Registry Factory
                if (objJson.contains("Components"))
                {
                    for (const auto& compJson : objJson["Components"])
                    {
                        std::string type = compJson.value("Type", "");

                        if (auto newComp{ ComponentRegistry::Create(type) })
                        {
                            newComp->Deserialize(compJson);
                            targetNode->AddComponent(std::move(newComp));
                        }
                    }
                }
            }
        }

        if (enemyMgr) enemyMgr->Deserialize(root);
        if (itemMgr)  itemMgr->Deserialize(root);

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