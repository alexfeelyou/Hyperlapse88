#define STB_TRUETYPE_IMPLEMENTATION
#include "FontTTF.h"

struct FontVertex {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT4 color;
    DirectX::XMFLOAT2 texcoord;
};

FontTTF::FontTTF() {}

bool FontTTF::Initialize(const std::string& ttfPath, float fontSize, const std::vector<uint32_t>& customCodepoints)
{
    std::ifstream file(ttfPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<unsigned char> ttfBuffer(size);
    if (!file.read((char*)ttfBuffer.data(), size)) return false;

    std::vector<uint32_t> codepoints = GenerateDefaultGlyphList(customCodepoints);
    std::vector<unsigned char> atlasPixels(static_cast<size_t>(m_atlasSize * m_atlasSize), 0);

    stbtt_pack_context packContext;
    if (!stbtt_PackBegin(&packContext, atlasPixels.data(), (int)m_atlasSize, (int)m_atlasSize, 0, 1, nullptr)) return false;

    stbtt_PackSetOversampling(&packContext, 1, 1);

    std::vector<stbtt_packedchar> packedChars(codepoints.size());
    stbtt_pack_range range;
    range.font_size = fontSize;
    range.first_unicode_codepoint_in_range = 0;
    range.array_of_unicode_codepoints = (int*)codepoints.data();
    range.num_chars = static_cast<int>(codepoints.size());
    range.chardata_for_range = packedChars.data();

    if (!stbtt_PackFontRanges(&packContext, ttfBuffer.data(), 0, &range, 1)) {
        stbtt_PackEnd(&packContext);
        return false;
    }
    stbtt_PackEnd(&packContext);

    m_lineHeight = fontSize + 10.0f;
    for (size_t i = 0; i < codepoints.size(); ++i) {
        uint32_t cp = codepoints[i];
        const auto& pc = packedChars[i];

        GlyphInfo glyph;
        glyph.u0 = pc.x0 / m_atlasSize;
        glyph.v0 = pc.y0 / m_atlasSize;
        glyph.u1 = pc.x1 / m_atlasSize;
        glyph.v1 = pc.y1 / m_atlasSize;
        glyph.width = pc.x1 - pc.x0;
        glyph.height = pc.y1 - pc.y0;
        glyph.xOffset = static_cast<int>(pc.xoff);
        glyph.yOffset = static_cast<int>(pc.yoff);
        glyph.xAdvance = static_cast<int>(pc.xadvance);

        m_glyphDatabase[cp] = glyph;
    }

    std::vector<uint32_t> rgbaPixels(static_cast<size_t>(m_atlasSize * m_atlasSize));
    for (size_t i = 0; i < rgbaPixels.size(); ++i) {
        unsigned char alpha = atlasPixels[i];
        rgbaPixels[i] = (alpha << 24) | (0xFF << 16) | (0xFF << 8) | 0xFF;
    }

    auto device = Graphics::Instance().GetDevice();
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = (UINT)m_atlasSize;
    texDesc.Height = (UINT)m_atlasSize;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA subData = {};
    subData.pSysMem = rgbaPixels.data();
    subData.SysMemPitch = (UINT)m_atlasSize * sizeof(uint32_t);

    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    HRESULT hr = device->CreateTexture2D(&texDesc, &subData, texture.GetAddressOf());
    if (FAILED(hr)) return false;

    hr = device->CreateShaderResourceView(texture.Get(), nullptr, m_textureSRV.GetAddressOf());
    if (FAILED(hr)) return false;

    CreateShadersAndBuffers(device);
    return true;
}

std::vector<uint32_t> FontTTF::GenerateDefaultGlyphList(const std::vector<uint32_t>& customKanji)
{
    std::vector<uint32_t> list;
    // Basic ASCII 
    for (uint32_t cp = 32; cp <= 126; ++cp) list.push_back(cp);

    // CJK Symbols & Punctuation (〜, 。, 、, 「」)
    for (uint32_t cp = 0x3000; cp <= 0x303F; ++cp) list.push_back(cp);

    // Hiragana 
    for (uint32_t cp = 0x3040; cp <= 0x309F; ++cp) list.push_back(cp);
    // Katakana 
    for (uint32_t cp = 0x30A0; cp <= 0x30FF; ++cp) list.push_back(cp);
    // Katakana Phonetic Extensions
    for (uint32_t cp = 0xFF00; cp <= 0xFFEF; ++cp) list.push_back(cp);

    // CJK Unified Ideographs (Kanji)
    for (uint32_t cp : customKanji) {
        if (std::find(list.begin(), list.end(), cp) == list.end()) {
            list.push_back(cp);
        }
    }
    return list;
}

uint32_t FontTTF::DecodeUTF8(const std::string& str, size_t& i)
{
    unsigned char c = str[i];
    if (c < 0x80) { i += 1; return c; }
    if ((c & 0xE0) == 0xC0) {
        if (i + 1 >= str.size()) { i += 1; return 0; }
        uint32_t res = ((c & 0x1F) << 6) | (str[i + 1] & 0x3F);
        i += 2; return res;
    }
    if ((c & 0xF0) == 0xE0) {
        if (i + 2 >= str.size()) { i += 1; return 0; }
        uint32_t res = ((c & 0x0F) << 12) | ((str[i + 1] & 0x3F) << 6) | (str[i + 2] & 0x3F);
        i += 3; return res;
    }
    if ((c & 0xF8) == 0xF0) {
        if (i + 3 >= str.size()) { i += 1; return 0; }
        uint32_t res = ((c & 0x07) << 18) | ((str[i + 1] & 0x3F) << 12) | ((str[i + 2] & 0x3F) << 6) | (str[i + 3] & 0x3F);
        i += 4; return res;
    }
    i += 1; return 0;
}

void FontTTF::CreateShadersAndBuffers(ID3D11Device* device)
{
    D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    GpuResourceUtils::LoadVertexShader(device, "Data/Shader/SpriteVS.cso", layoutDesc, ARRAYSIZE(layoutDesc), m_inputLayout.GetAddressOf(), m_vertexShader.GetAddressOf());
    GpuResourceUtils::LoadPixelShader(device, "Data/Shader/SpritePS.cso", m_pixelShader.GetAddressOf());

    D3D11_BUFFER_DESC bufDesc = {};
    bufDesc.ByteWidth = sizeof(FontVertex) * 6 * 1000; // 1 Character = 2 Triangles (6 Vertex List)
    bufDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bufDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = device->CreateBuffer(&bufDesc, nullptr, m_vertexBuffer.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

    // Create a dedicated blend state for Font rendering to avoid relying on external context state
    {
        D3D11_BLEND_DESC blendDesc{};
        blendDesc.RenderTarget[0].BlendEnable = TRUE;
        blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

        hr = device->CreateBlendState(&blendDesc, m_blendState.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
    }
}

void FontTTF::Draw(const std::string& utf8Text, float startX, float startY, float scale, DirectX::XMFLOAT4 color)
{
    if (utf8Text.empty() || !m_textureSRV) return;

    std::vector<FontVertex> vertices;
    float cursorX = startX;
    float cursorY = startY;

    size_t charIdx = 0;
    while (charIdx < utf8Text.size()) {
        uint32_t codepoint = DecodeUTF8(utf8Text, charIdx);

        if (codepoint == '\n') {
            cursorX = startX;
            cursorY += m_lineHeight * scale;
            continue;
        }

        if (m_glyphDatabase.find(codepoint) == m_glyphDatabase.end()) continue;
        const auto& glyph = m_glyphDatabase[codepoint];

        float x0 = cursorX + (glyph.xOffset * scale);
        float y0 = cursorY + (glyph.yOffset * scale);
        float x1 = x0 + (glyph.width * scale);
        float y1 = y0 + (glyph.height * scale);

        auto dc = Graphics::Instance().GetDeviceContext();
        D3D11_VIEWPORT viewport;
        UINT numViewports = 1;
        dc->RSGetViewports(&numViewports, &viewport);
        float screenW = viewport.Width;
        float screenH = viewport.Height;

        float ndcX0 = (2.0f * x0 / screenW) - 1.0f;
        float ndcY0 = 1.0f - (2.0f * y0 / screenH);
        float ndcX1 = (2.0f * x1 / screenW) - 1.0f;
        float ndcY1 = 1.0f - (2.0f * y1 / screenH);

        FontVertex vTL = { {ndcX0, ndcY0, 0.0f}, color, {glyph.u0, glyph.v0} };
        FontVertex vTR = { {ndcX1, ndcY0, 0.0f}, color, {glyph.u1, glyph.v0} };
        FontVertex vBL = { {ndcX0, ndcY1, 0.0f}, color, {glyph.u0, glyph.v1} };
        FontVertex vBR = { {ndcX1, ndcY1, 0.0f}, color, {glyph.u1, glyph.v1} };

        vertices.push_back(vTL); vertices.push_back(vTR); vertices.push_back(vBL);
        vertices.push_back(vTR); vertices.push_back(vBR); vertices.push_back(vBL);

        cursorX += glyph.xAdvance * scale;
    }

    if (vertices.empty()) return;

    auto dc = Graphics::Instance().GetDeviceContext();
    D3D11_MAPPED_SUBRESOURCE ms;
    if (SUCCEEDED(dc->Map(m_vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        memcpy(ms.pData, vertices.data(), sizeof(FontVertex) * vertices.size());
        dc->Unmap(m_vertexBuffer.Get(), 0);
    }

    UINT stride = sizeof(FontVertex);
    UINT offset = 0;
    dc->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
    dc->IASetInputLayout(m_inputLayout.Get());
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    dc->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    dc->PSSetShader(m_pixelShader.Get(), nullptr, 0);
    dc->PSSetShaderResources(0, 1, m_textureSRV.GetAddressOf());

    // Bind the dedicated blend state to guarantee correct alpha blending on every font draw call
    BindRenderState(dc);

    dc->Draw(static_cast<UINT>(vertices.size()), 0);
}

void FontTTF::Draw3D(const std::string& utf8Text, const Camera* camera, DirectX::XMFLOAT3 worldPos, float scale, DirectX::XMFLOAT4 color)
{
    if (utf8Text.empty() || !m_textureSRV || !camera) return;

    std::vector<FontVertex> vertices;
    float cursorX = 0.0f;
    float cursorY = 0.0f;

    using namespace DirectX;

    XMMATRIX matVP = XMLoadFloat4x4(&camera->GetView()) * XMLoadFloat4x4(&camera->GetProjection());

    size_t charIdx = 0;
    while (charIdx < utf8Text.size()) {
        uint32_t codepoint = DecodeUTF8(utf8Text, charIdx);
        if (codepoint == '\n') {
            cursorX = 0.0f;
            cursorY -= m_lineHeight * scale;
            continue;
        }

        if (m_glyphDatabase.find(codepoint) == m_glyphDatabase.end()) continue;
        const auto& glyph = m_glyphDatabase[codepoint];

        float x0 = cursorX + (glyph.xOffset * scale);
        float y0 = cursorY - (glyph.yOffset * scale);
        float x1 = x0 + (glyph.width * scale);
        float y1 = y0 - (glyph.height * scale);

        XMFLOAT3 localPos[4] = {
                    { x0, 0.0f, y0 }, // TL 
                    { x1, 0.0f, y0 }, // TR 
                    { x0, 0.0f, y1 }, // BL 
                    { x1, 0.0f, y1 }  // BR 
        };

        FontVertex v[4];
        for (int i = 0; i < 4; ++i) {
            XMVECTOR vPos = XMLoadFloat3(&localPos[i]) + XMLoadFloat3(&worldPos);
            XMVECTOR vClip = XMVector3Transform(vPos, matVP);

            float vW = XMVectorGetW(vClip);
            if (vW < 0.0001f) vW = 0.0001f;

            XMStoreFloat3(&v[i].position, vClip / vW);
            v[i].color = color;
        }

        v[0].texcoord = { glyph.u0, glyph.v0 };
        v[1].texcoord = { glyph.u1, glyph.v0 };
        v[2].texcoord = { glyph.u0, glyph.v1 };
        v[3].texcoord = { glyph.u1, glyph.v1 };

        vertices.push_back(v[0]); vertices.push_back(v[1]); vertices.push_back(v[2]);
        vertices.push_back(v[1]); vertices.push_back(v[3]); vertices.push_back(v[2]);

        cursorX += glyph.xAdvance * scale;
    }

    if (vertices.empty()) return;

    auto dc = Graphics::Instance().GetDeviceContext();
    D3D11_MAPPED_SUBRESOURCE ms;
    if (SUCCEEDED(dc->Map(m_vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        memcpy(ms.pData, vertices.data(), sizeof(FontVertex) * vertices.size());
        dc->Unmap(m_vertexBuffer.Get(), 0);
    }

    UINT stride = sizeof(FontVertex);
    UINT offset = 0;
    dc->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
    dc->IASetInputLayout(m_inputLayout.Get());
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    dc->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    dc->PSSetShader(m_pixelShader.Get(), nullptr, 0);
    dc->PSSetShaderResources(0, 1, m_textureSRV.GetAddressOf());

    BindRenderState(dc);

    dc->Draw(static_cast<UINT>(vertices.size()), 0);
}

void FontTTF::BindRenderState(ID3D11DeviceContext* dc) const
{
    // Straight alpha blending — required since transparent font atlas areas 
    // carry uninitialized/black RGB data that would otherwise overwrite the framebuffer incorrectly
    float blendFactor[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
    dc->OMSetBlendState(m_blendState.Get(), blendFactor, 0xFFFFFFFF);
}