struct VS_OUT
{
    float4 position : SV_POSITION;
    float4 worldPosition : WORLD_POSITION;
};

cbuffer SKYMAP_CONSTANT_BUFFER : register(b7)
{
    row_major float4x4 inverseViewProjection;
    float4 cameraPosition;
    float4 sunDirectionAndCosRadius; // xyz: Normalized direction TO sun, w: cos(sunAngularRadius)
    float4 sunColorAndCorona; // rgb: Pre-linearized sun radiance, w: Corona intensity
};

// TextureCube
TextureCube skyTexture : register(t0);
SamplerState skySampler : register(s0);

float4 main(VS_OUT pin) : SV_TARGET
{
    float3 viewDir = normalize(pin.worldPosition.xyz - cameraPosition.xyz);
    float4 skyColor = skyTexture.Sample(skySampler, viewDir);

    // Linearize sRGB cubemap texels into linear HDR space
    float3 linearSky = pow(abs(skyColor.rgb), 2.2f);

    // Analytic HDR Sun Disc & Atmospheric Mie Corona (Zero transcendental pow() calls)
    float cosTheta = saturate(dot(viewDir, sunDirectionAndCosRadius.xyz));

    // Limb-darkened solar core (triggers HDR bloom pyramid > 1.0)
    float sunDisc = smoothstep(sunDirectionAndCosRadius.w, sunDirectionAndCosRadius.w + 0.0006f, cosTheta);

    // Exponential Mie corona + 4th-order wide atmospheric scatter
    float tightCorona = exp2((cosTheta - 1.0f) * 80.0f) * sunColorAndCorona.w;
    float cosTheta2 = cosTheta * cosTheta;
    float wideBloomHalo = (cosTheta2 * cosTheta2) * 0.20f;

    float3 solarRadiance = sunColorAndCorona.rgb * (sunDisc * 12.0f + tightCorona + wideBloomHalo);

    return float4(linearSky + solarRadiance, skyColor.a);
}