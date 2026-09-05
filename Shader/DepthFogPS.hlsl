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
    float4 fogColor;
    float fogStart;
    float fogEnd;
    float nearZ;
    float farZ;
    int excludeSkybox;
    float3 padding;
};

float4 main(VS_OUT pin) : SV_TARGET
{
    float4 originalColor = sceneTexture.Sample(pointSampler, pin.texcoord);
    float depthRaw = depthTexture.Load(int3(pin.position.xy, 0));

    // Mask out the skybox (drawn at depth 1.0)
    if (excludeSkybox && depthRaw >= 0.99999f)
    {
        return originalColor;
    }

    float linearDepth = (2.0f * nearZ) / (farZ + nearZ - depthRaw * (farZ - nearZ));
    float physicalDistance = linearDepth * farZ;

    float fogFactor = saturate((physicalDistance - fogStart) / (fogEnd - fogStart));
    float3 finalColor = lerp(originalColor.rgb, fogColor.rgb, fogFactor);

    return float4(finalColor, originalColor.a);
}