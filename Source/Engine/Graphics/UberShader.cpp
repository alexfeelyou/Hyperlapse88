#include "UberShader.h"
#include "System/GpuResourceUtils.h" 
#include "System/Misc.h"
#include <algorithm> 

UberShader::UberShader(ID3D11Device* device)
{
    // --------------------------------------------------------
    // Load Shaders
    // --------------------------------------------------------
    GpuResourceUtils::LoadVertexShader(device, "Data/Shader/UberPostProcessVS.cso", nullptr, 0, nullptr, vertexShader.GetAddressOf());
    GpuResourceUtils::LoadPixelShader(device, "Data/Shader/UberPostProcessPS.cso", pixelShader.GetAddressOf());

    // --------------------------------------------------------
    // Create Constant Buffer
    // --------------------------------------------------------
    GpuResourceUtils::CreateConstantBuffer(device, sizeof(CbUber), constantBuffer.GetAddressOf());

    // --------------------------------------------------------
    // Create Sampler State
    // --------------------------------------------------------
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    device->CreateSamplerState(&samplerDesc, samplerState.GetAddressOf());

    currentData.intensity = -1.0f; // Force update
}

void UberShader::Draw(ID3D11DeviceContext* dc, ID3D11ShaderResourceView* textureSRV, const UberData& data)
{

    UberData finalData = data;
	float juiceKick = 0.0f;
    finalData.glitchStrength += juiceKick;

    // --------------------------------------------------------
    // OPTIMIZATION: Update GPU Buffer only if data changed
    // --------------------------------------------------------
    if (!(currentData == finalData))
    {
        CbUber cb = {};
        cb.color = finalData.color;
        cb.center = finalData.center;
        cb.intensity = finalData.intensity * INTENSITY_FACTOR;
        cb.smoothness = (std::max)(SMOOTHNESS_MIN, data.smoothness * SMOOTHNESS_FACTOR);
        cb.rounded = data.rounded ? 1.0f : 0.0f;
        cb.roundness = ROUNDNESS_POWER * (1.0f - data.roundness) + data.roundness;

        cb.blurStrength = std::clamp(data.blurStrength, 0.0f, 0.1f);
        cb.chromaticAberration = finalData.chromaticAberration;
        cb.distortion = std::clamp(data.distortion, -0.2f, 0.2f);
        cb.glitchStrength = finalData.glitchStrength;
        cb.time = data.time;

        cb.scanlineStrength = data.scanlineStrength;
        cb.scanlineSpeed = data.scanlineSpeed;
        cb.scanlineSize = data.scanlineSize;

        cb.fineOpacity = data.fineOpacity;
        cb.fineDensity = data.fineDensity;
        cb.fineRotation = data.fineRotation;

        cb.bloomThreshold = data.bloomThreshold;
        cb.bloomIntensity = data.bloomIntensity;

        cb.psxEnabled = finalData.psxEnabled ? 1.0f : 0.0f;
        cb.psxResWidth = (std::max)(1.0f, finalData.psxResWidth); 
        cb.psxResHeight = (std::max)(1.0f, finalData.psxResHeight);
        cb.psxColorDepth = (std::max)(1.0f, finalData.psxColorDepth);
        cb.psxDitherStrength = finalData.psxDitherStrength;

        dc->UpdateSubresource(constantBuffer.Get(), 0, 0, &cb, 0, 0);
        currentData = finalData;
    }

    // --------------------------------------------------------
    // SETUP PIPELINE
    // --------------------------------------------------------
    dc->VSSetShader(vertexShader.Get(), nullptr, 0);
    dc->PSSetShader(pixelShader.Get(), nullptr, 0);
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    
    // OPTIMIZATION: Unbind Input Layout & Vertex Buffers
    dc->IASetInputLayout(nullptr);
    dc->IASetInputLayout(nullptr);
    ID3D11Buffer* nullBuffer = nullptr;
    UINT stride = 0, offset = 0;
    dc->IASetVertexBuffers(0, 1, &nullBuffer, &stride, &offset);

    // --------------------------------------------------------
    // BIND RESOURCES
    // --------------------------------------------------------
    dc->PSSetConstantBuffers(0, 1, constantBuffer.GetAddressOf());
    dc->PSSetShaderResources(0, 1, &textureSRV);
    dc->PSSetSamplers(0, 1, samplerState.GetAddressOf());

    // --------------------------------------------------------
    // DRAW FULL SCREEN TRIANGLE
    // --------------------------------------------------------
    dc->Draw(VERTEX_COUNT, 0);
}