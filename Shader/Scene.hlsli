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

    // Calculate slope scale: 0.0 when light hits dead-on, 1.0 when grazing
    float NdotL = saturate(dot(normal, dirToLight));
    float slopeScale = 1.0f - NdotL;

    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        // NORMAL BIAS: Scale Normal Bias. 
        // Pushes the sample point out along the normal by a few centimeters on sloped walls to prevent acne,
        // but applies almost 0 push on flat floors to keep feet perfectly grounded.
        float normalOffset = slopeScale * 0.1f; // 10cm max push
        float3 biasedPos = worldPos + (normal * normalOffset);

        // Project the biased world position into Light NDC space
        float4 wvpPos = mul(float4(biasedPos, 1.0f), cascadeMatrices[i]);
        wvpPos.xyz /= wvpPos.w;

        // Convert NDC [-1, 1] to Texture UV [0, 1]
        float2 uv = wvpPos.xy * float2(0.5f, -0.5f) + 0.5f;

        // If the pixel falls within this cascade's bounds, calculate the shadow
        if (uv.x >= 0.0f && uv.x <= 1.0f &&
            uv.y >= 0.0f && uv.y <= 1.0f &&
            wvpPos.z >= 0.0f && wvpPos.z <= 1.0f)
        {
            // Apply the microscopic flat depth bias from the Inspector
            float testDepth = wvpPos.z - cascadeBias[i];

            float litFactor = CascadeShadowMaps[i].SampleCmpLevelZero(ShadowSampler, uv, testDepth);
            return lerp(shadowSettings.y, 1.0f, litFactor);
        }
    }

    return 1.0f; // Unshadowed if out of all bounds
}