Texture2D<float4> CurrentColor : register(t0);
Texture2D<float> SceneDepth : register(t1);
Texture2D<float2> VelocityBuffer : register(t2);
Texture2D<float4> HistoryColor : register(t3);

SamplerState LinearSampler : register(s0);

cbuffer CbTemporalAA : register(b0)
{
    float historyWeight;
    float sharpenAmount;
    float2 texelSize;
};

// Fast YCoCg conversions using additions and bit-shifts
float3 RGBToYCoCg(float3 c)
{
    return float3(
         0.25f * c.r + 0.5f * c.g + 0.25f * c.b,
         0.50f * c.r - 0.50f * c.b,
        -0.25f * c.r + 0.5f * c.g - 0.25f * c.b
    );
}

float3 YCoCgToRGB(float3 c)
{
    return float3(
        c.x + c.y - c.z,
        c.x + c.z,
        c.x - c.y - c.z
    );
}

float4 main(float4 position : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET
{
    const int2 pixelCoord = int2(position.xy);

    // 3x3 Velocity Dilation (Finds the motion vector of the closest foreground edge)
    int2 closestOffset = int2(0, 0);
    float closestDepth = SceneDepth.Load(int3(pixelCoord, 0));

    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            if (dx == 0 && dy == 0)
                continue;
            float d = SceneDepth.Load(int3(pixelCoord + int2(dx, dy), 0));
            if (d < closestDepth)
            {
                closestDepth = d;
                closestOffset = int2(dx, dy);
            }
        }
    }

    const float2 velocity = VelocityBuffer.Load(int3(pixelCoord + closestOffset, 0));
    const float2 previousUV = uv - velocity;

    // 5-Tap Cross Neighborhood (Direct integer loads from cache)
    const float3 cCenter = CurrentColor.Load(int3(pixelCoord, 0)).rgb;
    const float3 cTop = CurrentColor.Load(int3(pixelCoord + int2(0, -1), 0)).rgb;
    const float3 cBottom = CurrentColor.Load(int3(pixelCoord + int2(0, 1), 0)).rgb;
    const float3 cLeft = CurrentColor.Load(int3(pixelCoord + int2(-1, 0), 0)).rgb;
    const float3 cRight = CurrentColor.Load(int3(pixelCoord + int2(1, 0), 0)).rgb;

    // Convert to YCoCg for perceptual bounds calculation
    const float3 yCenter = RGBToYCoCg(cCenter);
    const float3 yTop = RGBToYCoCg(cTop);
    const float3 yBottom = RGBToYCoCg(cBottom);
    const float3 yLeft = RGBToYCoCg(cLeft);
    const float3 yRight = RGBToYCoCg(cRight);

    float3 yMin = min(yCenter, min(min(yTop, yBottom), min(yLeft, yRight)));
    float3 yMax = max(yCenter, max(max(yTop, yBottom), max(yLeft, yRight)));

    // Sample and Clamp History
    const bool offScreen = any(previousUV < 0.0f) || any(previousUV > 1.0f);
    float3 history = HistoryColor.SampleLevel(LinearSampler, previousUV, 0).rgb;
    float3 yHistory = RGBToYCoCg(history);
    
    // Clamp inside the local 5-tap neighborhood constraints
    yHistory = clamp(yHistory, yMin, yMax);
    history = YCoCgToRGB(yHistory);

    // Temporal Blend & Sharpen
    const float blendWeight = offScreen ? 0.0f : historyWeight;
    float3 resolved = lerp(cCenter, history, blendWeight);

    const float3 localAverage = (cTop + cBottom + cLeft + cRight) * 0.25f;
    resolved += (resolved - localAverage) * sharpenAmount;

    return float4(max(0.0f, resolved), 1.0f);
}