#pragma once

#include <string_view>

// Forward declarations 
class EnemyManager;
class GameObject;
class ItemManager;

// Handles File I/O for saving and loading level configurations to JSON
// Implements pure static utility functions (Stateless)
class SceneSerializer
{
public:
    // Deleted constructors enforce that this is a static-only utility class
    SceneSerializer() = delete;
    ~SceneSerializer() = delete;

    // Serializes the current state of the managers to a JSON file on disk
    static void Save(std::string_view filepath, GameObject* sceneRoot, const EnemyManager* enemyMgr = nullptr, const ItemManager* itemMgr = nullptr);

    // Deserializes a JSON file from disk and pushes the data into the managers
    static void Load(std::string_view filepath, GameObject* sceneRoot, EnemyManager* enemyMgr = nullptr, ItemManager* itemMgr = nullptr);
};