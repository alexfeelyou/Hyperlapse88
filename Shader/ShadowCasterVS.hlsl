#include "Skinning.hlsli"

cbuffer CbShadow : register(b3)
{
    row_major float4x4 lightViewProjection;
};

float4 main(
    float4 position : POSITION,
    float4 boneWeights : BONE_WEIGHTS,
    uint4 boneIndices : BONE_INDICES,
    float4x4 instanceWorld : INSTANCE_TRANSFORM,
    float4x4 instancePrevWorld : INSTANCE_PREV_TRANSFORM,
    float4 instanceColor : INSTANCE_COLOR,
    uint4 instanceBoneOffsets : INSTANCE_BONE_OFFSETS) : SV_POSITION
{
    float4 localPos = SkinningPosition(position, boneWeights, boneIndices, instanceBoneOffsets.x);
    float4 worldPos = mul(localPos, instanceWorld);
    return mul(worldPos, lightViewProjection);
}