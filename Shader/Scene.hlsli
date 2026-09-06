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
    
    row_major float4x4 cascadeMatrices[4];
    float4 cascadeSplits;
    float4 cascadeBias;
    float4 shadowSettings; // x: castShadows, y: attenuation
    
    PointLightData pointLights[8];
    SpotLightData spotLights[8];
};

// Bind the shadow textures and the special comparison sampler
Texture2D CascadeShadowMaps[4] : register(t10);
SamplerComparisonState ShadowSampler : register(s10);

// Single reusable function for all lit pixel shaders
float CalculateCascadeShadow(float3 worldPos, float3 normal, float3 dirToLight)
{
    // shadowSettings.x represents the Cast Shadows toggle (0.0 = off, 1.0 = on)
    if (shadowSettings.x < 0.5f)
        return 1.0f;

    // Calculate View-Space Depth (Distance along camera plane)
    float3 toPixel = worldPos - cameraPosition.xyz;
    float viewZ = dot(toPixel, cameraDirection.xyz);

    // Cascade Selection (Eliminates Matrix Loop ALU Waste)
    int cascadeIndex = 0;
    if (viewZ > cascadeSplits.w)
        return 1.0f; // Beyond Far Plane
    else if (viewZ > cascadeSplits.z)
        cascadeIndex = 3;
    else if (viewZ > cascadeSplits.y)
        cascadeIndex = 2;
    else if (viewZ > cascadeSplits.x)
        cascadeIndex = 1;

    // Slope-Scaled Normal Bias
    float NdotL = saturate(dot(normal, dirToLight));
    float slopeScale = 1.0f - NdotL;
    float normalOffset = slopeScale * 0.1f;
    float3 biasedPos = worldPos + (normal * normalOffset);

    // Project into Light Space
    float4 wvpPos = mul(float4(biasedPos, 1.0f), cascadeMatrices[cascadeIndex]);
    wvpPos.xyz /= wvpPos.w;

    float2 uv = wvpPos.xy * float2(0.5f, -0.5f) + 0.5f;

    // Hardware PCF shadow sampling
    if (uv.x >= 0.0f && uv.x <= 1.0f && uv.y >= 0.0f && uv.y <= 1.0f && wvpPos.z <= 1.0f)
    {
        float testDepth = wvpPos.z - cascadeBias[cascadeIndex];
        float litFactor = 1.0f;

        // Static unrolled branch to index Texture2D arrays across all D3D11 feature levels
        if (cascadeIndex == 0)
            litFactor = CascadeShadowMaps[0].SampleCmpLevelZero(ShadowSampler, uv, testDepth);
        else if (cascadeIndex == 1)
            litFactor = CascadeShadowMaps[1].SampleCmpLevelZero(ShadowSampler, uv, testDepth);
        else if (cascadeIndex == 2)
            litFactor = CascadeShadowMaps[2].SampleCmpLevelZero(ShadowSampler, uv, testDepth);
        else if (cascadeIndex == 3)
            litFactor = CascadeShadowMaps[3].SampleCmpLevelZero(ShadowSampler, uv, testDepth);

        return lerp(shadowSettings.y, 1.0f, litFactor);
    }

    return 1.0f; // Unshadowed if out of all bounds
}