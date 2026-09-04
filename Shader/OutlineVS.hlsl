#include "Skinning.hlsli"
#include "Scene.hlsli"

cbuffer CbOutline : register(b0)
{
    float4 outlineColor;
    float outlineWidth;
    int alphaMode;
    float alphaCutoff;
    float padding;
};

struct VS_OUT
{
    float4 vertex : SV_POSITION;
    float2 texcoord : TEXCOORD;
};

VS_OUT main(
    float4 position : POSITION,
    float4 boneWeights : BONE_WEIGHTS,
    uint4 boneIndices : BONE_INDICES,
    float2 texcoord : TEXCOORD,
    float3 normal : NORMAL)
{
    VS_OUT vout = (VS_OUT) 0;
    vout.texcoord = texcoord;

    // Calculate World Position and Clip Space Vertex
    position = SkinningPosition(position, boneWeights, boneIndices);
    vout.vertex = mul(position, viewProjection);

    // Transform the World Normal into Clip Space
    float3 worldNormal = SkinningVector(normal, boneWeights, boneIndices);
    float4 clipNormal = mul(float4(worldNormal, 0.0f), viewProjection);
    
    // Normalize only the 2D screen-space components
    float2 offset = normalize(clipNormal.xy);

    // Aspect Ratio Correction (using dynamic engine resolution from CbScene)
    float screenWidth = packedParams.y;
    float screenHeight = packedParams.z;
    float aspect = screenWidth / screenHeight;
    offset.x /= aspect; // Squish X offset so lines aren't distorted on widescreen monitors

    // Define the maximum distance the constant-width rule applies
    float maxDepthScale = 15.0f;
    
    // Clamp the depth multiplier so the outline thins out at long ranges
    float depthMultiplier = min(vout.vertex.w, maxDepthScale);

    vout.vertex.xy += offset * outlineWidth * depthMultiplier;

    return vout;
}