#include "Lambert.hlsli"

cbuffer CbMesh : register(b0)
{
    float4 materialColor;
    float3 emissiveColor;
    int alphaMode;
    float alphaCutoff;
    float3 meshPadding;
};

cbuffer CbObject : register(b2)
{
    float4 objectColor;
};

Texture2D DiffuseMap : register(t0);
SamplerState LinearSampler : register(s0);

// Calculates pure diffuse light scattering
float3 CalculateLambertLight(float3 L, float3 N, float3 radiance, float3 albedo)
{
    float NdotL = max(dot(N, L), 0.0f);
    return albedo * radiance * NdotL;
}

float4 main(VS_OUT pin) : SV_TARGET
{
    float4 texColor = DiffuseMap.Sample(LinearSampler, pin.texcoord);
    float4 color = texColor * materialColor * objectColor;

    if (alphaMode == 1)
    {
        clip(color.a - alphaCutoff);
    }
    else if (alphaMode == 2)
    {
        clip(color.a - 0.01f);
    }

    // Linearize the texture to match the engine's HDR/Gamma pipeline 
    float3 albedo = pow(abs(color.rgb), 2.2f);
    float3 N = normalize(pin.normal);

    float3 totalDirectLight = float3(0.0f, 0.0f, 0.0f);

    // Directional Light (Invert direction to point to the light source)
    float3 dirL = normalize(-lightDirection.xyz);
    totalDirectLight += CalculateLambertLight(dirL, N, lightColor.rgb, albedo);

    // Point Lights
    for (int i = 0; i < lightCounts.x; ++i)
    {
        float3 L = pointLights[i].positionAndRange.xyz - pin.position;
        float distance = length(L);
        float range = pointLights[i].positionAndRange.w;
        
        if (distance < range)
        {
            L /= distance;
            float attenuation = saturate(1.0f - pow(distance / range, 4.0f));
            attenuation = (attenuation * attenuation) / (distance * distance + 1.0f);
            
            float3 radiance = pointLights[i].colorAndIntensity.xyz * pointLights[i].colorAndIntensity.w * attenuation;
            totalDirectLight += CalculateLambertLight(L, N, radiance, albedo);
        }
    }

    // Spot Lights
    for (int j = 0; j < lightCounts.y; ++j)
    {
        float3 L = spotLights[j].positionAndRange.xyz - pin.position;
        float distance = length(L);
        float range = spotLights[j].positionAndRange.w;
        
        if (distance < range)
        {
            L /= distance;
            float attenuation = saturate(1.0f - pow(distance / range, 4.0f));
            attenuation = (attenuation * attenuation) / (distance * distance + 1.0f);
            
            float cosHalfAngle = spotLights[j].directionAndAngle.w;
            float currentCosAngle = dot(L, -spotLights[j].directionAndAngle.xyz);
            float spotAttenuation = smoothstep(cosHalfAngle, cosHalfAngle + 0.1f, currentCosAngle);
            
            float3 radiance = spotLights[j].colorAndIntensity.xyz * spotLights[j].colorAndIntensity.w * attenuation * spotAttenuation;
            totalDirectLight += CalculateLambertLight(L, N, radiance, albedo);
        }
    }

    // Ambient Hemisphere (Sky / Ground dynamic blending)
    float factor = dot(N, float3(0.0f, 1.0f, 0.0f)) * 0.5f + 0.5f;
    float3 ambientLight = lerp(ambientGroundColor.rgb, ambientSkyColor.rgb, factor);

    float3 finalColor = totalDirectLight + (ambientLight * albedo) + emissiveColor;

    // Apply gamma correction to output properly to the monitor
    finalColor = pow(abs(finalColor), float3(1.0f / 2.2f, 1.0f / 2.2f, 1.0f / 2.2f));

    return float4(finalColor, color.a);
}