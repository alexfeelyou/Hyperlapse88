#pragma once
#include <DirectXMath.h>
#include <memory>
#include <string>
#include "System/ModelRenderer.h"
#include "System/ShapeRenderer.h"
#include "System/Model.h"
#include "CharacterMovement.h"

class Camera;

class Character
{
public:
    Character();
    virtual ~Character() = default;
    virtual void Update(float elapsedTime, Camera* camera) = 0;

    // Standard render pipeline
    void Render(ModelRenderer* renderer);
    void RenderDebug(const RenderContext& rc, ShapeRenderer* renderer);

    DirectX::XMFLOAT3 GetPosition() const { return movement->GetPosition(); }

    // Returns raw pointer for read access — ownership stays here
    CharacterMovement* GetMovement() const { return movement.get(); }

    DirectX::XMFLOAT3 scale = { 1.0f, 1.0f, 1.0f };

protected:
    // Syncs movement state to the visual model's root node
    void SyncData();

protected:
    std::unique_ptr<CharacterMovement> movement;
    std::shared_ptr<Model>             model;
};