cbuffer CbPSX : register(b0)
{
    float psxResWidth;
    float psxResHeight;
    float psxColorDepth;
    float psxDitherStrength;
};

struct VS_OUT
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

Texture2D sceneTexture : register(t0);
SamplerState pointSampler : register(s0);

static const float4x4 BayerMatrix = float4x4(
    0.0 / 16.0, 8.0 / 16.0, 2.0 / 16.0, 10.0 / 16.0,
    12.0 / 16.0, 4.0 / 16.0, 14.0 / 16.0, 6.0 / 16.0,
    3.0 / 16.0, 11.0 / 16.0, 1.0 / 16.0, 9.0 / 16.0,
    15.0 / 16.0, 7.0 / 16.0, 13.0 / 16.0, 5.0 / 16.0
);

float4 main(VS_OUT pin) : SV_TARGET
{
    float2 psxRes = float2(psxResWidth, psxResHeight);
    float2 uv = floor(pin.texcoord * psxRes) / psxRes;
    
    float4 color = sceneTexture.Sample(pointSampler, uv);
    
    float2 screenPos = pin.texcoord * psxRes;
    int x = int(fmod(screenPos.x, 4.0));
    int y = int(fmod(screenPos.y, 4.0));
    float dither = (BayerMatrix[x][y] - 0.5f) * psxDitherStrength;

    color.r = floor((color.r + dither / psxColorDepth) * psxColorDepth) / psxColorDepth;
    color.g = floor((color.g + dither / psxColorDepth) * psxColorDepth) / psxColorDepth;
    color.b = floor((color.b + dither / psxColorDepth) * psxColorDepth) / psxColorDepth;
    
    return color;
}