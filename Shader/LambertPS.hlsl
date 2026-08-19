#include "Lambert.hlsli"

// Buffer Material (Slot 0) - Bawaan Model
cbuffer CbMesh : register(b0)
{
    float4 materialColor;
};

// === TAMBAHAN BARU ===
// Buffer Object (Slot 2) - Warna Merah/Biru dari Kode Boss.cpp
cbuffer CbObject : register(b2)
{
    float4 objectColor; // Warna strobing
};
// =====================

Texture2D DiffuseMap : register(t0);
SamplerState LinearSampler : register(s0);

float4 main(VS_OUT pin) : SV_TARGET
{
    // Ambil warna tekstur dasar
    float4 color = DiffuseMap.Sample(LinearSampler, pin.texcoord);
    
    // Kalikan dengan warna material asli
    color *= materialColor;

    // === TAMBAHAN BARU ===
    // Kalikan dengan warna object (efek strobing)
    color *= objectColor;
    // =====================

    float3 N = normalize(pin.normal);
    float3 L = normalize(-lightDirection.xyz);
    float power = max(0, dot(L, N));

    power = power * 0.7 + 0.3f;

    color.rgb *= lightColor.rgb * power;

    return color;
}