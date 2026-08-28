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

    // Ask the base Scene Graph to serialize itself (We will implement this next)
    // if (sceneRoot) sceneRoot->Serialize(root);
    
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

        // Dispatch JSON to Managers for reconstruction
        // if (sceneRoot) sceneRoot->Deserialize(root); // (To be implemented)
        if (enemyMgr) enemyMgr->Deserialize(root);
        if (itemMgr)  itemMgr->Deserialize(root);

        Log::Success("Loaded Scene from: " + std::string{ filepath });
    }
    catch (const std::exception& e)
    {
        Log::Error("JSON parse error in " + std::string{ filepath } + ": " + e.what());
    }
}