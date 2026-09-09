#include "TemporalAAEffect.h"

TemporalAAEffect::TemporalAAEffect(ID3D11Device* device)
{
    GpuResourceUtils::LoadPixelShader(device, "Data/Shader/TemporalResolvePS.cso", m_pixelShader.GetAddressOf());
    GpuResourceUtils::CreateConstantBuffer(device, sizeof(CbTemporalAA), m_constantBuffer.GetAddressOf());

    D3D11_SAMPLER_DESC desc{};
    desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.MaxAnisotropy = 1; 
    desc.MinLOD = 0.0f;     
    desc.MaxLOD = 0.0f;
    device->CreateSamplerState(&desc, m_linearSampler.GetAddressOf());

    m_currentData.historyWeight = -1.0f;
    CreateHistoryBuffers(device, m_width, m_height);
}

void TemporalAAEffect::CreateHistoryBuffers(ID3D11Device* device, int width, int height)
{
    m_width = width;
    m_height = height;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    for (auto& buffer : m_history)
    {
        buffer.Reset();
        device->CreateTexture2D(&desc, nullptr, buffer.texture.GetAddressOf());
        device->CreateRenderTargetView(buffer.texture.Get(), nullptr, buffer.rtv.GetAddressOf());
        device->CreateShaderResourceView(buffer.texture.Get(), nullptr, buffer.srv.GetAddressOf());
    }
}

void TemporalAAEffect::OnResize(ID3D11Device* device, int width, int height) noexcept
{
    if (width == m_width && height == m_height) return;
    CreateHistoryBuffers(device, width, height);
    InvalidateHistory(); // stale history from the old resolution would reproject garbage otherwise
}

void TemporalAAEffect::Draw(ID3D11DeviceContext* dc, ID3D11ShaderResourceView* srcSRV)
{
    CbTemporalAA cb{};
    cb.historyWeight = m_historyValid ? std::clamp(m_data.historyWeight, 0.0f, 0.98f) : 0.0f;
    cb.sharpenAmount = m_data.sharpenAmount;
    cb.texelSize = { 1.0f / static_cast<float>(m_width), 1.0f / static_cast<float>(m_height) };
    dc->UpdateSubresource(m_constantBuffer.Get(), 0, nullptr, &cb, 0, 0);

    ID3D11ShaderResourceView* const historySRV{ GetReadSRV() };

    dc->PSSetShader(m_pixelShader.Get(), nullptr, 0);
    dc->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    dc->PSSetShaderResources(0, 1, &srcSRV);
    dc->PSSetShaderResources(3, 1, &historySRV);
    dc->PSSetSamplers(0, 1, m_linearSampler.GetAddressOf());
    dc->Draw(3, 0);

    m_historyValid = true;
}

void TemporalAAEffect::DrawGUI() noexcept
{
    if (ImGui::Checkbox("Enable Temporal AA", &m_data.enabled))
    {
        // Flush the history buffer so we start with a clean slate
        if (m_data.enabled) InvalidateHistory();
    }

    if (!m_data.enabled) return;

    ImGui::SliderFloat("History Weight", &m_data.historyWeight, 0.0f, 0.98f);
    ImGui::SliderFloat("Sharpen Amount", &m_data.sharpenAmount, 0.0f, 1.0f);
}

void TemporalAAEffect::Serialize(nlohmann::json& out) const
{
    out["enabled"] = m_data.enabled;
    out["historyWeight"] = m_data.historyWeight;
    out["sharpenAmount"] = m_data.sharpenAmount;
}

void TemporalAAEffect::Deserialize(const nlohmann::json& in)
{
    m_data.enabled = in.value("enabled", true);
    m_data.historyWeight = in.value("historyWeight", 0.90f); 
    m_data.sharpenAmount = in.value("sharpenAmount", 0.025f); 
}

void TemporalAAEffect::ResetToDefault() noexcept
{
    m_data = Data{};
}