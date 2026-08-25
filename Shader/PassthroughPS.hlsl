struct VS_OUT
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

Texture2D sceneTexture : register(t0);
SamplerState pointSampler : register(s0);

float4 main(VS_OUT pin) : SV_TARGET
{
    return sceneTexture.Sample(pointSampler, pin.texcoord);
}