cbuffer CbOutline : register(b0)
{
    float4 outlineColor;
    float outlineWidth;
    int alphaMode;
    float alphaCutoff;
    float padding;
};

Texture2D DiffuseMap : register(t0);
SamplerState LinearSampler : register(s0);

struct VS_OUT
{
    float4 vertex : SV_POSITION;
    float2 texcoord : TEXCOORD;
};

float4 main(VS_OUT pin) : SV_TARGET
{
    // Discard outline pixels if they fall on a masked/transparent part of the base texture (e.g. hair cards)
    if (alphaMode == 1)
    {
        float4 texColor = DiffuseMap.Sample(LinearSampler, pin.texcoord);
        clip(texColor.a - alphaCutoff);
    }
    else if (alphaMode == 2)
    {
        float4 texColor = DiffuseMap.Sample(LinearSampler, pin.texcoord);
        clip(texColor.a - 0.01f);
    }

    // Output pure unlit flat color
    return outlineColor;
}