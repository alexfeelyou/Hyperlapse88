struct VS_OUT
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

Texture2D<float4> sceneTexture : register(t0);
Texture2D<float4> lutTexture : register(t1);

SamplerState pointSampler : register(s0);
SamplerState linearSampler : register(s1);

cbuffer CbColorGrading : register(b0)
{
    float4 exposureAndWhiteBalance; // rgb: Precomputed 2^EV * WhiteBalance, w: Contrast
    float4 shadowLift; // rgb: Pre-linearized shadow lift color, w: Saturation
    float4 highlightGain; // rgb: Pre-linearized highlight gain color, w: unused
    float4 lutParams; // x: lutSize, y: 1.0 / lutSize, z: lutIntensity, w: unused
};

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

// Hardware-accelerated Trilinear 3D LUT lookup across an unwrapped 2D strip (e.g., 1024x32 or 256x16)
float3 SampleUnwrappedLut3D(float3 srgbColor, float lutSize, float invLutSize)
{
    float3 scaledColor = saturate(srgbColor) * (lutSize - 1.0f);

    float slice0 = floor(scaledColor.b);
    float slice1 = min(slice0 + 1.0f, lutSize - 1.0f);
    float sliceLerp = scaledColor.b - slice0;

    float2 uvBase = (scaledColor.rg + 0.5f) * invLutSize;
    uvBase.x *= invLutSize;

    float2 uv0 = float2(uvBase.x + slice0 * invLutSize, uvBase.y);
    float2 uv1 = float2(uvBase.x + slice1 * invLutSize, uvBase.y);

    float3 lut0 = lutTexture.SampleLevel(linearSampler, uv0, 0.0f).rgb;
    float3 lut1 = lutTexture.SampleLevel(linearSampler, uv1, 0.0f).rgb;
    return lerp(lut0, lut1, sliceLerp);
}

// Jorge Jimenez Interleaved Gradient Noise for 8-bit swapchain anti-banding
float InterleavedGradientNoise(float2 pixelPos)
{
    return frac(52.9829189f * frac(dot(pixelPos, float2(0.06711056f, 0.00583715f))));
}

float4 main(VS_OUT pin) : SV_TARGET
{
    // Fetch Linear HDR Scene & apply CPU-precomputed Exposure + White Balance
    float3 hdrColor = max(0.0f, sceneTexture.SampleLevel(pointSampler, pin.texcoord, 0.0f).rgb);
    hdrColor *= exposureAndWhiteBalance.rgb;

    // Watercolor Shadow Lift (softly lifts dark crevices) & Golden Highlight Gain
    float linearLuma = dot(hdrColor, float3(0.2126f, 0.7152f, 0.0722f));
    float shadowWeight = saturate(1.0f - linearLuma);
    shadowWeight *= shadowWeight;
    hdrColor = max(0.0f, hdrColor * highlightGain.rgb + shadowLift.rgb * shadowWeight);

    // Compress unbounded linear HDR into display-referred [0, 1] via ACES Filmic curve
    float3 ldrLinear = ACESFilmic(hdrColor);

    // Convert to perceptual display sRGB (1.0 / 2.2)
    float3 srgbColor = pow(max(0.0f, ldrLinear), 1.0f / 2.2f);

    // Perceptual Midpoint Contrast & Watercolor Saturation
    srgbColor = saturate((srgbColor - 0.5f) * exposureAndWhiteBalance.w + 0.5f);
    float srgbLuma = dot(srgbColor, float3(0.299f, 0.587f, 0.114f));
    srgbColor = saturate(lerp(srgbLuma.xxx, srgbColor, shadowLift.w));

    // Optional 3D LUT Strip Trilinear Grading (Zero texture fetches when disabled)
    [branch]
    if (lutParams.z > 0.0001f)
    {
        float3 lutGraded = SampleUnwrappedLut3D(srgbColor, lutParams.x, lutParams.y);
        srgbColor = lerp(srgbColor, lutGraded, lutParams.z);
    }

    // 7. Apply +/- 0.5 LSB dither before 8-bit B8G8R8A8_UNORM quantization to eliminate sky banding
    float dither = (InterleavedGradientNoise(pin.position.xy) - 0.5f) / 255.0f;
    srgbColor = saturate(srgbColor + dither);

    return float4(srgbColor, 1.0f);
}