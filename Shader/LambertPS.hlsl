#include "Lambert.hlsli"

cbuffer CbMesh : register(b0)
{
    float4 materialColor;
    int alphaMode;
    float alphaCutoff;
    float2 meshPadding;
};

cbuffer CbObject : register(b2)
{
    float4 objectColor;
};

Texture2D DiffuseMap : register(t0);
SamplerState LinearSampler : register(s0);

float4 main(VS_OUT pin) : SV_TARGET
{
    float4 color = DiffuseMap.Sample(LinearSampler, pin.texcoord);
    color *= materialColor;
    color *= objectColor;

    if (alphaMode == 1)
    {
        clip(color.a - alphaCutoff);
    }
    else if (alphaMode == 2)
    {
        clip(color.a - 0.01f);
    }

    float3 N = normalize(pin.normal);
    float3 L = normalize(-lightDirection.xyz);
    float power = max(0.0f, dot(L, N));

    power = power * 0.7f + 0.3f;
    color.rgb *= lightColor.rgb * power;

    return color;
}