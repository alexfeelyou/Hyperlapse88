struct VS_OUT
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

Texture2D sceneTexture : register(t0);
Texture2D<float> depthTexture : register(t1);
SamplerState pointSampler : register(s0);

cbuffer CbFog : register(b0)
{
    row_major float4x4 inverseViewProjection;
    float4 cameraPosition;
    float4 fogColor; // Pre-linearized Horizon/Zenith RGB
    float4 sunInscatterColor; // Pre-linearized Sun Inscatter RGB (HDR scaled)
    float4 sunDirectionAndPhase; // xyz: Normalized direction to sun, w: Mie phase exponent
    float fogStart;
    float fogEnd;
    float heightFalloff;
    float heightOffset;
    int excludeSkybox;
    float3 padding;
};

float4 main(VS_OUT pin) : SV_TARGET
{
    float4 originalColor = sceneTexture.Sample(pointSampler, pin.texcoord);
    float depthRaw = depthTexture.Load(int3(pin.position.xy, 0));

    float2 ndcXY = pin.texcoord * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);

    // Skybox Integration Path (Hardware depth at far plane)
    if (depthRaw >= 0.99999f)
    {
        if (excludeSkybox)
        {
            return originalColor;
        }

        // Reconstruct infinite view ray for skybox atmospheric wash
        float4 farWorldH = mul(float4(ndcXY, 1.0f, 1.0f), inverseViewProjection);
        float3 skyRay = normalize(farWorldH.xyz * rcp(farWorldH.w) - cameraPosition.xyz);

        float sunAlignment = saturate(dot(skyRay, sunDirectionAndPhase.xyz));
        float miePhase = pow(sunAlignment, sunDirectionAndPhase.w);
        float3 inscatterColor = lerp(fogColor.rgb, sunInscatterColor.rgb, miePhase);

        // Soft horizon mist + golden sun forward wash (clear zenith, dreamy horizon)
        float horizonHaze = saturate(1.0f - max(0.0f, skyRay.y) * 3.5f) * 0.45f;
        float skyWash = saturate(horizonHaze + miePhase * 0.35f);

        float3 finalSky = lerp(originalColor.rgb, inscatterColor, skyWash);
        return float4(finalSky, originalColor.a);
    }

    // Geometry Atmospheric Depth Path
    float4 worldPosH = mul(float4(ndcXY, depthRaw, 1.0f), inverseViewProjection);
    float3 worldPos = worldPosH.xyz * rcp(worldPosH.w);

    // Single rsqrt computes both Euclidean radial distance and normalized view ray
    float3 toPixel = worldPos - cameraPosition.xyz;
    float distSq = dot(toPixel, toPixel);
    float invDist = rsqrt(max(distSq, 0.000001f));
    float physicalDistance = distSq * invDist;
    float3 viewRay = toPixel * invDist;

    // Smooth exponential optical depth curve
    float rangeFactor = saturate((physicalDistance - fogStart) * rcp(max(fogEnd - fogStart, 0.001f)));
    float distDensity = 1.0f - exp2(-rangeFactor * rangeFactor * 4.0f);

    // Exponential altitude attenuation (fast hardware exp2)
    float relativeHeight = max(0.0f, worldPos.y - heightOffset);
    float heightAtten = exp2(-relativeHeight * heightFalloff * 1.442695f);
    float fogFactor = saturate(distDensity * heightAtten);

    // Directional Sun Atmospheric Inscattering (Anisotropic Mie Forward Scattering)
    float sunAlignment = saturate(dot(viewRay, sunDirectionAndPhase.xyz));
    float miePhase = pow(sunAlignment, sunDirectionAndPhase.w);
    float3 inscatterColor = lerp(fogColor.rgb, sunInscatterColor.rgb, miePhase);

    float3 finalColor = lerp(originalColor.rgb, inscatterColor, fogFactor);
    return float4(finalColor, originalColor.a);
}