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

    // 1. Linearize the combined Albedo (Texture + UI Colors) to fix washed-out grayness
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
        emissive *= pow(abs(emTex), 2.2f); // Linearize emissive texture
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

    // -----------------------------------------------------------------
    // Direct Light
    // -----------------------------------------------------------------
    float3 L = normalize(lightDirection.xyz);
    float3 H = normalize(V + L);
    
    // 2. THE FIX: Multiply LDR light intensity by PI. 
    // This perfectly cancels out the mathematical `albedo / PI` energy loss below,
    // matching the exact brightness scale of your Phong shader.
    float3 radiance = lightColor.rgb * PI;

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
    float3 directLighting = (kD * albedo.rgb / PI + specular) * radiance * NdotL;

    // -----------------------------------------------------------------
    // Ambient / Environment
    // -----------------------------------------------------------------
    // 3. THE FIX: Restored ambient multipliers to 0.5 to exactly match Phong
    float3 skyColor = float3(0.6f, 0.6f, 0.65f) * 0.5f;
    float3 groundColor = float3(0.2f, 0.2f, 0.2f) * 0.5f;
    
    float factor = dot(N, float3(0.0f, 1.0f, 0.0f)) * 0.5f + 0.5f;
    float3 irradiance = lerp(groundColor, skyColor, factor);
    float3 diffuseAmbient = irradiance * albedo.rgb;
    
    float3 F_ambient = FresnelSchlickRoughness(max(dot(N, V), 0.0f), F0, roughness);
    float3 kS_ambient = F_ambient;
    float3 kD_ambient = 1.0f - kS_ambient;
    kD_ambient *= 1.0f - metallic;
    
    float3 reflectionDir = reflect(-V, N);
    float reflFactor = dot(reflectionDir, float3(0.0f, 1.0f, 0.0f)) * 0.5f + 0.5f;
    float3 prefilteredColor = lerp(groundColor, skyColor, reflFactor);
    
    float2 envBRDF = float2(1.0f, 1.0f);
    float3 specularAmbient = prefilteredColor * (F_ambient * envBRDF.x + envBRDF.y);
    
    float3 ambient = (kD_ambient * diffuseAmbient + specularAmbient) * ao;

    // -----------------------------------------------------------------
    // Final Compositing & Gamma
    // -----------------------------------------------------------------
    float3 finalColor = ambient + directLighting + emissive;

    // 4. Gamma Correction (Transforms Linear math back to monitor sRGB color space)
    finalColor = pow(abs(finalColor), float3(1.0f / 2.2f, 1.0f / 2.2f, 1.0f / 2.2f));

    return float4(finalColor, albedo.a);
}