cbuffer CbVignette : register(b0)
{
    float4 v_color;
    float2 v_center;
    float v_intensity;
    float v_smoothness;
    float v_rounded;
    float v_roundness;
    float2 padding;
};

struct VS_OUT
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

Texture2D sceneTexture : register(t0);
SamplerState linearSampler : register(s0);

float4 main(VS_OUT pin) : SV_TARGET
{
    float4 color = sceneTexture.Sample(linearSampler, pin.texcoord);
    
    float width, height;
    sceneTexture.GetDimensions(width, height);
    
    float2 coord = pin.texcoord - v_center;
    coord.x *= lerp(1.0f, width / height, v_rounded);
    
    float distSq = dot(coord, coord);
    float mask = saturate(1.0f - distSq * v_intensity);
    mask = smoothstep(0.0f, v_smoothness, mask);
    
    return lerp(v_color, color, mask);
}