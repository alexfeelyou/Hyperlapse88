cbuffer CbRadialBlur : register(b0)
{
    float2 b_center;
    float b_blurStrength;
    float padding;
};

struct VS_OUT
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

Texture2D sceneTexture : register(t0);
SamplerState linearSampler : register(s0);

static const int BLUR_SAMPLES = 10;

float4 main(VS_OUT pin) : SV_TARGET
{
    float2 uv = pin.texcoord;
    float2 coord = uv - b_center;
    float distSq = dot(coord, coord);
    float blurAmount = b_blurStrength * distSq * 4.0f;
    
    float4 accumColor = float4(0, 0, 0, 0);

    [unroll]
    for (int i = 0; i < BLUR_SAMPLES; i++)
    {
        float scale = 1.0f - blurAmount * (float(i) / float(BLUR_SAMPLES - 1));
        accumColor += sceneTexture.Sample(linearSampler, b_center + (uv - b_center) * scale);
    }
    
    return accumColor / float(BLUR_SAMPLES);
}