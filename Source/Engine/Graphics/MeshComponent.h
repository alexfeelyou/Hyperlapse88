#pragma once

#include <DirectXMath.h>
#include <memory>
#include "System/ModelRenderer.h" 
#include "GameObject.h"
#include "IComponent.h"

// Forward declaration 
class Model; 

// Handles binding a 3D Model to a GameObject and rendering it to the scene
class MeshComponent final : public IComponent
{
public:
    // Explicitly require a model pointer; a mesh component is useless without geometry.
    explicit MeshComponent(std::shared_ptr<Model> model) noexcept;
    ~MeshComponent() override = default;

    MeshComponent(const MeshComponent&) = delete;
    MeshComponent& operator=(const MeshComponent&) = delete;

    void Render(ModelRenderer* renderer) override;
    void DrawInspector() override;

    [[nodiscard]] const char* GetTypeName() const noexcept override { return "MeshComponent"; }

    // Mutators
    void SetShader(ShaderId shader) noexcept { m_shaderId = shader; }
    void SetColor(const DirectX::XMFLOAT4& color) noexcept { m_color = color; }

private:
    std::shared_ptr<Model> m_model{};
    ShaderId               m_shaderId{ ShaderId::Phong };
    DirectX::XMFLOAT4      m_color{ 1.0f, 1.0f, 1.0f, 1.0f };
};