#include "Skinning.hlsli"
#include "Pbr.hlsli"

VS_OUT main(
    float4 position : POSITION,
    float4 boneWeights : BONE_WEIGHTS,
    uint4 boneIndices : BONE_INDICES,
    float2 texcoord : TEXCOORD,
    float4 tangent : TANGENT,
    float3 normal : NORMAL,
    float4x4 instanceWorld : INSTANCE_TRANSFORM,
    float4x4 instancePrevWorld : INSTANCE_PREV_TRANSFORM,
    float4 instanceColor : INSTANCE_COLOR)
{
    VS_OUT vout = (VS_OUT) 0;

    float4 localPos = SkinningPosition(position, boneWeights, boneIndices);
    float4 worldPos = mul(localPos, instanceWorld);
    
    vout.vertex = mul(worldPos, viewProjection);
    vout.texcoord = texcoord;
    
    float3 localNormal = SkinningVector(normal, boneWeights, boneIndices);
    vout.normal = normalize(mul(localNormal, (float3x3) instanceWorld));
    
    float3 localTangent = SkinningVector(tangent.xyz, boneWeights, boneIndices);
    vout.tangent.xyz = normalize(mul(localTangent, (float3x3) instanceWorld));
    vout.tangent.w = tangent.w;
    
    vout.position = worldPos.xyz;
    vout.color = instanceColor;
    
    return vout;
}