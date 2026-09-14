#include <cmath>
#include <imgui.h>
#include "System/PhysicsManager.h"
#include "System/ShapeRenderer.h"
#include "CapsuleColliderComponent.h"
#include "ComponentRegistry.h"
#include "EditorManager.h"
#include "GameObject.h"

CapsuleColliderComponent::CapsuleColliderComponent(const CapsuleColliderConfig& config) noexcept
    : m_config{ config }
{}

CapsuleColliderComponent::~CapsuleColliderComponent() noexcept
{
    DestroyController();
}

void CapsuleColliderComponent::OnAttach(GameObject* owner) noexcept
{
    IComponent::OnAttach(owner);

    if (m_owner)
    {
        m_lastFramePos = m_owner->transform.position;
    }
}

void CapsuleColliderComponent::OnEnable() noexcept
{
    if (!m_controller)
    {
        CreateController();
    }
}

void CapsuleColliderComponent::OnDisable() noexcept
{
    DestroyController();
}

void CapsuleColliderComponent::Update(const float dt)
{
    if (!m_owner) return;

    // Lazy initialization guard: Ensure controller is created after hierarchy transforms settle
    if (!m_controller)
    {
        CreateController();
        if (!m_controller) return;
    }

    const EditorMode mode{ EditorManager::Instance().GetEditorMode() };

    // EDIT MODE: Hierarchy Transform is strictly authoritative.
    // Detect if designer modified the position via ImGuizmo or Inspector and teleport controller.
    if (mode == EditorMode::Edit)
    {
        if (m_isDirty)
        {
            CreateController();
            m_isDirty = false;
        }

        const DirectX::XMFLOAT3& currentPos{ m_owner->transform.position };
        const bool transformChanged{
            !IsFloatEqual(currentPos.x, m_lastFramePos.x) ||
            !IsFloatEqual(currentPos.y, m_lastFramePos.y) ||
            !IsFloatEqual(currentPos.z, m_lastFramePos.z)
        };

        if (transformChanged)
        {
            Teleport(currentPos);
            m_lastFramePos = currentPos;
        }
    }
}

physx::PxControllerCollisionFlags CapsuleColliderComponent::Move(const DirectX::XMFLOAT3& displacement, const float dt) noexcept
{
    if (!m_controller)
    {
        return physx::PxControllerCollisionFlags{ 0 };
    }

    constexpr float minStepDistance{ 0.001f };
    const physx::PxVec3 pxDisplacement{ displacement.x, displacement.y, displacement.z };

    // Standard controller filtering options: Collide with level geometry and other dynamic shapes
    physx::PxControllerFilters filters{};

    const physx::PxControllerCollisionFlags flags{
        m_controller->move(pxDisplacement, minStepDistance, dt, filters)
    };

    // Use PhysX's explicit isSet() method to avoid ambiguous implicit conversions
    m_isGrounded = flags.isSet(physx::PxControllerCollisionFlag::eCOLLISION_DOWN);

    // Read back solved position directly into GameObject Transform
    const DirectX::XMFLOAT3 footPos{ GetFootPosition() };
    m_owner->transform.position = footPos;
    m_lastFramePos = footPos;

    return flags;
}

void CapsuleColliderComponent::Teleport(const DirectX::XMFLOAT3& worldPos) noexcept
{
    if (!m_controller) return;

    // PhysX controller positions represent the center of the capsule
    const float centerOffsetY{ GetTotalHalfHeight() + m_config.localOffset.y };

    const physx::PxExtendedVec3 pxTargetPos{
        static_cast<physx::PxExtended>(worldPos.x + m_config.localOffset.x),
        static_cast<physx::PxExtended>(worldPos.y + centerOffsetY),
        static_cast<physx::PxExtended>(worldPos.z + m_config.localOffset.z)
    };

    m_controller->setPosition(pxTargetPos);
}

void CapsuleColliderComponent::Resize(const float radius, const float height) noexcept
{
    m_config.radius = (std::max)(0.01f, radius);
    m_config.height = (std::max)(0.01f, height);

    if (m_controller)
    {
        // In Play Mode, use PhysX non-destructive resize to maintain solver continuity
        auto* capsuleCtrl{ static_cast<physx::PxCapsuleController*>(m_controller) };
        capsuleCtrl->setRadius(m_config.radius);
        capsuleCtrl->resize(m_config.height);
    }
}

