#include "Stage.h"

using namespace DirectX;

Stage::Stage(ID3D11Device* device)
{
    // Load the model
    model = std::make_shared<Model>(device, StageConfig::MODEL_PATH);

    // Apply defaults
    position = StageConfig::DEFAULT_POS;
    rotation = StageConfig::DEFAULT_ROT;
    scale = StageConfig::DEFAULT_SCALE;
    color = StageConfig::DEFAULT_COLOR;

    // Initialize Debug Objects
    m_debugWalls.clear();
    for (const auto& originalWall : StageConfig::DEBUG_WALLS)
    {
        m_debugWalls.push_back(originalWall);
    }
    m_linesVoid = StageConfig::DEBUG_LINES_VOID;
    m_linesDisable = StageConfig::DEBUG_LINES_DISABLE;
    m_linesEnable = StageConfig::DEBUG_LINES_ENABLE;
    m_linesCheckpoint = StageConfig::DEBUG_LINES_CHECKPOINT;

    RebuildSpatialGrid();
    UpdateTransform();
}

Stage::~Stage()
{
    if (m_physxActor) {
        if (m_scene) m_scene->removeActor(*m_physxActor);
        m_physxActor->release();
        m_physxActor = nullptr;
    }
    for (auto* mesh : m_collisionMeshes) {
        if (mesh) mesh->release();
    }
    m_collisionMeshes.clear();
}

void Stage::InitPhysics(physx::PxPhysics* physics, physx::PxScene* scene, physx::PxMaterial* material)
{
    m_physics = physics;
    m_scene = scene;
    m_material = material;

    physx::PxTolerancesScale physxScale = m_physics->getTolerancesScale();
    physx::PxCookingParams params(physxScale);
    params.meshPreprocessParams |= physx::PxMeshPreprocessingFlag::eDISABLE_CLEAN_MESH;

    if (model) {
        for (const auto& mesh : model->GetMeshes()) {
            if (mesh.vertices.empty()) continue;

            // 頂点データの変換（コピーを最小限に抑える）
            std::vector<physx::PxVec3> bakedVertices;
            bakedVertices.reserve(mesh.vertices.size());
            XMMATRIX globalMat = XMLoadFloat4x4(&mesh.node->globalTransform);

            for (const auto& v : mesh.vertices) {
                XMVECTOR pos = XMVector3TransformCoord(XMLoadFloat3(&v.position), globalMat);
                XMFLOAT3 f; XMStoreFloat3(&f, pos);
                bakedVertices.emplace_back(f.x, f.y, f.z);
            }

            // メッシュ記述子の作成
            physx::PxTriangleMeshDesc meshDesc;
            meshDesc.points.count = static_cast<physx::PxU32>(bakedVertices.size());
            meshDesc.points.stride = sizeof(physx::PxVec3);
            meshDesc.points.data = bakedVertices.data();
            meshDesc.triangles.count = static_cast<physx::PxU32>(mesh.indices.size() / 3);
            meshDesc.triangles.stride = 3 * sizeof(uint32_t);
            meshDesc.triangles.data = mesh.indices.data();

            physx::PxTriangleMesh* triMesh = PxCreateTriangleMesh(
                params,
                meshDesc,
                physics->getPhysicsInsertionCallback()
            );

            if (triMesh) m_collisionMeshes.push_back(triMesh);
        }
    }

    RebuildPhysics();
}

