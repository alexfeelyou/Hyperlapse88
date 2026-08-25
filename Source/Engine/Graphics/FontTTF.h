#pragma once

#include <d3d11.h>
#include <DirectXMath.h>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <wrl.h>
#include "System/GpuResourceUtils.h"
#include "System/Graphics.h"
#include "System/Misc.h"
#include "stb_truetype.h"

class Camera;

class FontTTF
{
public:
    struct GlyphInfo {
        float u0, v0, u1, v1;
        int width, height;
        int xOffset, yOffset;
        int xAdvance;
    };

    FontTTF();
    ~FontTTF() = default;

    bool Initialize(const std::string& ttfPath, float fontSize, const std::vector<uint32_t>& customCodepoints = {});
    void Draw(const std::string& utf8Text, float startX, float startY, float scale, DirectX::XMFLOAT4 color = { 1.0f, 1.0f, 1.0f, 1.0f });
    void Draw3D(const std::string& utf8Text, const Camera* camera, DirectX::XMFLOAT3 worldPos, float scale, DirectX::XMFLOAT4 color = { 1.0f, 1.0f, 1.0f, 1.0f });

    // Bind the dedicated blend state to prevent shared-state corruption (black artifacts)
    void BindRenderState(ID3D11DeviceContext* dc) const;

private:
    std::vector<uint32_t> GenerateDefaultGlyphList(const std::vector<uint32_t>& customKanji);
    uint32_t DecodeUTF8(const std::string& str, size_t& index);
    void CreateShadersAndBuffers(ID3D11Device* device);

private:
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_textureSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer>             m_vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>          m_inputLayout;
    Microsoft::WRL::ComPtr<ID3D11VertexShader>         m_vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>          m_pixelShader;
    Microsoft::WRL::ComPtr<ID3D11BlendState>           m_blendState{}; // Dedicated blend state for safe text rendering

    std::unordered_map<uint32_t, GlyphInfo> m_glyphDatabase;
    float m_lineHeight = 0.0f;
    float m_atlasSize = 1024.0f;
};