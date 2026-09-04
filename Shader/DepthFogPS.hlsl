struct VS_OUT
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

// Slots configured by PostProcessManager
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
};

float4 main(VS_OUT pin) : SV_TARGET
{
    // Retrieve scene pixel data
    float4 originalColor = sceneTexture.Sample(pointSampler, pin.texcoord);
    
    // Load hardware depth [0 to 1]. Using int3(xy, mipLevel) avoids filtering artifacts
    float depthRaw = depthTexture.Load(int3(pin.position.xy, 0));

    // Linearization formula for standard perspective projections
    float linearDepth = (2.0f * nearZ) / (farZ + nearZ - depthRaw * (farZ - nearZ));
    float physicalDistance = linearDepth * farZ;

    // Apply linear blend equation clamped to [0.0, 1.0]
    float fogFactor = saturate((physicalDistance - fogStart) / (fogEnd - fogStart));
    float3 finalColor = lerp(originalColor.rgb, fogColor.rgb, fogFactor);

    return float4(finalColor, originalColor.a);
}