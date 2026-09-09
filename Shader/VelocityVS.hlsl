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
    uint4 boneIndices : BONE_INDICES)
{
    VSOutput output;

    const float4 currentWorldPos = SkinningPosition(position, boneWeights, boneIndices);
    const float4 previousWorldPos = SkinningPositionPrevious(position, boneWeights, boneIndices);

    output.currentClipPos = mul(currentWorldPos, currentViewProjection);
    output.previousClipPos = mul(previousWorldPos, previousViewProjection);
    output.position = output.currentClipPos;

    return output;
}