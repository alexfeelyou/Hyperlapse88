#include "Basic.hlsli"

cbuffer CbMesh : register(b0)
{
    float4 materialColor;
    int alphaMode;
    float alphaCutoff;
    int uvProjectionMode;
    float meshPadding;
    float2 uvTiling;
    float2 uvOffset;
};

Texture2D DiffuseMap : register(t0);
SamplerState LinearSampler : register(s0);

float4 main(VS_OUT pin) : SV_TARGET
{
    float2 uv = pin.texcoord * uvTiling + uvOffset;
    float4 texColor = DiffuseMap.Sample(LinearSampler, uv);
    float4 color = texColor * materialColor * pin.color;
    
    // Discard pixel before writing to render target if it fails alpha test
    if (alphaMode == 1)
    {
        clip(color.a - alphaCutoff);
    }
    else if (alphaMode == 2)
    {
        clip(color.a - 0.01f);
    }

    // Linearize unlit sRGB albedo to match the linear HDR scene target
    color.rgb = pow(abs(color.rgb), 2.2f);
    return color;
}