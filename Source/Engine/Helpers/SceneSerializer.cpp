#include <json.hpp>
#include <filesystem>
#include <fstream>
#include "System/Logger.h"
#include "EnemyManager.h"
#include "GameObject.h"
#include "ItemManager.h"
#include "SceneSerializer.h"

void SceneSerializer::Save(std::string_view filepath, GameObject* sceneRoot, const EnemyManager* enemyMgr, const ItemManager* itemMgr)
{
    nlohmann::json root{};

    // Serialize Static Scene Objects (Player, Stage, Empty nodes)
    if (sceneRoot)
    {
        nlohmann::json sceneObjects = nlohmann::json::array();
        for (const auto& child : sceneRoot->GetChildren())
        {
            const std::string& name = child->GetName();

            // Exclude dynamically spawned enemies and items because their 
            // respective Managers already handle serializing them to prevent duplicates.
            if (name.find("Mushroom") != std::string::npos ||
                name.find("Item") != std::string::npos ||
                name.find("Paddle") != std::string::npos ||
                name.find("Ball") != std::string::npos ||
                name.find("FakeBoss") != std::string::npos)
            {
                continue;
            }

            nlohmann::json objJson{};
            objJson["Name"] = name;
            objJson["IsActive"] = child->IsActive();

            objJson["PosX"] = child->transform.position.x;
            objJson["PosY"] = child->transform.position.y;
            objJson["PosZ"] = child->transform.position.z;

            objJson["RotX"] = child->transform.rotation.x;
            objJson["RotY"] = child->transform.rotation.y;
            objJson["RotZ"] = child->transform.rotation.z;

            objJson["ScaleX"] = child->transform.scale.x;
            objJson["ScaleY"] = child->transform.scale.y;
            objJson["ScaleZ"] = child->transform.scale.z;

            sceneObjects.push_back(objJson);
        }
        root["SceneObjects"] = sceneObjects;
    }

    // Ask Managers to serialize their live data
    if (enemyMgr) enemyMgr->Serialize(root);
    if (itemMgr)  itemMgr->Serialize(root);

    // Ensure directory exists
    const std::filesystem::path pathObj{ filepath };
    const std::filesystem::path directory{ pathObj.parent_path() };

    if (!directory.empty() && !std::filesystem::exists(directory))
    {
        std::filesystem::create_directories(directory);
    }

    // Write to disk
    std::ofstream file{ std::string{ filepath } };
    if (file.is_open())
    {
        // Dump with an indent of 4 spaces for human readability
        file << root.dump(4);
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

        // Deserialize Static Scene Objects
        if (sceneRoot && root.contains("SceneObjects"))
        {
            for (const auto& objJson : root["SceneObjects"])
            {
                std::string name = objJson.value("Name", "");

                // Find the existing pre-spawned GameObject
                GameObject* targetNode = nullptr;
                for (const auto& child : sceneRoot->GetChildren())
                {
                    if (child->GetName() == name)
                    {
                        targetNode = child.get();
                        break;
                    }
                }

                // If found, update its transform. The Component Bridge will detect this 
                // change on the very first frame and push the logic down automatically.
                if (targetNode)
                {
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
                }
                else if (name == "Empty")
                {
                    // Recreate generic empty nodes that the user added manually via the Editor
                    auto emptyNode = std::make_unique<GameObject>("Empty");
                    emptyNode->SetActive(objJson.value("IsActive", true));

                    emptyNode->transform.position = {
                        objJson.value("PosX", 0.0f),
                        objJson.value("PosY", 0.0f),
                        objJson.value("PosZ", 0.0f)
                    };
                    emptyNode->transform.rotation = {
                        objJson.value("RotX", 0.0f),
                        objJson.value("RotY", 0.0f),
                        objJson.value("RotZ", 0.0f)
                    };
                    emptyNode->transform.scale = {
                        objJson.value("ScaleX", 1.0f),
                        objJson.value("ScaleY", 1.0f),
                        objJson.value("ScaleZ", 1.0f)
                    };

                    sceneRoot->AddChild(std::move(emptyNode));
                }
            }
        }

        // Dispatch JSON to Managers for reconstruction
        if (enemyMgr) enemyMgr->Deserialize(root);
        if (itemMgr)  itemMgr->Deserialize(root);

        Log::Success("Loaded Scene from: " + std::string{ filepath });
    }
    catch (const std::exception& e)
    {
        Log::Error("JSON parse error in " + std::string{ filepath } + ": " + e.what());
    }
}