#include "Scene.hlsli"
#include "Skinning.hlsli"

cbuffer CbOutline : register(b0)
{
    float4 outlineColor;
    float outlineWidth;
    float outlineFadeStart;
    float outlineFadeEnd;
    int alphaMode;
    float alphaCutoff;
    float3 padding;
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
    float3 normal : NORMAL,
    float4x4 instanceWorld : INSTANCE_TRANSFORM,
    float4x4 instancePrevWorld : INSTANCE_PREV_TRANSFORM,
    float4 instanceColor : INSTANCE_COLOR,
    uint4 instanceBoneOffsets : INSTANCE_BONE_OFFSETS)
{
    VS_OUT vout = (VS_OUT) 0;
    vout.texcoord = texcoord;

    // Evaluate hardware instancing world space
    float4 localPos = SkinningPosition(position, boneWeights, boneIndices, instanceBoneOffsets.x);
    float4 worldPos = mul(localPos, instanceWorld);
    float dist = length(cameraPosition.xyz - worldPos.xyz);
    
    float fadeFactor = 1.0f - smoothstep(outlineFadeStart, outlineFadeEnd, dist);
    
    vout.vertex = mul(worldPos, viewProjection);

    // Evaluate instance-aligned normal
    float3 localNormal = SkinningVector(normal, boneWeights, boneIndices, instanceBoneOffsets.x);
    float3 worldNormal = normalize(mul(localNormal, (float3x3) instanceWorld));
    float4 clipNormal = mul(float4(worldNormal, 0.0f), viewProjection);
    float2 offset = normalize(clipNormal.xy);

    float aspect = packedParams.y / packedParams.z;
    offset.x /= aspect;

    float maxDepthScale = min(outlineFadeStart, 15.0f);
    float depthMultiplier = min(vout.vertex.w, maxDepthScale);

    vout.vertex.xy += offset * outlineWidth * depthMultiplier * fadeFactor;

    return vout;
}