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

// High-frequency spatial hash for zero-cost stochastic cascade selection resolved by TAA
float SpatialShadowDither(float3 p)
{
    float3 d = frac(p * float3(443.897f, 441.423f, 437.195f));
    d += dot(d, d.yzx + 19.19f);
    return frac((d.x + d.y) * d.z);
}

// Single unified hardware PCF lookup across Texture2DArray slices with smooth boundary rolloff
float CalculateCascadeShadow(float3 worldPos, float3 normal, float3 dirToLight)
{
    if (shadowSettings.x < 0.5f)
        return 1.0f;

    float3 toPixel = worldPos - cameraPosition.xyz;
    float viewZ = dot(toPixel, cameraDirection.xyz);

    // Beyond shadow boundary
    if (viewZ > cascadeSplits.z || viewZ < 0.0f)
        return 1.0f;

    // Smooth distance fade-out at the outer boundary (prevents pop-in line at cascadeSplits.z)
    float fadeStart = cascadeSplits.z * 0.85f;
    float shadowFade = saturate((cascadeSplits.z - viewZ) * rcp(max(cascadeSplits.z - fadeStart, 0.001f)));

    // Stochastic Dithered Cascade Selection (Zero extra texture taps, antialiased by TAA)
    int cascadeIndex = 0;
    float dither = SpatialShadowDither(worldPos);

    float split0Transition = cascadeSplits.x * 0.85f;
    float blend0 = saturate((viewZ - split0Transition) * rcp(max(cascadeSplits.x - split0Transition, 0.001f)));

    float split1Transition = cascadeSplits.y * 0.85f;
    float blend1 = saturate((viewZ - split1Transition) * rcp(max(cascadeSplits.y - split1Transition, 0.001f)));

    if (viewZ > cascadeSplits.y || (viewZ > split1Transition && dither < blend1))
    {
        cascadeIndex = 2;
    }
    else if (viewZ > cascadeSplits.x || (viewZ > split0Transition && dither < blend0))
    {
        cascadeIndex = 1;
    }

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
        float shadow = lerp(shadowSettings.y, 1.0f, litFactor);
        return lerp(1.0f, shadow, shadowFade);
    }

    return 1.0f;
}