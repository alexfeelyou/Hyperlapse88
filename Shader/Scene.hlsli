struct PointLightData
{
    float4 positionAndRange;
    float4 colorAndIntensity;
};

struct SpotLightData
{
    float4 positionAndRange;
    float4 directionAndAngle;
    float4 colorAndIntensity;
};

cbuffer CbScene : register(b7)
{
    row_major float4x4 viewProjection;
    float4 lightDirection;
    float4 lightColor;
    float4 cameraPosition;
    float4 cameraDirection;
    float4 ambientSkyColor;
    float4 ambientGroundColor;
    float4 packedParams;
    int4 lightCounts;
    
    row_major float4x4 cascadeMatrices[3];
    float4 cascadeSplits;
    float4 cascadeBias;
    float4 shadowSettings; // x: castShadows, y: attenuation
    
    PointLightData pointLights[8];
    SpotLightData spotLights[8];
};

// Bind the shadow textures and the special comparison sampler
Texture2DArray CascadeShadowMapArray : register(t10);
SamplerComparisonState ShadowSampler : register(s10);

// Single unified hardware PCF lookup across Texture2DArray slices
float CalculateCascadeShadow(float3 worldPos, float3 normal, float3 dirToLight)
{
    if (shadowSettings.x < 0.5f)
        return 1.0f;

    float3 toPixel = worldPos - cameraPosition.xyz;
    float viewZ = dot(toPixel, cameraDirection.xyz);

    // Beyond shadow boundary
    if (viewZ > cascadeSplits.z || viewZ < 0.0f)
        return 1.0f;

    int cascadeIndex = 0;
    if (viewZ > cascadeSplits.y)
        cascadeIndex = 2;
    else if (viewZ > cascadeSplits.x)
        cascadeIndex = 1;

    // Slope-scaled normal offset bias to eliminate surface acne
    float NdotL = saturate(dot(normal, dirToLight));
    float slopeScale = 1.0f - NdotL;
    float normalOffset = slopeScale * (0.04f * (float(cascadeIndex) + 1.0f));
    float3 biasedPos = worldPos + (normal * normalOffset);

    float4 wvpPos = mul(float4(biasedPos, 1.0f), cascadeMatrices[cascadeIndex]);
    float3 ndc = wvpPos.xyz / wvpPos.w;
    float2 uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;

    if (uv.x >= 0.0f && uv.x <= 1.0f && uv.y >= 0.0f && uv.y <= 1.0f && ndc.z <= 1.0f && ndc.z >= 0.0f)
    {
        float testDepth = ndc.z - cascadeBias[cascadeIndex];
        float litFactor = CascadeShadowMapArray.SampleCmpLevelZero(
            ShadowSampler,
            float3(uv, float(cascadeIndex)),
            testDepth
        );
        return lerp(shadowSettings.y, 1.0f, litFactor);
    }

    return 1.0f;
}