void Stage::RebuildPhysics()
{
    // If physics isn't linked, abort
    if (!m_physics || !m_scene || !m_material) return;

    // Destroy the old actor if we are rebuilding from the GUI
    if (m_physxActor)
    {
        m_scene->removeActor(*m_physxActor);
        m_physxActor->release();
        m_physxActor = nullptr;
    }

    // Setup the Actor's Pose (Stage Rotation & Position)
    XMVECTOR q = XMQuaternionRotationRollPitchYaw(
        XMConvertToRadians(rotation.x),
        XMConvertToRadians(rotation.y),
        XMConvertToRadians(rotation.z)
    );
    XMFLOAT4 qF;
    XMStoreFloat4(&qF, q);
    physx::PxQuat pxQuat(qF.x, qF.y, qF.z, qF.w);
    physx::PxVec3 pxPos(position.x, position.y, position.z);

    // Create a single Static Actor for the entire stage
    m_physxActor = m_physics->createRigidStatic(physx::PxTransform(pxPos, pxQuat));
    _ASSERT_EXPR_A(m_physxActor != nullptr, "Failed to create Stage PhysX Actor!");

    // Apply the Stage's Scale dynamically to the Cached Meshes
    float sx = (std::max)(0.001f, scale.x);
    float sy = (std::max)(0.001f, scale.y);
    float sz = (std::max)(0.001f, scale.z);
    physx::PxMeshScale pxScale(physx::PxVec3(sx, sy, sz), physx::PxQuat(physx::PxIdentity));

    // Attach all cooked 3D meshes to the Static Actor
    for (physx::PxTriangleMesh* triMesh : m_collisionMeshes)
    {
        physx::PxTriangleMeshGeometry geom(triMesh, pxScale);
        physx::PxShape* shape = m_physics->createShape(geom, *m_material);

        if (shape)
        {
            m_physxActor->attachShape(*shape);

            shape->release();
        }
    }

    // Invisible Physx barriers for void line
    for (const auto& line : m_linesVoid)
    {
        const float halfLength{ line.Scale.x * 0.5f };
        const float halfHeight{ 50.0f }; 
        const float halfDepth{ 0.05f };   

        physx::PxBoxGeometry invisibleWallGeom(halfLength, halfHeight, halfDepth);
        physx::PxShape* wallShape{ m_physics->createShape(invisibleWallGeom, *m_material) };

        if (wallShape)
        {
            DirectX::XMVECTOR qRot{ DirectX::XMQuaternionRotationRollPitchYaw(
                DirectX::XMConvertToRadians(line.Rotation.x),
                DirectX::XMConvertToRadians(line.Rotation.y),
                DirectX::XMConvertToRadians(line.Rotation.z)
            ) };

            DirectX::XMFLOAT4 qF;
            DirectX::XMStoreFloat4(&qF, qRot);

            physx::PxQuat pxQuat(qF.x, qF.y, qF.z, qF.w);
            physx::PxVec3 pxPos(line.Position.x, line.Position.y, line.Position.z);

            physx::PxTransform localPose(pxPos, pxQuat);
            wallShape->setLocalPose(localPose);

            m_physxActor->attachShape(*wallShape);

            wallShape->release();
        }
    }

    // Add the fully constructed stage to the active scene simulation
    m_scene->addActor(*m_physxActor);
}

void Stage::RebuildSpatialGrid()
{
    m_spatialGrid.Clear();

    for (size_t i = 0; i < m_debugWalls.size(); ++i)
    {
        const auto& wall = m_debugWalls[i];

        // Calculate bounding radius (conservative estimate)
        float radiusX = wall.Scale.x;
        float radiusZ = wall.Scale.z;
        float boundingRadius = std::sqrt(radiusX * radiusX + radiusZ * radiusZ);

        // Insert into grid
        m_spatialGrid.Insert(wall.Position, boundingRadius, i);
    }
}

void Stage::UpdateTransform()
{
    if (!model) return;

    // Calculate World Matrix
    XMMATRIX S = XMMatrixScaling(scale.x, scale.y, scale.z);
    XMMATRIX R = XMMatrixRotationRollPitchYaw(
        XMConvertToRadians(rotation.x),
        XMConvertToRadians(rotation.y),
        XMConvertToRadians(rotation.z)
    );
    XMMATRIX T = XMMatrixTranslation(position.x, position.y, position.z);

    XMMATRIX world = S * R * T;

    XMFLOAT4X4 worldFloat;
    XMStoreFloat4x4(&worldFloat, world);

    // Apply to the model's root nodes
    model->UpdateTransform(worldFloat);
}

void Stage::Render(ModelRenderer* renderer)
{
    if (!model || !renderer) return;
    renderer->Draw(ShaderId::Phong, model, color);
}

