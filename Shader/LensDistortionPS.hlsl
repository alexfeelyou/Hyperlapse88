cbuffer CbDistortion : register(b0)
{
    float2 l_center;
    float l_distortion;
    float l_chromaticAberration;
    float l_glitchStrength;
    float l_time;
    float2 padding;
};

struct VS_OUT
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

Texture2D sceneTexture : register(t0);
SamplerState linearSampler : register(s0);

float rand(float2 n)
{
    return frac(sin(dot(n, float2(12.9898, 4.1414))) * 43758.5453);
}

float2 LensDistort(float2 uv, float k)
{
    float2 t = uv - l_center;
    float r2 = t.x * t.x + t.y * t.y;
    float f = 1.0 + r2 * (k * 10.0f);
    return l_center + t * f;
}

float4 main(VS_OUT pin) : SV_TARGET
{
    float2 uv = pin.texcoord;

    if (l_glitchStrength > 0.0f)
    {
        float shake = (rand(float2(0, uv.y + l_time)) - 0.5f) * l_glitchStrength * 0.1f;
        uv.x += shake;
    }

    float2 distortedUV = LensDistort(uv, l_distortion);
    float shiftAmount = l_chromaticAberration * 0.5f;

    float2 uvR = distortedUV + float2(shiftAmount, 0.0f);
    float2 uvG = distortedUV;
    float2 uvB = distortedUV - float2(shiftAmount, 0.0f);

    if (any(uvR < 0.0) || any(uvR > 1.0) || any(uvB < 0.0) || any(uvB > 1.0))
    {
        return float4(0, 0, 0, 1);
    }

    float4 color;
    color.r = sceneTexture.Sample(linearSampler, uvR).r;
    color.g = sceneTexture.Sample(linearSampler, uvG).g;
    color.b = sceneTexture.Sample(linearSampler, uvB).b;
    color.a = 1.0f;
    
    return color;
}