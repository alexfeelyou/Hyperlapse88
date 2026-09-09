struct PSInput
{
    float4 position : SV_POSITION;
    float4 currentClipPos : TEXCOORD0;
    float4 previousClipPos : TEXCOORD1;
};

float2 main(PSInput input) : SV_TARGET
{
    // Perspective divide into NDC, then convert to UV-space motion (D3D's Y is flipped vs NDC)
    const float2 currentNDC = input.currentClipPos.xy / input.currentClipPos.w;
    const float2 previousNDC = input.previousClipPos.xy / input.previousClipPos.w;

    return (currentNDC - previousNDC) * float2(0.5f, -0.5f);
}