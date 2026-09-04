#include "Toon.hlsli"

cbuffer CbMesh : register(b0)
{
    float4 materialColor;
    int alphaMode;
    float alphaCutoff;
    float roughness;
    float padding;
};

cbuffer CbObject : register(b2)
{
    float4 objectColor;
};

Texture2D DiffuseMap : register(t0);
SamplerState LinearSampler : register(s0);

// Calculates pure Toon light scattering using quantized Half-Lambert and stepped Specular
float3 CalculateToonLight(float3 L, float3 N, float3 V, float3 radiance, float3 albedo)
{
    // Half-Lambert Remapping: Prevents pitch-black shadows on the unlit side of the mesh
    float NdotL = dot(N, L);
    float halfLambert = NdotL * 0.5f + 0.5f;

    // Quantized Cel-Banding: Creates a 3-tone shaded look with slight anti-aliasing on the edges
    float celDiffuse = smoothstep(0.48f, 0.52f, halfLambert) * 0.6f +
                       smoothstep(0.78f, 0.82f, halfLambert) * 0.4f;
    
    float3 diffuseColor = albedo * radiance * celDiffuse;

    // Hard-Stepped Blinn-Phong Specular
    float3 H = normalize(L + V);
    float NdotH = max(dot(N, H), 0.0f);
    
    // Map intuitive 0-1 roughness to a traditional glossiness exponent
    float glossiness = exp2(10.0f * (1.0f - roughness));
    float specRaw = pow(NdotH, glossiness);
    
    // A hard step creates a solid, sharp geometric glint instead of a soft realistic glare
    float celSpec = step(0.5f, specRaw);
    float3 specularColor = radiance * celSpec * 0.5f; 

    return diffuseColor + specularColor;
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

    float3 albedo = pow(abs(color.rgb), 2.2f);
    float3 N = normalize(pin.normal);
    float3 V = normalize(cameraPosition.xyz - pin.position); // View vector for specular

    float3 totalDirectLight = float3(0.0f, 0.0f, 0.0f);

    // Directional Light
    float3 dirL = normalize(-lightDirection.xyz);
    totalDirectLight += CalculateToonLight(dirL, N, V, lightColor.rgb, albedo);

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
            
            totalDirectLight += CalculateToonLight(L, N, V, radiance, albedo);
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
            
            totalDirectLight += CalculateToonLight(L, N, V, radiance, albedo);
        }
    }

    // Ambient Hemisphere Blending (Hard-stepped for Toon Style)
    float factor = dot(N, float3(0.0f, 1.0f, 0.0f));
    float celFactor = step(0.0f, factor); // Hard step between sky and ground color
    float3 ambientLight = lerp(ambientGroundColor.rgb, ambientSkyColor.rgb, celFactor);

    float3 finalColor = totalDirectLight + (ambientLight * albedo);
    finalColor = pow(abs(finalColor), float3(1.0f / 2.2f, 1.0f / 2.2f, 1.0f / 2.2f));

    return float4(finalColor, color.a);
}