#pragma once

#include <algorithm>
#include <cmath>
#include <DirectXMath.h>
#include <memory>
#include "System/Misc.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <PxPhysicsAPI.h>
#include <cooking/PxCooking.h>
#include <geometry/PxTriangleMesh.h>
#include <geometry/PxTriangleMeshGeometry.h>
#include "System/ModelRenderer.h"
#include "System/ShapeRenderer.h" 
#include "System/PrimitiveRenderer.h"
#pragma comment(lib, "PhysXCooking_64.lib") 
#pragma comment(lib, "PhysXCommon_64.lib")
#pragma comment(lib, "PhysXFoundation_64.lib")

class GameObject;

struct DebugWallData {
    DirectX::XMFLOAT3 Position;
    DirectX::XMFLOAT3 Rotation;
    DirectX::XMFLOAT3 Scale;
    float WorldRadius;
};

struct DebugLineData {
    DirectX::XMFLOAT3 Position;
    DirectX::XMFLOAT3 Rotation;
    DirectX::XMFLOAT3 Scale;
};

struct SpatialHashGrid
{
	// Cell Size (in world units)
    static constexpr float CELL_SIZE = 10.0f;   // Larger cells = faster for large objects

    // Grid Bounds
    // X Range: Covers -100 to +100
    // Z Range: Covers -900 to +100
    static constexpr int COLS = 20;
    static constexpr int ROWS = 100;
    static constexpr float OFFSET_X = 100.0f; 
    static constexpr float OFFSET_Z = 900.0f; 

    std::vector<size_t> cells[COLS * ROWS];

    void Clear()
    {
        for (auto& cell : cells)
        {
            cell.clear();
        }
    }

    void Insert(const DirectX::XMFLOAT3& center, float radius, size_t index)
    {
        // Convert World Pos -> Grid Indices
        int minX = (int)((center.x - radius + OFFSET_X) / CELL_SIZE);
        int maxX = (int)((center.x + radius + OFFSET_X) / CELL_SIZE);
        int minZ = (int)((center.z - radius + OFFSET_Z) / CELL_SIZE);
        int maxZ = (int)((center.z + radius + OFFSET_Z) / CELL_SIZE);

        // Clamp to ensure we don't crash
        minX = (std::max)(0, (std::min)(minX, COLS - 1));
        maxX = (std::max)(0, (std::min)(maxX, COLS - 1));
        minZ = (std::max)(0, (std::min)(minZ, ROWS - 1));
        maxZ = (std::max)(0, (std::min)(maxZ, ROWS - 1));

        // Fill buckets
        for (int z = minZ; z <= maxZ; ++z)
        {
            for (int x = minX; x <= maxX; ++x)
            {
                cells[z * COLS + x].push_back(index);
            }
        }
    }

    // Standard Query
    std::vector<size_t> QueryRadius(const DirectX::XMFLOAT3& center, float radius) const
    {
        int minX = (int)((center.x - radius + OFFSET_X) / CELL_SIZE);
        int maxX = (int)((center.x + radius + OFFSET_X) / CELL_SIZE);
        int minZ = (int)((center.z - radius + OFFSET_Z) / CELL_SIZE);
        int maxZ = (int)((center.z + radius + OFFSET_Z) / CELL_SIZE);

        minX = (std::max)(0, (std::min)(minX, COLS - 1));
        maxX = (std::max)(0, (std::min)(maxX, COLS - 1));
        minZ = (std::max)(0, (std::min)(minZ, ROWS - 1));
        maxZ = (std::max)(0, (std::min)(maxZ, ROWS - 1));

        std::vector<size_t> results;
        // Reserve rough amount to avoid small reallocs
        results.reserve(16);

        for (int z = minZ; z <= maxZ; ++z)
        {
            for (int x = minX; x <= maxX; ++x)
            {
                const auto& cell = cells[z * COLS + x];
                results.insert(results.end(), cell.begin(), cell.end());
            }
        }

        std::sort(results.begin(), results.end());
        auto last = std::unique(results.begin(), results.end());
        results.erase(last, results.end());

        return results;
    }
};

namespace StageConfig
{
    static const char* MODEL_PATH = "Data/Model/Stage/ExampleStage.glb";
    static const DirectX::XMFLOAT3 DEFAULT_POS = { 0.0f, 0.0f, 0.0f };
    static const DirectX::XMFLOAT3 DEFAULT_ROT = { 0.0f, 0.0f, 0.1f };
    static const DirectX::XMFLOAT3 DEFAULT_SCALE = { 1.5f, 1.5f, 1.5f };
    static const DirectX::XMFLOAT4 DEFAULT_COLOR = { 0.8235f, 0.8235f, 0.8235f, 1.0f };

