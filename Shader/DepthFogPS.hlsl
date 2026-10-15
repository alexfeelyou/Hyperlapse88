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

    // Mask out the skybox (drawn at depth 1.0) when excludeSkybox is enabled
    if (excludeSkybox && depthRaw >= 0.99999f)
    {
        return originalColor;
    }

    // Reconstruct World-Space Position from NDC and hardware depth
    float2 ndcXY = pin.texcoord * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
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

    // Exponential altitude attenuation 
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