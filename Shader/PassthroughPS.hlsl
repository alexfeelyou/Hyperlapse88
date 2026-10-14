struct VS_OUT
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

Texture2D sceneTexture : register(t0);
SamplerState pointSampler : register(s0);

// Narkowicz rational polynomial ACES Filmic Tone Mapping curve (4 MADs, zero transcendental ops)
float3 ACESFilmic(float3 x)
{
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// Jorge Jimenez Interleaved Gradient Noise for 8-bit swapchain anti-banding
float InterleavedGradientNoise(float2 pixelPos)
{
    return frac(52.9829189f * frac(dot(pixelPos, float2(0.06711056f, 0.00583715f))));
}

float4 main(VS_OUT pin) : SV_TARGET
{
    float3 hdrColor = max(0.0f, sceneTexture.Sample(pointSampler, pin.texcoord).rgb);

    // Compress unbounded linear HDR into display-referred [0, 1] via ACES Filmic curve
    float3 ldrLinear = ACESFilmic(hdrColor);

    // Apply sRGB display gamma transfer (1.0 / 2.2)
    float3 srgbColor = pow(ldrLinear, 1.0f / 2.2f);

    // Apply +/- 0.5 LSB dither before 8-bit B8G8R8A8_UNORM quantization to eliminate sky banding
    float dither = (InterleavedGradientNoise(pin.position.xy) - 0.5f) / 255.0f;
    srgbColor = saturate(srgbColor + dither);

    return float4(srgbColor, 1.0f);
}