DirectX::XMFLOAT3 CapsuleColliderComponent::GetFootPosition() const noexcept
{
    if (!m_controller)
    {
        return m_owner ? m_owner->transform.position : DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };
    }

    const physx::PxExtendedVec3 centerPos{ m_controller->getPosition() };
    const float halfHeight{ GetTotalHalfHeight() };

    return DirectX::XMFLOAT3{
        static_cast<float>(centerPos.x) - m_config.localOffset.x,
        static_cast<float>(centerPos.y) - halfHeight - m_config.localOffset.y,
        static_cast<float>(centerPos.z) - m_config.localOffset.z
    };
}

DirectX::XMFLOAT3 CapsuleColliderComponent::GetCenterPosition() const noexcept
{
    if (!m_controller)
    {
        return m_owner ? m_owner->transform.position : DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };
    }

    const physx::PxExtendedVec3 centerPos{ m_controller->getPosition() };

    return DirectX::XMFLOAT3{
        static_cast<float>(centerPos.x),
        static_cast<float>(centerPos.y),
        static_cast<float>(centerPos.z)
    };
}

float CapsuleColliderComponent::GetTotalHalfHeight() const noexcept
{
    // Total capsule half-height = (cylinder height / 2) + hemisphere radius
    return (m_config.height * 0.5f) + m_config.radius;
}

void CapsuleColliderComponent::MarkDirty() noexcept
{
    m_isDirty = true;
}

void CapsuleColliderComponent::CreateController() noexcept
{
    DestroyController();

    if (!m_owner) return;

    auto* manager{ PhysicsManager::Instance().GetControllerManager() };
    auto* material{ PhysicsManager::Instance().GetDefaultMaterial() };

    if (!manager || !material) return;

    const DirectX::XMFLOAT3 basePos{ m_owner->transform.position };
    const float centerOffsetY{ GetTotalHalfHeight() + m_config.localOffset.y };

    physx::PxCapsuleControllerDesc desc{};
    desc.radius = (std::max)(0.01f, m_config.radius);
    desc.height = (std::max)(0.01f, m_config.height);
    desc.stepOffset = (std::max)(0.0f, m_config.stepOffset);
    desc.slopeLimit = std::cos(DirectX::XMConvertToRadians(m_config.slopeLimitDeg));
    desc.contactOffset = (std::max)(0.001f, m_config.contactOffset);
    desc.material = material;
    desc.upDirection = physx::PxVec3{ 0.0f, 1.0f, 0.0f };
    desc.climbingMode = physx::PxCapsuleClimbingMode::eCONSTRAINED;

    desc.position = physx::PxExtendedVec3{
        static_cast<physx::PxExtended>(basePos.x + m_config.localOffset.x),
        static_cast<physx::PxExtended>(basePos.y + centerOffsetY),
        static_cast<physx::PxExtended>(basePos.z + m_config.localOffset.z)
    };

    m_controller = manager->createController(desc);

    if (m_controller)
    {
        ApplyFilterData();
    }
}

void CapsuleColliderComponent::DestroyController() noexcept
{
    if (m_controller)
    {
        m_controller->release();
        m_controller = nullptr;
    }
}

void CapsuleColliderComponent::ApplyFilterData() const noexcept
{
    if (!m_controller) return;

    physx::PxRigidDynamic* actor{ m_controller->getActor() };
    if (!actor) return;

    physx::PxShape* shapes[1]{ nullptr };
    const physx::PxU32 shapeCount{ actor->getShapes(shapes, 1) };

    if (shapeCount > 0 && shapes[0])
    {
        physx::PxFilterData filterData{};
        filterData.word0 = m_config.layer;
        filterData.word1 = m_config.collidesWith;

        shapes[0]->setSimulationFilterData(filterData);
        shapes[0]->setQueryFilterData(filterData);
    }
}

