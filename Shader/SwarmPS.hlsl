Texture2D SpriteTexture : register(t0);
SamplerState Sampler : register(s0);

struct VS_OUT
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
    float4 color : COLOR;
};

float4 main(VS_OUT pin) : SV_TARGET
{
    // Kalikan warna gambar asli dengan warna dari Instance Data
    // (Berguna untuk kasih efek merah/glitch saat jendela errornya "marah")
    float4 texColor = SpriteTexture.Sample(Sampler, pin.texcoord);
    return texColor * pin.color;
}