#include "Basic.hlsli"

cbuffer CbMesh : register(b0)
{
    float4 materialColor;
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
    
    return texColor * materialColor * guiColor;
}