struct VS_OUT
{
    float4 position : SV_POSITION;
    float4 worldPosition : WORLD_POSITION;
};

cbuffer SKYMAP_CONSTANT_BUFFER : register(b7)
{
    row_major float4x4 inverseViewProjection;
    float4 cameraPosition;
};

// TextureCube
TextureCube skyTexture : register(t0);
SamplerState skySampler : register(s0);

float4 main(VS_OUT pin) : SV_TARGET
{
    float3 viewDir = normalize(pin.worldPosition.xyz - cameraPosition.xyz);
    return skyTexture.Sample(skySampler, viewDir);
}