struct BoneMatrix
{
    row_major float4x4 transform;
};

// Unified Bone Palette StructuredBuffer for Multi-Character Skinned Instancing
StructuredBuffer<BoneMatrix> g_BonePalette : register(t14);

float4 SkinningPosition(float4 position, float4 boneWeights, uint4 boneIndices, uint boneOffset)
{
    float4 p = float4(0, 0, 0, 0);
    [unroll]
    for (int i = 0; i < 4; i++)
    {
        p += (boneWeights[i] * mul(position, g_BonePalette[boneOffset + boneIndices[i]].transform));
    }
    return p;
}

float3 SkinningVector(float3 vec, float4 boneWeights, uint4 boneIndices, uint boneOffset)
{
    float3 v = float3(0, 0, 0);
    [unroll]
    for (int i = 0; i < 4; i++)
    {
        v += boneWeights[i] * mul(float4(vec, 0), g_BonePalette[boneOffset + boneIndices[i]].transform).xyz;
    }
    return v;
}

float4 SkinningPositionPrevious(float4 position, float4 boneWeights, uint4 boneIndices, uint prevBoneOffset)
{
    float4 p = float4(0, 0, 0, 0);
    [unroll]
    for (int i = 0; i < 4; i++)
    {
        p += (boneWeights[i] * mul(position, g_BonePalette[prevBoneOffset + boneIndices[i]].transform));
    }
    return p;
}