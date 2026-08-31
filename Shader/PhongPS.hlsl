#include "Phong.hlsli"

cbuffer CbMesh : register(b0)
{
    float4 materialColor;
};

cbuffer CbObject : register(b2)
{
    float4 objectColor;
};

Texture2D DiffuseMap : register(t0);
Texture2D NormalMap : register(t1);
SamplerState LinearSampler : register(s0);

float3 CalcLambert(float3 N, float3 L, float3 C, float3 K)
{
    float power = saturate(dot(N, -L));
    return C * power * K;
}
    
float3 CalcPhongSpecular(float3 N, float3 L, float3 E, float3 C, float3 K)
{
    float3 R = reflect(L, N);
    float power = max(dot(-E, R), 0);
    power = pow(power, 128);
    return C * power * K;
}

float3 CalcHemiSphereLight(float3 normal, float3 up, float3 sky_color, float3 ground_color, float4 hemisphere_weight)
{
    float factor = dot(normal, up) * 0.5f + 0.5f;
    return lerp(ground_color, sky_color, factor) * hemisphere_weight.x;
}

float4 main(VS_OUT pin) : SV_TARGET
{
    float4 color = DiffuseMap.Sample(LinearSampler, pin.texcoord) * materialColor * objectColor;

    // Normal
    float normalLenSq = dot(pin.normal, pin.normal);
    float3 N = (normalLenSq > 0.0001f) ? normalize(pin.normal) : float3(0.0f, 1.0f, 0.0f);

    // Uniform bracnch
    float tangentLenSq = dot(pin.tangent.xyz, pin.tangent.xyz);
    if (tangentLenSq > 0.0001f)
    {
        // Unconditionally sample the texture 
        float4 sampled = NormalMap.Sample(LinearSampler, pin.texcoord);
        
        float3 T = pin.tangent.xyz; // Raw tangent
        float3 orthoT = T - N * dot(N, T);
        float orthoLenSq = dot(orthoT, orthoT);

        // Branchless Fallback
        float3 safeT = (orthoLenSq > 0.0001f) ? normalize(orthoT) : normalize(T);
        float3 B = normalize(cross(N, safeT) * pin.tangent.w);
        
        float3 localNormal = sampled.xyz * 2.0f - 1.0f;
        float3 newNormal = localNormal.x * safeT + localNormal.y * B + localNormal.z * N;
        
        // Conditionally apply the normal map without a pipeline-stalling 'if' statement
        N = (orthoLenSq > 0.0001f) ? normalize(newNormal) : N;
    }

    // Standard Lighting Math
    float3 L = normalize(lightDirection.xyz);
    float3 LC = lightColor.rgb;

    float3 directDiffuse = CalcLambert(N, L, LC, 1.0f);

    float3 upDir = float3(0.0f, 1.0f, 0.0f);
    float3 skyColor = float3(0.6f, 0.6f, 0.65f);
    float3 groundColor = float3(0.2f, 0.2f, 0.2f);
    float ambient = 0.5f;
    
    float3 ambientLight = CalcHemiSphereLight(N, upDir, skyColor, groundColor, ambient);
    float3 finalLight = directDiffuse + ambientLight;

    color.rgb *= finalLight;

    // Final Error Trap
    if (any(isnan(color)) || any(isinf(color)))
    {
        return float4(1.0f, 0.0f, 1.0f, 1.0f);
    }
    
    return color;
}