void Stage::RenderDebug(ShapeRenderer* shapeRenderer, PrimitiveRenderer* primRenderer)
{
    if (shapeRenderer)
    {
        // Default Color (Green)
        DirectX::XMFLOAT4 defaultColor = { 0.0f, 1.0f, 0.0f, 1.0f };
        // Highlight Color (Yellow)
        DirectX::XMFLOAT4 highlightColor = { 1.0f, 1.0f, 0.0f, 1.0f };

        for (size_t i = 0; i < m_debugWalls.size(); ++i)
        {
            const auto& wall = m_debugWalls[i];
            DirectX::XMFLOAT4 drawColor = (m_highlightWallIndex == (int)i) ? highlightColor : defaultColor;
            DirectX::XMFLOAT3 rotRadians = {
                XMConvertToRadians(wall.Rotation.x),
                XMConvertToRadians(wall.Rotation.y),
                XMConvertToRadians(wall.Rotation.z)
            };

            // Use drawColor here
            shapeRenderer->DrawBox(wall.Position, rotRadians, wall.Scale, drawColor);
        }
    }

    // Draw Debug Lines
    if (primRenderer)
    {
        auto DrawLineList = [&](const std::vector<DebugLineData>& lines, DirectX::XMFLOAT4 defaultColor, DebugLineType listType)
            {
                for (int i = 0; i < lines.size(); ++i)
                {
                    const auto& line = lines[i];
                    DirectX::XMFLOAT4 finalColor = defaultColor;

                    if (m_highlightState.index == i && m_highlightState.type == listType)
                    {
                        finalColor = { 1.0f, 1.0f, 0.0f, 1.0f }; // Bright Yellow Highlight
                    }

                    XMMATRIX T = XMMatrixTranslation(line.Position.x, line.Position.y, line.Position.z);
                    XMMATRIX R = XMMatrixRotationRollPitchYaw(
                        XMConvertToRadians(line.Rotation.x),
                        XMConvertToRadians(line.Rotation.y),
                        XMConvertToRadians(line.Rotation.z)
                    );

                    float halfLen = line.Scale.x * 0.5f;
                    XMVECTOR p0 = XMVectorSet(-halfLen, 0, 0, 1);
                    XMVECTOR p1 = XMVectorSet(halfLen, 0, 0, 1);

                    XMMATRIX W = R * T;
                    p0 = XMVector3TransformCoord(p0, W);
                    p1 = XMVector3TransformCoord(p1, W);

                    XMFLOAT3 v0, v1;
                    XMStoreFloat3(&v0, p0);
                    XMStoreFloat3(&v1, p1);

                    primRenderer->AddVertex(v0, finalColor);
                    primRenderer->AddVertex(v1, finalColor);
                }
            };

        // Draw Lists with their Type identifier
        DrawLineList(m_linesVoid, { 0.0f, 1.0f, 1.0f, 1.0f }, DebugLineType::Void);         // Cyan
        DrawLineList(m_linesDisable, { 1.0f, 0.0f, 0.0f, 1.0f }, DebugLineType::Disable);   // Red
        DrawLineList(m_linesEnable, { 0.0f, 1.0f, 0.0f, 1.0f }, DebugLineType::Enable);     // Green
        DrawLineList(m_linesCheckpoint, { 0.2f, 0.4f, 1.0f, 1.0f }, DebugLineType::Checkpoint); // Bright Blue
    }
}

void Stage::AddDebugWall()
{
    DebugWallData newWall;

    if (!m_debugWalls.empty())
    {
        newWall = m_debugWalls.back();
    }
    else
    {
        // Fallback default if list is empty
        newWall.Position = { 0.0f, 0.0f, 0.0f };
        newWall.Rotation = { 0.0f, 0.0f, 0.0f };
        newWall.Scale = StageConfig::WALL_DEFAULT_SCALE;
    }

    m_debugWalls.push_back(newWall);
}

void Stage::AddDebugLine(DebugLineType type)
{
    DebugLineData newLine;
    newLine.Position = { 0,0,0 };
    newLine.Rotation = { 0,0,0 };
    newLine.Scale = StageConfig::LINE_DEFAULT_SCALE;

	// Determine which list to add to based on the type
    std::vector<DebugLineData>* targetList = nullptr;

    switch (type)
    {
    case DebugLineType::Void:       targetList = &m_linesVoid; break;
    case DebugLineType::Disable:    targetList = &m_linesDisable; break;
    case DebugLineType::Enable:     targetList = &m_linesEnable; break;
    case DebugLineType::Checkpoint: targetList = &m_linesCheckpoint; break;
    }

    if (targetList)
    {
        if (!targetList->empty()) newLine = targetList->back();
        targetList->push_back(newLine);
    }
}
