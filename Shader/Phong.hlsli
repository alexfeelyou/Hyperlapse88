#include "Scene.hlsli"

struct VS_OUT
{
    float4 vertex : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL;
    float3 position : POSITION;
    float4 tangent : TANGENT;
    float4 color : COLOR;
    float3 boxPos : TEXCOORD1;
    float3 boxNormal : NORMAL1;
};
