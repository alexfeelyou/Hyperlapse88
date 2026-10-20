#include "Skinning.hlsli"
#include "Toon.hlsli"

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

    float4 localPos = SkinningPosition(position, boneWeights, boneIndices, instanceBoneOffsets.x);
    float4 worldPos = mul(localPos, instanceWorld);
    
    vout.vertex = mul(worldPos, viewProjection);
    vout.texcoord = texcoord;
    
    float3 localNormal = SkinningVector(normal, boneWeights, boneIndices, instanceBoneOffsets.x);
    vout.normal = normalize(mul(localNormal, (float3x3) instanceWorld));
    
    vout.position = worldPos.xyz;
    vout.color = instanceColor;

    float3 instanceScale = float3(
        length(instanceWorld[0].xyz),
        length(instanceWorld[1].xyz),
        length(instanceWorld[2].xyz)
    );
    vout.boxPos = localPos.xyz * instanceScale;
    vout.boxNormal = localNormal;

    return vout;
}