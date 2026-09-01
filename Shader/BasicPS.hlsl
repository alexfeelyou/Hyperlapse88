#include "Basic.hlsli"

cbuffer CbMesh : register(b0)
{
    float4 materialColor;
    int alphaMode;
    float alphaCutoff;
    float2 meshPadding;
};

cbuffer CbObject : register(b2)
{
    float4 guiColor;
};

Texture2D DiffuseMap : register(t0);
SamplerState LinearSampler : register(s0);

float4 main(VS_OUT pin) : SV_TARGET
{
    float4 texColor = DiffuseMap.Sample(LinearSampler, pin.texcoord);
    float4 color = texColor * materialColor * guiColor;
    
    // Discard pixel before writing to render target if it fails alpha test
    if (alphaMode == 1)
    {
        clip(color.a - alphaCutoff);
    }
    else if (alphaMode == 2)
    {
        clip(color.a - 0.01f);
    }
    
    return color;
}