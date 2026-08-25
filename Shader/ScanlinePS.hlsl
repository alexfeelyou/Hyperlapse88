cbuffer CbScanline : register(b0)
{
    float crt_scanlineStrength;
    float crt_scanlineSpeed;
    float crt_scanlineSize;
    float crt_fineOpacity;
    float crt_fineDensity;
    float crt_fineRotation;
    float crt_time;
    float padding;
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

    if (crt_scanlineStrength > 0.0f)
    {
        float bar = sin(pin.texcoord.y * 3.0f - crt_time * crt_scanlineSpeed);
        bar = (bar + 1.0f) * 0.5f;
        bar = pow(bar, max(1.0f, crt_scanlineSize));
        color.rgb *= (1.0f - bar * crt_scanlineStrength * 0.5f);
    }

    if (crt_fineOpacity > 0.0f)
    {
        float s = sin(crt_fineRotation);
        float c = cos(crt_fineRotation);
        float2x2 rot = float2x2(c, -s, s, c);
        float2 centeredUV = pin.texcoord - 0.5f;
        float2 rotatedUV = mul(centeredUV, rot) + 0.5f;

        float mesh = sin(rotatedUV.y * crt_fineDensity * 50.0f);
        mesh = (mesh + 1.0f) * 0.5f;
        mesh = pow(mesh, 1.2f);
        color.rgb *= (1.0f - (1.0f - mesh) * crt_fineOpacity * 0.3f);
    }

    return color;
}