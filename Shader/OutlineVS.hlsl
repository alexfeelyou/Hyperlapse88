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
    float3 normal : NORMAL)
{
    VS_OUT vout = (VS_OUT) 0;
    vout.texcoord = texcoord;

    // Calculate World Position to determine exact camera distance
    float4 worldPos = SkinningPosition(position, boneWeights, boneIndices);
    float dist = length(cameraPosition.xyz - worldPos.xyz);
    
    // Calculate continuous pixel fade factor 
    // smoothstep returns a smooth 0->1 curve. Subtract from 1 so it shrinks as distance increases.
    float fadeFactor = 1.0f - smoothstep(outlineFadeStart, outlineFadeEnd, dist);
    
    // Transform to clip space
    vout.vertex = mul(worldPos, viewProjection);

    // Extrude along Clip Space Normal
    float3 worldNormal = SkinningVector(normal, boneWeights, boneIndices);
    float4 clipNormal = mul(float4(worldNormal, 0.0f), viewProjection);
    float2 offset = normalize(clipNormal.xy);

    // Aspect Ratio Fix
    float aspect = packedParams.y / packedParams.z;
    offset.x /= aspect;

    // Depth Lock
    // Clamp the hardware perspective cancellation so the model isn't swallowed far away
    float maxDepthScale = min(outlineFadeStart, 15.0f);
    float depthMultiplier = min(vout.vertex.w, maxDepthScale);

    vout.vertex.xy += offset * outlineWidth * depthMultiplier * fadeFactor;

    return vout;
}