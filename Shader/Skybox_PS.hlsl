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

Texture2D skyTexture : register(t0);
SamplerState skySampler : register(s0);

float4 main(VS_OUT pin) : SV_TARGET
{
    // Calculate normalized view vector from camera to world pixel
    float3 viewDir = normalize(pin.worldPosition.xyz - cameraPosition.xyz);
    
    // Spherical Mapping (Equirectangular)
    static const float PI = 3.14159265f;
    float latitude = (1.0f / (2.0f * PI)) * atan2(viewDir.z, viewDir.x) + 0.5f;
    float longitude = (1.0f / PI) * atan2(viewDir.y, length(viewDir.xz)) + 0.5f;
    
    // Sample Texture
    return skyTexture.Sample(skySampler, float2(1.0f - saturate(latitude), 1.0f - saturate(longitude)));
}