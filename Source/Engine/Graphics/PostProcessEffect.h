#pragma once

#include <algorithm>
#include <d3d11.h>
#include <DirectXMath.h>
#include <json.hpp>
#include <string_view>
#include <wrl/client.h>
#include "System/GpuResourceUtils.h"

// Base abstract class defining the contract for all discrete post-process passes
// Adheres strictly to the Interface Segregation and Single Responsibility principles
class PostProcessEffect
{
public:
    virtual ~PostProcessEffect() = default;

    // Evaluates whether this specific effect should execute a GPU draw pass
    [[nodiscard]] virtual bool IsEnabled() const noexcept = 0;

    // Renders the effect using the supplied source texture into whatever RTV is currently bound
    virtual void Draw(ID3D11DeviceContext* dc, ID3D11ShaderResourceView* srcSRV) = 0;

    // Renders ImGui debug controls for this effect in the EditorManager
    virtual void DrawGUI() noexcept = 0;

    // Returns a human-readable identifier for debugging and profiling
    [[nodiscard]] virtual std::string_view GetName() const noexcept = 0;

    // Serialization Interface
    // Serializes this effect's unique data to a JSON object
    virtual void Serialize(nlohmann::json& out) const = 0;

    // Safely loads this effect's data from a JSON object
    virtual void Deserialize(const nlohmann::json& in) = 0;

    // Instantly reverts the active state to the hardcoded C++ defaults
    virtual void ResetToDefault() noexcept = 0;
};