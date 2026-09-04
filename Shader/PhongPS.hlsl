#include "Phong.hlsli"

cbuffer CbMesh : register(b0)
{
    float4 materialColor;
    int alphaMode;
    float alphaCutoff;
    float2 meshPadding;
};

cbuffer CbObject : register(b2)
{
    float4 objectColor;
};

Texture2D DiffuseMap : register(t0);
Texture2D NormalMap : register(t1);
SamplerState LinearSampler : register(s0);

// Calculates Diffuse + Blinn-Phong Specular
float3 CalculatePhongLight(float3 L, float3 V, float3 N, float3 radiance, float3 albedo, float shininess)
{
    float NdotL = max(dot(N, L), 0.0f);
    float3 diffuse = albedo * radiance * NdotL;

    // Blinn-Phong Specular (Half-Vector)
    float3 H = normalize(V + L);
    float NdotH = max(dot(N, H), 0.0f);
    
    // Only apply specular if the surface is actually facing the light
    float specIntensity = (NdotL > 0.0f) ? pow(NdotH, shininess) : 0.0f;
    float3 specular = radiance * specIntensity;

    return diffuse + specular;
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

    // Linearize texture
    float3 albedo = pow(abs(color.rgb), 2.2f);
    
    // Normal Mapping
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
    float shininess = 128.0f; 

    float3 totalDirectLight = float3(0.0f, 0.0f, 0.0f);

    // Directional Light
    float3 dirL = normalize(-lightDirection.xyz);
    totalDirectLight += CalculatePhongLight(dirL, V, N, lightColor.rgb, albedo, shininess);

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
            totalDirectLight += CalculatePhongLight(L, V, N, radiance, albedo, shininess);
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
            totalDirectLight += CalculatePhongLight(L, V, N, radiance, albedo, shininess);
        }
    }

    // Ambient Hemisphere
    float factor = dot(N, float3(0.0f, 1.0f, 0.0f)) * 0.5f + 0.5f;
    float3 ambientLight = lerp(ambientGroundColor.rgb, ambientSkyColor.rgb, factor);

    float3 finalColor = totalDirectLight + (ambientLight * albedo);
    finalColor = pow(abs(finalColor), float3(1.0f / 2.2f, 1.0f / 2.2f, 1.0f / 2.2f));

    if (any(isnan(finalColor)) || any(isinf(finalColor)))
    {
        return float4(1.0f, 0.0f, 1.0f, 1.0f);
    }

    return float4(finalColor, color.a);
}