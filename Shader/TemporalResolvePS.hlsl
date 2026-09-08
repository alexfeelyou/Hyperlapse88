Texture2D CurrentColor : register(t0);
Texture2D SceneDepth : register(t1); // already bound globally by PostProcessManager
Texture2D VelocityBuffer : register(t2);
Texture2D HistoryColor : register(t3);
SamplerState LinearSampler : register(s0);

cbuffer CbTemporalAA : register(b0)
{
    float historyWeight;
    float sharpenAmount;
    float2 texelSize; // 1/width, 1/height — avoids hardcoding resolution in the shader
};

float4 main(float4 position : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET
{
    const float2 velocity = VelocityBuffer.Sample(LinearSampler, uv).xy;
    const float2 previousUV = uv - velocity;

    const float3 current = CurrentColor.Sample(LinearSampler, uv).rgb;

    // Neighborhood clamp 
    float3 minColor = current;
    float3 maxColor = current;
    [unroll]
    for (int x = -1; x <= 1; ++x)
    {
        [unroll]
        for (int y = -1; y <= 1; ++y)
        {
            if (x == 0 && y == 0)
                continue;
            const float3 tap = CurrentColor.Sample(LinearSampler, uv + float2(x, y) * texelSize).rgb;
            minColor = min(minColor, tap);
            maxColor = max(maxColor, tap);
        }
    }

    const bool offScreen = previousUV.x < 0.0f || previousUV.x > 1.0f || previousUV.y < 0.0f || previousUV.y > 1.0f;
    float3 history = HistoryColor.Sample(LinearSampler, previousUV).rgb;
    history = clamp(history, minColor, maxColor);

    const float weight = offScreen ? 0.0f : historyWeight;
    float3 resolved = lerp(current, history, weight);

    // Push the resolved pixel away from its local blurred average.
    const float3 blurredAvg = (minColor + maxColor) * 0.5f;
    resolved += (resolved - blurredAvg) * sharpenAmount;

    return float4(resolved, 1.0f);
}