void CapsuleColliderComponent::DrawGizmo(const GizmoContext& ctx) noexcept
{
    // Fast-fail if Character Physics Gizmo mask is toggled off in the editor toolbar
    if (!(ctx.categoryMask & static_cast<std::uint32_t>(GizmoCategory::DynamicPhysics)))
    {
        return;
    }

    if (!m_owner || !ctx.shapes) return;

    constexpr DirectX::XMFLOAT4 capsuleColor{ 0.1f, 0.8f, 1.0f, 0.6f };

    // Grab the authoritative World Matrix from the parent GameObject
    const Transform& t{ m_owner->transform };
    const DirectX::XMMATRIX objWorld{ DirectX::XMLoadFloat4x4(&t.GetWorldMatrix()) };

    // Apply the capsule's local offsets
    const float centerOffsetY{ GetTotalHalfHeight() + m_config.localOffset.y };
    const DirectX::XMMATRIX locTrans{ DirectX::XMMatrixTranslation(m_config.localOffset.x, centerOffsetY, m_config.localOffset.z) };

    // Multiply Local * World to get the final render matrix 
    const DirectX::XMMATRIX compWorld{ locTrans * objWorld };

    DirectX::XMFLOAT4X4 transformMatrix{};
    DirectX::XMStoreFloat4x4(&transformMatrix, compWorld);

    // Render debug capsule
    ctx.shapes->DrawCapsule(transformMatrix, m_config.radius, m_config.height, capsuleColor);
}

void CapsuleColliderComponent::DrawInspector()
{
    ImGui::TextDisabled("Kinematic Capsule Proxy");
    ImGui::Separator();

    bool configChanged{ false };

    configChanged |= ImGui::DragFloat("Radius", &m_config.radius, 0.01f, 0.05f, 10.0f, "%.2f m");
    configChanged |= ImGui::DragFloat("Height", &m_config.height, 0.01f, 0.05f, 20.0f, "%.2f m");
    configChanged |= ImGui::DragFloat("Step Offset", &m_config.stepOffset, 0.01f, 0.0f, 2.0f, "%.2f m");
    configChanged |= ImGui::SliderFloat("Slope Limit", &m_config.slopeLimitDeg, 0.0f, 89.0f, "%.1f deg");
    configChanged |= ImGui::DragFloat3("Local Offset", &m_config.localOffset.x, 0.02f);

    if (configChanged)
    {
        const EditorMode mode{ EditorManager::Instance().GetEditorMode() };
        if (mode == EditorMode::Edit)
        {
            MarkDirty(); // Defer to Update loop for rebuilding safely
        }
        else
        {
            Resize(m_config.radius, m_config.height);
        }
    }

    ImGui::Spacing();
    ImGui::TextDisabled("RUNTIME DIAGNOSTICS");
    ImGui::BeginDisabled();
    bool groundedCheck{ m_isGrounded };
    ImGui::Checkbox("Is Grounded", &groundedCheck);
    const DirectX::XMFLOAT3 footPos{ GetFootPosition() };
    ImGui::InputFloat3("Foot Position", const_cast<float*>(&footPos.x), "%.2f");
    ImGui::EndDisabled();
}

void CapsuleColliderComponent::Serialize(nlohmann::json& outJson) const
{
    outJson["Radius"] = m_config.radius;
    outJson["Height"] = m_config.height;
    outJson["StepOffset"] = m_config.stepOffset;
    outJson["SlopeLimitDeg"] = m_config.slopeLimitDeg;
    outJson["ContactOffset"] = m_config.contactOffset;

    outJson["LocalOffsetX"] = m_config.localOffset.x;
    outJson["LocalOffsetY"] = m_config.localOffset.y;
    outJson["LocalOffsetZ"] = m_config.localOffset.z;

    outJson["Layer"] = m_config.layer;
    outJson["CollidesWith"] = m_config.collidesWith;
}

void CapsuleColliderComponent::Deserialize(const nlohmann::json& inJson)
{
    m_config.radius = inJson.value("Radius", 0.5f);
    m_config.height = inJson.value("Height", 1.0f);
    m_config.stepOffset = inJson.value("StepOffset", 0.3f);
    m_config.slopeLimitDeg = inJson.value("SlopeLimitDeg", 45.0f);
    m_config.contactOffset = inJson.value("ContactOffset", 0.02f);

    m_config.localOffset = {
        inJson.value("LocalOffsetX", 0.0f),
        inJson.value("LocalOffsetY", 0.0f),
        inJson.value("LocalOffsetZ", 0.0f)
    };

    m_config.layer = inJson.value("Layer", CollisionLayer::Player);
    m_config.collidesWith = inJson.value("CollidesWith", CollisionLayer::Mask::Player);

    CreateController();
}

// Automatically register component with dynamic Inspector factory
REGISTER_COMPONENT(CapsuleColliderComponent)