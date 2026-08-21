#include "Lambert.hlsli"

// Buffer Material (Slot 0) 
cbuffer CbMesh : register(b0)
{
    float4 materialColor;
};

// Buffer Object (Slot 2) 
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

    float3 N = normalize(pin.normal);
    float3 L = normalize(-lightDirection.xyz);
    float power = max(0, dot(L, N));

    power = power * 0.7 + 0.3f;

    color.rgb *= lightColor.rgb * power;

    return color;
}