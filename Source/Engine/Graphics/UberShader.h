#pragma once

#include <cstring>
#include <DirectXMath.h>
#include "System/Shader.h"

class UberShader : public Shader
{
public:
    static constexpr bool  DEFAULT_ENABLED      = false;
    static constexpr float DEFAULT_INTENSITY = 0.0f;
    static constexpr float DEFAULT_SMOOTHNESS = 0.0f; 
    static constexpr bool  DEFAULT_ROUNDED = false;
    static constexpr float DEFAULT_ROUNDNESS = 0.0f; 

    static constexpr float DEFAULT_COLOR_R = 0.0f;
    static constexpr float DEFAULT_COLOR_G = 0.0f;
    static constexpr float DEFAULT_COLOR_B = 0.0f;

    static constexpr float INTENSITY_FACTOR = 3.0f;
    static constexpr float SMOOTHNESS_FACTOR = 5.0f;
    static constexpr float SMOOTHNESS_MIN = 0.000001f;
    static constexpr float ROUNDNESS_POWER = 6.0f;
    static constexpr UINT  VERTEX_COUNT = 3;
    static constexpr bool  DEFAULT_PSX_ENABLED{ false };
    static constexpr float DEFAULT_PSX_RES_WIDTH{ 0.0f };
    static constexpr float DEFAULT_PSX_RES_HEIGHT{ 0.0f };
    static constexpr float DEFAULT_PSX_COLOR_DEPTH{ 0.0f };
    static constexpr float DEFAULT_PSX_DITHER{ 0.0f };

    // =========================================================
    // DATA STRUCTURE 
    // =========================================================
    struct UberData
    {
        bool              enabled               = DEFAULT_ENABLED;
        DirectX::XMFLOAT4 color                 = { DEFAULT_COLOR_R, DEFAULT_COLOR_G, DEFAULT_COLOR_B, 1.0f };
        DirectX::XMFLOAT2 center                = { 0.0f, 0.0f };
        float             intensity             = DEFAULT_INTENSITY;
        float             smoothness            = DEFAULT_SMOOTHNESS;
        bool              rounded               = DEFAULT_ROUNDED;
        float             roundness             = DEFAULT_ROUNDNESS;

        // Effects
        float             blurStrength = 0.0f;
        float             distortion = 0.0f;
        float             glitchStrength = 0.0f;
        float             chromaticAberration = 0.0f;
        float             time = 0.0f;

        // Scanlines
        float             scanlineStrength = 0.0f;
        float             scanlineSpeed = 0.0f;
        float             scanlineSize = 0.0f;
        float             fineOpacity = 0.0f;
        float             fineDensity = 0.0f;
        float             fineRotation = 0.0f;

		// PSX Emulation
        bool  psxEnabled        { DEFAULT_PSX_ENABLED };
        float psxResWidth       { DEFAULT_PSX_RES_WIDTH };
        float psxResHeight      { DEFAULT_PSX_RES_HEIGHT };
        float psxColorDepth     { DEFAULT_PSX_COLOR_DEPTH };
        float psxDitherStrength { DEFAULT_PSX_DITHER };

        bool operator==(const UberData& other) const
        {
            auto IsEqual = [](float a, float b) { return std::abs(a - b) < 0.0001f; };

            return enabled == other.enabled &&
                IsEqual(color.x, other.color.x) && IsEqual(color.y, other.color.y) && IsEqual(color.z, other.color.z) &&
                IsEqual(center.x, other.center.x) && IsEqual(center.y, other.center.y) &&
                IsEqual(intensity, other.intensity) &&
                IsEqual(smoothness, other.smoothness) &&
                rounded == other.rounded &&
                IsEqual(roundness, other.roundness) &&
                IsEqual(blurStrength, other.blurStrength) &&
                IsEqual(distortion, other.distortion) &&
                IsEqual(glitchStrength, other.glitchStrength) &&
                IsEqual(chromaticAberration, other.chromaticAberration) &&
                IsEqual(time, other.time) &&
                IsEqual(scanlineStrength, other.scanlineStrength) &&
                IsEqual(scanlineSpeed, other.scanlineSpeed) &&
                IsEqual(scanlineSize, other.scanlineSize) &&
                IsEqual(fineOpacity, other.fineOpacity) &&
                IsEqual(fineDensity, other.fineDensity) &&
                IsEqual(fineRotation, other.fineRotation) &&
                psxEnabled == other.psxEnabled &&
                IsEqual(psxResWidth, other.psxResWidth) &&
                IsEqual(psxResHeight, other.psxResHeight) &&
                IsEqual(psxColorDepth, other.psxColorDepth) &&
                IsEqual(psxDitherStrength, other.psxDitherStrength);
        }
    };

    // =========================================================
    // PUBLIC METHODS
    // =========================================================
    UberShader(ID3D11Device* device);
    ~UberShader() override = default;

    void Begin(const RenderContext& rc) override {}
    void Update(const RenderContext& rc, const Model::Mesh& mesh) override {}
    void End(const RenderContext& rc) override {}

    void Draw(ID3D11DeviceContext* dc, ID3D11ShaderResourceView* textureSRV, const UberData& data);

private:
    // =========================================================
    // INTERNAL BUFFER 
    // =========================================================
    struct CbUber
    {
        DirectX::XMFLOAT4 color;
        DirectX::XMFLOAT2 center;
        float intensity;
        float smoothness;
        float rounded;
        float roundness;
        float blurStrength;
        float chromaticAberration;
        float distortion;
        float glitchStrength;
        float scanlineStrength;
        float time;
        float scanlineSpeed;
        float scanlineSize;
        float fineOpacity;
        float fineDensity;
        float fineRotation;
        float psxEnabled;
        float psxResWidth;
        float psxResHeight;
        float psxColorDepth;
        float psxDitherStrength;
        float padding_psx[5];
    };

    Microsoft::WRL::ComPtr<ID3D11VertexShader>  vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>   pixelShader;
    Microsoft::WRL::ComPtr<ID3D11Buffer>        constantBuffer;
    Microsoft::WRL::ComPtr<ID3D11SamplerState>  samplerState;

    UberData currentData = {};
};