#include "Pbr.hlsli"

static const float PI = 3.14159265359f;

cbuffer CbMesh : register(b0)
{
    float4 materialColor;
    float3 emissiveColorBase;
    float matRoughness;
    float matMetallic;
    float occlusionStrength;
    int alphaMode;
    float alphaCutoff;
    int textureFlags;
    float3 meshPadding;
};

cbuffer CbObject : register(b2)
{
    float4 objectColor;
};

Texture2D DiffuseMap : register(t0);
Texture2D NormalMap : register(t1);
Texture2D MetalRoughMap : register(t2);
Texture2D EmissiveMap : register(t3);
Texture2D AOMap : register(t4);

SamplerState LinearSampler : register(s0);

// BRDF Math 
float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0f);
    float NdotH2 = NdotH * NdotH;
    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0f) + 1.0f);
    denom = PI * denom * denom;
    return nom / max(denom, 0.0000001f);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0f);
    float k = (r * r) / 8.0f;
    float nom = NdotV;
    float denom = NdotV * (1.0f - k) + k;
    return nom / denom;
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0f);
    float NdotL = max(dot(N, L), 0.0f);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0f - F0) * pow(clamp(1.0f - cosTheta, 0.0f, 1.0f), 5.0f);
}

float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    return F0 + (max(float3(1.0f - roughness, 1.0f - roughness, 1.0f - roughness), F0) - F0) * pow(clamp(1.0f - cosTheta, 0.0f, 1.0f), 5.0f);
}

// Reusable Direct Lighting Processor
float3 CalculateDirectLight(float3 L, float3 V, float3 N, float3 radiance, float3 albedo, float roughness, float metallic, float3 F0)
{
    float3 H = normalize(V + L);
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0f), F0);
        
    float3 numerator = NDF * G * F;
    float denominator = 4.0f * max(dot(N, V), 0.0f) * max(dot(N, L), 0.0f) + 0.0001f;
    float3 specular = numerator / denominator;
        
    float3 kS = F;
    float3 kD = float3(1.0f, 1.0f, 1.0f) - kS;
    kD *= 1.0f - metallic;

    float NdotL = max(dot(N, L), 0.0f);
    return (kD * albedo / PI + specular) * radiance * NdotL;
}

float4 main(VS_OUT pin) : SV_TARGET
{
    float4 albedoTex = DiffuseMap.Sample(LinearSampler, pin.texcoord);
    float4 albedo = albedoTex * materialColor * objectColor;

    if (alphaMode == 1)
    {
        clip(albedo.a - alphaCutoff);
    }
    else if (alphaMode == 2)
    {
        clip(albedo.a - 0.01f);
    }

    albedo.rgb = pow(abs(albedo.rgb), 2.2f);

    float metallic = matMetallic;
    float roughness = matRoughness;
    if (textureFlags & (1 << 0))
    {
        float4 mrSample = MetalRoughMap.Sample(LinearSampler, pin.texcoord);
        roughness = mrSample.g * matRoughness;
        metallic = mrSample.b * matMetallic;
    }
    
    roughness = clamp(roughness, 0.04f, 1.0f);

    float3 emissive = emissiveColorBase;
    if (textureFlags & (1 << 1))
    {
        float3 emTex = EmissiveMap.Sample(LinearSampler, pin.texcoord).rgb;
        emissive *= pow(abs(emTex), 2.2f);
    }

    float ao = 1.0f;
    if (textureFlags & (1 << 2))
    {
        ao = lerp(1.0f, AOMap.Sample(LinearSampler, pin.texcoord).r, occlusionStrength);
    }

    float3 N = normalize(pin.normal);
    float tangentLenSq = dot(pin.tangent.xyz, pin.tangent.xyz);
    if (tangentLenSq > 0.0001f)
    {
        float3 T = normalize(pin.tangent.xyz - N * dot(N, pin.tangent.xyz));
        float3 B = normalize(cross(N, T) * pin.tangent.w);
        float3x3 TBN = float3x3(T, B, N);
        
        float3 localNormal = NormalMap.Sample(LinearSampler, pin.texcoord).xyz * 2.0f - 1.0f;
        N = normalize(mul(localNormal, TBN));
    }

    float3 V = normalize(cameraPosition.xyz - pin.position);
    float3 F0 = float3(0.04f, 0.04f, 0.04f);
    F0 = lerp(F0, albedo.rgb, metallic);

    float3 totalDirectLighting = float3(0.0f, 0.0f, 0.0f);

    // Directional Light
    float3 dirL = normalize(lightDirection.xyz);
    float3 dirRadiance = lightColor.rgb * PI;
    totalDirectLighting += CalculateDirectLight(dirL, V, N, dirRadiance, albedo.rgb, roughness, metallic, F0);

    // Point Lights
    for (int i = 0; i < lightCounts.x; ++i)
    {
        float3 L = pointLights[i].positionAndRange.xyz - pin.position;
        float distance = length(L);
        float range = pointLights[i].positionAndRange.w;
        
        if (distance < range)
        {
            L /= distance;
            // Epic Games inverse-square falloff
            float attenuation = saturate(1.0f - pow(distance / range, 4.0f));
            attenuation = (attenuation * attenuation) / (distance * distance + 1.0f);
            
            float3 radiance = pointLights[i].colorAndIntensity.xyz * pointLights[i].colorAndIntensity.w * attenuation * PI;
            totalDirectLighting += CalculateDirectLight(L, V, N, radiance, albedo.rgb, roughness, metallic, F0);
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
            
            // Smooth inner/outer cone interpolation
            float spotAttenuation = smoothstep(cosHalfAngle, cosHalfAngle + 0.1f, currentCosAngle);
            
            float3 radiance = spotLights[j].colorAndIntensity.xyz * spotLights[j].colorAndIntensity.w * attenuation * spotAttenuation * PI;
            totalDirectLighting += CalculateDirectLight(L, V, N, radiance, albedo.rgb, roughness, metallic, F0);
        }
    }

    // Ambient Fallback
    float factor = dot(N, float3(0.0f, 1.0f, 0.0f)) * 0.5f + 0.5f;
    float3 irradiance = lerp(ambientGroundColor.rgb, ambientSkyColor.rgb, factor);
    float3 diffuseAmbient = irradiance * albedo.rgb;
    
    float3 F_ambient = FresnelSchlickRoughness(max(dot(N, V), 0.0f), F0, roughness);
    float3 kS_ambient = F_ambient;
    float3 kD_ambient = 1.0f - kS_ambient;
    kD_ambient *= 1.0f - metallic;
    
    float3 reflectionDir = reflect(-V, N);
    float reflFactor = dot(reflectionDir, float3(0.0f, 1.0f, 0.0f)) * 0.5f + 0.5f;
    float3 prefilteredColor = lerp(ambientGroundColor.rgb, ambientSkyColor.rgb, reflFactor);
    
    float2 envBRDF = float2(1.0f, 1.0f);
    float3 specularAmbient = prefilteredColor * (F_ambient * envBRDF.x + envBRDF.y);
    
    float3 ambient = (kD_ambient * diffuseAmbient + specularAmbient) * ao;

    float3 finalColor = ambient + totalDirectLighting + emissive;
    finalColor = pow(abs(finalColor), float3(1.0f / 2.2f, 1.0f / 2.2f, 1.0f / 2.2f));

    return float4(finalColor, albedo.a);
}