    static const DirectX::XMFLOAT3 WALL_DEFAULT_SCALE = { 1.0f, 1.0f, 1.0f };
    static const DirectX::XMFLOAT3 LINE_DEFAULT_SCALE = { 10.0f, 0.0f, 0.0f };

	// Debug Wall Data (Yellow) - For Debugging Purposes
    static const std::vector<DebugWallData> DEBUG_WALLS = 
    {
        
    };

	// Debug Line Data (Blue) - For Debugging Purposes
    static const std::vector<DebugLineData> DEBUG_LINES_VOID =
    {
        // Line Void 1
        { {-2.5,0.7,0.8}, {0,43.1,0}, {17.1,0,0} },

        // Line Void 2
        { {8.2,1.2,-4.4}, {0,0,0}, {8.1,0,0} },
        
        // Line Void 3
        { {-2.3,0.9,5.6}, {0,-51.2,0}, {5.9,0,0} }
    };

	// Debug Line Data (Red) - Disables Input/Mechanic
    static const std::vector<DebugLineData> DEBUG_LINES_DISABLE =
    {
        
    };

	// Debug Line Data (Green) - Enables Input/Mechanic
    static const std::vector<DebugLineData> DEBUG_LINES_ENABLE =
    {
        // Line Enable 1
        { {40.9,1.2,80.5}, {0,15.6,0}, {17.9,0,0} },
    };

	// Debug Line Data (Purple) - Checkpoint
    static const std::vector<DebugLineData> DEBUG_LINES_CHECKPOINT =
    {
        // Line CheckPoint 1
        { {20.7,1.2,42.1}, {0,0,0}, {15.3,0,0} },
    };
}

enum class DebugLineType { Void, Disable, Enable, Checkpoint };

class Stage
{
public:
    Stage(ID3D11Device* device);
    ~Stage();

    void UpdateTransform();
    void Render(ModelRenderer* renderer);
    void RenderDebug(ShapeRenderer* shapeRenderer, PrimitiveRenderer* primRenderer);

    // Dynamic Debug Tools
    void AddDebugWall();
    void AddDebugLine(DebugLineType type);
    void SetLineHighlight(DebugLineType type, int index) { m_highlightState = { type, index }; }
    void SetWallHighlight(int index) { m_highlightWallIndex = index; }
    void ClearLineHighlight() { m_highlightState.index = -1; }
    void ClearWallHighlight() { m_highlightWallIndex = -1; }

    // Physics Engine Hooks
    void InitPhysics(physx::PxPhysics* physics, physx::PxScene* scene, physx::PxMaterial* material);
    void RebuildPhysics();

    std::shared_ptr<Model> GetModel() const { return model; }
    const SpatialHashGrid& GetSpatialGrid() const { return m_spatialGrid; }

    // Bi-directional link to the Editor's GameObject
    void SetOwnerNode(GameObject* node) noexcept { m_ownerNode = node; }
    [[nodiscard]] GameObject* GetOwnerNode() const noexcept { return m_ownerNode; }

public:
    // Public variables for GUI editing
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 rotation;
    DirectX::XMFLOAT3 scale;
    DirectX::XMFLOAT4 color;

    std::vector<DebugWallData> m_debugWalls;
    std::vector<DebugLineData> m_linesVoid;
    std::vector<DebugLineData> m_linesDisable;
    std::vector<DebugLineData> m_linesEnable;
    std::vector<DebugLineData> m_linesCheckpoint;

private:
    GameObject* m_ownerNode{ nullptr }; 

    struct HighlightData {
        DebugLineType type = DebugLineType::Void; 
        int index = -1; 
    } m_highlightState;
    int m_highlightWallIndex = -1;

    void RebuildSpatialGrid();

    SpatialHashGrid m_spatialGrid;
    std::shared_ptr<Model> model;

    physx::PxPhysics* m_physics{ nullptr };
    physx::PxScene* m_scene{ nullptr };
    physx::PxMaterial* m_material{ nullptr };
    physx::PxRigidStatic* m_physxActor{ nullptr };

    std::vector<physx::PxTriangleMesh*> m_collisionMeshes;
};