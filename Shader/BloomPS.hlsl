// FILE: Shader/BloomPS.hlsl
struct VS_OUT
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

Texture2D<float4> sourceTexture : register(t0);
Texture2D<float4> bloomTexture : register(t3);

SamplerState linearSampler : register(s0);
SamplerState pointSampler : register(s1);

cbuffer CbBloom : register(b0)
{
    float2 texelSize;
    float threshold;
    float softKnee;
    float4 bloomTintAndIntensity;
    float scatter;
    int passMode; // 0: Karis Prefilter + 13-Tap, 1: 13-Tap Downsample, 2: 9-Tap Tent Upsample, 3: Final Composite
    float2 padding;
};

// Quadratic soft-knee highlight extraction curve
float3 Prefilter(float3 color)
{
    float brightness = max(color.r, max(color.g, color.b));
    float knee = threshold * softKnee + 0.0001f;
    float3 curve = float3(threshold - knee, knee * 2.0f, 0.25f / knee);
    float rq = clamp(brightness - curve.x, 0.0f, curve.y);
    rq = curve.z * rq * rq;
    float weight = max(rq, brightness - threshold) * rcp(max(brightness, 0.0001f));
    return max(0.0f, color * weight);
}

// Perceptual Karis weight to suppress sub-pixel HDR specular fireflies on the first downsample
float KarisWeight(float3 c)
{
    float luma = dot(c, float3(0.2126f, 0.7152f, 0.0722f));
    return rcp(1.0f + luma);
}

// Jimenez 13-Tap Bilinear Downsample Filter
float3 Downsample13Tap(float2 uv, bool useKarisPrefilter)
{
    float3 a = sourceTexture.SampleLevel(linearSampler, uv + float2(-2.0f, -2.0f) * texelSize, 0.0f).rgb;
    float3 b = sourceTexture.SampleLevel(linearSampler, uv + float2(0.0f, -2.0f) * texelSize, 0.0f).rgb;
    float3 c = sourceTexture.SampleLevel(linearSampler, uv + float2(2.0f, -2.0f) * texelSize, 0.0f).rgb;

    float3 d = sourceTexture.SampleLevel(linearSampler, uv + float2(-1.0f, -1.0f) * texelSize, 0.0f).rgb;
    float3 e = sourceTexture.SampleLevel(linearSampler, uv + float2(1.0f, -1.0f) * texelSize, 0.0f).rgb;

    float3 f = sourceTexture.SampleLevel(linearSampler, uv + float2(-2.0f, 0.0f) * texelSize, 0.0f).rgb;
    float3 g = sourceTexture.SampleLevel(linearSampler, uv, 0.0f).rgb;
    float3 h = sourceTexture.SampleLevel(linearSampler, uv + float2(2.0f, 0.0f) * texelSize, 0.0f).rgb;

    float3 i = sourceTexture.SampleLevel(linearSampler, uv + float2(-1.0f, 1.0f) * texelSize, 0.0f).rgb;
    float3 j = sourceTexture.SampleLevel(linearSampler, uv + float2(1.0f, 1.0f) * texelSize, 0.0f).rgb;

    float3 k = sourceTexture.SampleLevel(linearSampler, uv + float2(-2.0f, 2.0f) * texelSize, 0.0f).rgb;
    float3 l = sourceTexture.SampleLevel(linearSampler, uv + float2(0.0f, 2.0f) * texelSize, 0.0f).rgb;
    float3 m = sourceTexture.SampleLevel(linearSampler, uv + float2(2.0f, 2.0f) * texelSize, 0.0f).rgb;

    if (useKarisPrefilter)
    {
        float3 group0 = Prefilter((d + e + i + j) * 0.25f);
        float3 group1 = Prefilter((a + b + f + g) * 0.25f);
        float3 group2 = Prefilter((b + c + g + h) * 0.25f);
        float3 group3 = Prefilter((f + g + k + l) * 0.25f);
        float3 group4 = Prefilter((g + h + l + m) * 0.25f);

        float w0 = KarisWeight(group0) * 0.5f;
        float w1 = KarisWeight(group1) * 0.125f;
        float w2 = KarisWeight(group2) * 0.125f;
        float w3 = KarisWeight(group3) * 0.125f;
        float w4 = KarisWeight(group4) * 0.125f;

        float weightSum = max(w0 + w1 + w2 + w3 + w4, 0.0001f);
        return (group0 * w0 + group1 * w1 + group2 * w2 + group3 * w3 + group4 * w4) * rcp(weightSum);
    }

    float3 result = (d + e + i + j) * (0.25f * 0.5f);
    result += (a + b + f + g) * (0.25f * 0.125f);
    result += (b + c + g + h) * (0.25f * 0.125f);
    result += (f + g + k + l) * (0.25f * 0.125f);
    result += (g + h + l + m) * (0.25f * 0.125f);
    return result;
}

// 9-Tap 3x3 Tent Upsample Filter ([1, 2, 1; 2, 4, 2; 1, 2, 1] / 16)
float3 UpsampleTent9Tap(float2 uv)
{
    float2 d = texelSize;

    float3 s0 = sourceTexture.SampleLevel(linearSampler, uv + float2(-d.x, -d.y), 0.0f).rgb;
    float3 s1 = sourceTexture.SampleLevel(linearSampler, uv + float2(0.0f, -d.y), 0.0f).rgb * 2.0f;
    float3 s2 = sourceTexture.SampleLevel(linearSampler, uv + float2(d.x, -d.y), 0.0f).rgb;

    float3 s3 = sourceTexture.SampleLevel(linearSampler, uv + float2(-d.x, 0.0f), 0.0f).rgb * 2.0f;
    float3 s4 = sourceTexture.SampleLevel(linearSampler, uv, 0.0f).rgb * 4.0f;
    float3 s5 = sourceTexture.SampleLevel(linearSampler, uv + float2(d.x, 0.0f), 0.0f).rgb * 2.0f;

    float3 s6 = sourceTexture.SampleLevel(linearSampler, uv + float2(-d.x, d.y), 0.0f).rgb;
    float3 s7 = sourceTexture.SampleLevel(linearSampler, uv + float2(0.0f, d.y), 0.0f).rgb * 2.0f;
    float3 s8 = sourceTexture.SampleLevel(linearSampler, uv + float2(d.x, d.y), 0.0f).rgb;

    return (s0 + s1 + s2 + s3 + s4 + s5 + s6 + s7 + s8) * (0.0625f * scatter);
}

float4 main(VS_OUT pin) : SV_TARGET
{
    if (passMode == 0)
    {
        return float4(Downsample13Tap(pin.texcoord, true), 1.0f);
    }
    else if (passMode == 1)
    {
        return float4(Downsample13Tap(pin.texcoord, false), 1.0f);
    }
    else if (passMode == 2)
    {
        return float4(UpsampleTent9Tap(pin.texcoord), 1.0f);
    }

    // Pass Mode 3: Final Composite (Full-Res Linear HDR Scene + Upsampled Mip 0 Bloom)
    float3 sceneColor = sourceTexture.SampleLevel(pointSampler, pin.texcoord, 0.0f).rgb;
    float3 bloomColor = bloomTexture.SampleLevel(linearSampler, pin.texcoord, 0.0f).rgb;

    float3 finalColor = sceneColor + bloomColor * bloomTintAndIntensity.rgb;
    return float4(max(0.0f, finalColor), 1.0f);
}