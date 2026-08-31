#pragma once

#include <DirectXMath.h>
#include <memory>
#include <string>
#include "System/ModelRenderer.h"
#include "System/ShapeRenderer.h"
#include "System/Model.h"
#include "CharacterMovement.h"

class Camera;
class GameObject;

class Character
{
public:
    Character();
    virtual ~Character() = default;
    virtual void Update(float elapsedTime, Camera* camera) = 0;

    // Standard render pipeline
    void Render(ModelRenderer* renderer);
    void RenderDebug(const RenderContext& rc, ShapeRenderer* renderer);

    [[nodiscard]] DirectX::XMFLOAT3 GetPosition() const { return movement->GetPosition(); }
    [[nodiscard]] CharacterMovement* GetMovement() const { return movement.get(); }

    // Virtual transform setters
    // Allows LegacyCharacterComponent to update generic Characters, while giving 
    // derived classes (like Player) the hook they need to update PhysX controllers simultaneously
    virtual void SetPosition(const DirectX::XMFLOAT3& pos) noexcept { if (movement) movement->SetPosition(pos); }
    virtual void SetRotation(const DirectX::XMFLOAT3& rot) noexcept { if (movement) movement->SetRotation(rot); }

    // Forces an immediate push of movement data to the visual model
    void ForceVisualSync() noexcept { SyncData(); }

    DirectX::XMFLOAT3 scale = { 1.0f, 1.0f, 1.0f };

    [[nodiscard]] virtual bool IsActive() const noexcept { return true; }

    // Editor visibility syncing
    void SetOwnerNode(GameObject* node) noexcept { m_ownerNode = node; }
    [[nodiscard]] GameObject* GetOwnerNode() const noexcept { return m_ownerNode; }

protected:
    // Syncs movement state to the visual model's root node
    void SyncData() noexcept;

protected:
    std::unique_ptr<CharacterMovement>  movement;
    std::shared_ptr<Model>              model;
    GameObject*                         m_ownerNode{ nullptr }; // Tracks the Inspector GameObject
};