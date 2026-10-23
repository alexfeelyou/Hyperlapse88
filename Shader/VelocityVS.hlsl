#include "Skinning.hlsli"

cbuffer CbVelocity : register(b8)
{
    row_major float4x4 currentViewProjection;
    row_major float4x4 previousViewProjection;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float4 currentClipPos : TEXCOORD0;
    float4 previousClipPos : TEXCOORD1;
};

VSOutput main(
    float4 position : POSITION,
    float4 boneWeights : BONE_WEIGHTS,
    uint4 boneIndices : BONE_INDICES,
    float4x4 instanceWorld : INSTANCE_TRANSFORM,
    float4x4 instancePrevWorld : INSTANCE_PREV_TRANSFORM,
    float4 instanceColor : INSTANCE_COLOR,
    uint4 instanceBoneOffsets : INSTANCE_BONE_OFFSETS)
{
    VSOutput output;

    float4 localPos = SkinningPosition(position, boneWeights, boneIndices, instanceBoneOffsets.x);
    float4 prevLocalPos = SkinningPositionPrevious(position, boneWeights, boneIndices, instanceBoneOffsets.y);
    
    float4 currentWorldPos = mul(localPos, instanceWorld);
    float4 previousWorldPos = mul(prevLocalPos, instancePrevWorld);

    output.currentClipPos = mul(currentWorldPos, currentViewProjection);
    output.previousClipPos = mul(previousWorldPos, previousViewProjection);
    output.position = output.currentClipPos;

    return output;
}