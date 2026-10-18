struct VS_OUT
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

Texture2D<float4> sceneTexture : register(t0);
Texture2D<float> depthTexture : register(t1);
Texture2D<float> aoTexture : register(t3);

SamplerState pointSampler : register(s0);
SamplerState linearSampler : register(s1);

cbuffer CbSsao : register(b0)
{
    float4 projParams; // x: tanHalfFov * aspect, y: tanHalfFov, z: nearZ, w: farZ
    float4 texelSizes; // xy: halfResInvSize, zw: fullResInvSize
    float4 aoParams; // x: radius, y: intensity, z: bias, w: fadeDistance
    float4 aoColorAndPreserve; // rgb: pre-linearized crevice tint, w: directLightPreservation
    int passMode; // 0: Raw AO, 1: Blur H, 2: Blur V, 3: Bilateral Upsample & Composite
    float3 padding;
};

// Golden-Angle 8-Tap Vogel Spiral Kernel (pre-normalized sqrt(i + 0.5) / sqrt(8) radii)
static const float2 kSpiralDisk[8] =
{
    float2(0.250000f, 0.000000f),
    float2(-0.319152f, 0.290983f),
    float2(0.045492f, -0.557262f),
    float2(0.487139f, 0.487989f),
    float2(-0.779353f, -0.072579f),
    float2(0.596637f, -0.675490f),
    float2(-0.044225f, 0.900215f),
    float2(-0.669131f, -0.713939f)
};

// Snaps any UV coordinate to the exact center of the top-left full-resolution depth texel
// Eliminates IEEE-754 boundary rounding jitter when sampling full-res depth from half-res passes
float2 SnapToFullResCenter(float2 uv)
{
    float2 fullRes = rcp(texelSizes.zw);
    return (floor(uv * fullRes) + 0.5f) * texelSizes.zw;
}

// Converts hardware non-linear depth [0, 1] into linear View-Space Z [nearZ, farZ]
float GetLinearViewZ(float depthRaw)
{
    float nearZ = projParams.z;
    float farZ = projParams.w;
    return (nearZ * farZ) * rcp(max(farZ - depthRaw * (farZ - nearZ), 0.00001f));
}

// Reconstructs 3D View-Space Position (LH: +X right, +Y up, +Z forward) in 1 rcp + 4 muls
float3 ReconstructViewPos(float2 uv, float depthRaw)
{
    float viewZ = GetLinearViewZ(depthRaw);
    float2 ndc = uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
    float2 viewXY = ndc * projParams.xy * viewZ;
    return float3(viewXY, viewZ);
}

// Reconstructs artifact-free View-Space Normal using 4-Tap Min-Delta Best-Pair Selection
float3 ReconstructViewNormal(float2 snappedUV, float3 centerPos)
{
    float2 stepUV = texelSizes.zw; // Exact 1-texel step from snapped full-res center

    float dL = depthTexture.SampleLevel(pointSampler, snappedUV + float2(-stepUV.x, 0.0f), 0.0f);
    float dR = depthTexture.SampleLevel(pointSampler, snappedUV + float2(stepUV.x, 0.0f), 0.0f);
    float dT = depthTexture.SampleLevel(pointSampler, snappedUV + float2(0.0f, -stepUV.y), 0.0f);
    float dB = depthTexture.SampleLevel(pointSampler, snappedUV + float2(0.0f, stepUV.y), 0.0f);

    float3 pL = ReconstructViewPos(snappedUV + float2(-stepUV.x, 0.0f), dL);
    float3 pR = ReconstructViewPos(snappedUV + float2(stepUV.x, 0.0f), dR);
    float3 pT = ReconstructViewPos(snappedUV + float2(0.0f, -stepUV.y), dT);
    float3 pB = ReconstructViewPos(snappedUV + float2(0.0f, stepUV.y), dB);

    float3 dPdx = (abs(pR.z - centerPos.z) < abs(centerPos.z - pL.z)) ? (pR - centerPos) : (centerPos - pL);
    float3 dPdy = (abs(pB.z - centerPos.z) < abs(centerPos.z - pT.z)) ? (pB - centerPos) : (centerPos - pT);

    return normalize(cross(dPdx, dPdy));
}

// Jorge Jimenez Interleaved Gradient Noise for per-pixel spiral rotation
float InterleavedGradientNoise(float2 pixelPos)
{
    return frac(52.9829189f * frac(dot(pixelPos, float2(0.06711056f, 0.00583715f))));
}

// Pass 0: Half-Resolution 8-Tap McGuire Scalable Ambient Obscurance (SAO)
float ComputeHalfResAO(float2 uv, float2 pixelPos)
{
    // Snap half-res UV to exact full-res texel center to eliminate boundary rounding stair-steps
    float2 snappedUV = SnapToFullResCenter(uv);
    float depthRaw = depthTexture.SampleLevel(pointSampler, snappedUV, 0.0f);
    if (depthRaw >= 0.9999f)
    {
        return 1.0f; // Skybox is unoccluded
    }

    float3 viewPos = ReconstructViewPos(snappedUV, depthRaw);
    float fadeFactor = saturate((aoParams.w - viewPos.z) * rcp(max(aoParams.w * 0.25f, 0.001f)));
    if (fadeFactor <= 0.001f)
    {
        return 1.0f;
    }

    float3 viewNormal = ReconstructViewNormal(snappedUV, viewPos);

    // Project world-space radius into UV screen space proportional to perspective depth
    float2 screenRadius = (aoParams.x * rcp(max(viewPos.z * projParams.xy, 0.001f))) * 0.5f;
    screenRadius = clamp(screenRadius, texelSizes.xy * 1.5f, float2(0.10f, 0.10f));

    // Rotate 8-tap Vogel disk per pixel using Interleaved Gradient Noise
    float angle = InterleavedGradientNoise(pixelPos) * 6.2831853f;
    float s, c;
    sincos(angle, s, c);
    float2x2 rotMat = float2x2(c, -s, s, c);

    float occlusion = 0.0f;
    float radius = aoParams.x;
    float radiusSq = radius * radius;
    float invRadiusSq = rcp(max(radiusSq, 0.0001f));
    // Scale bias proportionally with view depth to prevent oblique surface acne
    float depthBias = aoParams.z * (1.0f + viewPos.z * 0.015f);

    [unroll]
    for (int i = 0; i < 8; ++i)
    {
        float2 offsetUV = mul(kSpiralDisk[i], rotMat) * screenRadius;
        float2 sampleUV = SnapToFullResCenter(saturate(snappedUV + offsetUV));

        float sampleDepthRaw = depthTexture.SampleLevel(pointSampler, sampleUV, 0.0f);
        float3 samplePos = ReconstructViewPos(sampleUV, sampleDepthRaw);

        float3 v = samplePos - viewPos;
        float distSq = dot(v, v);
        float vDotN = dot(v, viewNormal) - depthBias;

        // McGuire SAO Estimator: naturally weights near contact crevices while smoothly attenuating outer taps
        float falloff = saturate(1.0f - distSq * invRadiusSq);
        float aoTap = max(0.0f, vDotN * rcp(distSq + 0.015f));
        occlusion += aoTap * falloff * falloff;
    }

    occlusion = saturate((occlusion * (1.0f / 8.0f)) * aoParams.y * 1.35f * fadeFactor);
    return 1.0f - occlusion;
}

// Pass 1 & 2: Separable 4x4 (4-Tap H + 4-Tap V) Slope-Compensated Bilateral Filter
// A 4-tap window [-1, 0, +1, +2] exactly cancels the 4x4 spatial period of Interleaved Gradient Noise
float ComputeSeparableBilateralBlur(float2 uv, float2 axisStep)
{
    float2 centerDepthUV = SnapToFullResCenter(uv);
    float centerDepthRaw = depthTexture.SampleLevel(pointSampler, centerDepthUV, 0.0f);
    if (centerDepthRaw >= 0.9999f)
    {
        return 1.0f;
    }

    float centerZ = GetLinearViewZ(centerDepthRaw);
    float centerAO = aoTexture.SampleLevel(pointSampler, uv, 0.0f);

    // Estimate local planar slope (dZ/dStep) using min-delta neighbors so oblique walls blur smoothly
    float zMinus = GetLinearViewZ(depthTexture.SampleLevel(pointSampler, SnapToFullResCenter(uv - axisStep), 0.0f));
    float zPlus = GetLinearViewZ(depthTexture.SampleLevel(pointSampler, SnapToFullResCenter(uv + axisStep), 0.0f));
    float slopeZ = (abs(zPlus - centerZ) < abs(centerZ - zMinus)) ? (zPlus - centerZ) : (centerZ - zMinus);

    float invSlopeTolerance = rcp(max(aoParams.x * 0.35f, 0.03f));

    // Tap k = 0 (Center)
    float totalAO = centerAO;
    float totalWeight = 1.0f;

    static const int kOffsets[3] = { -1, 1, 2 };
    static const float kSpatialWeights[3] = { 0.85f, 0.85f, 0.55f };

    [unroll]
    for (int i = 0; i < 3; ++i)
    {
        float k = float(kOffsets[i]);
        float2 sampleUV = uv + axisStep * k;
        float sampleAO = aoTexture.SampleLevel(pointSampler, sampleUV, 0.0f);
        float sampleZ = GetLinearViewZ(depthTexture.SampleLevel(pointSampler, SnapToFullResCenter(sampleUV), 0.0f));

        // Compare actual sample depth against expected planar wall depth (centerZ + slopeZ * k)
        float expectedZ = centerZ + slopeZ * k;
        float planeError = abs(sampleZ - expectedZ) * invSlopeTolerance;
        float w = exp2(-planeError * planeError * 3.0f) * kSpatialWeights[i];

        totalAO += sampleAO * w;
        totalWeight += w;
    }

    return totalAO * rcp(totalWeight);
}

// Pass 3: Full-Resolution Exact 2x2 Bilateral Tent Upsample & Radiance-Preserving Composite
float4 CompositeUpsampleAO(float2 uv)
{
    float4 sceneColor = sceneTexture.SampleLevel(pointSampler, uv, 0.0f);
    float fullDepthRaw = depthTexture.SampleLevel(pointSampler, uv, 0.0f);
    if (fullDepthRaw >= 0.9999f)
    {
        return sceneColor;
    }

    float fullZ = GetLinearViewZ(fullDepthRaw);
    float invDepthTolerance = rcp(max(aoParams.x * 0.35f, 0.03f));

    // Locate the exact 2x2 neighborhood of half-resolution texel centers surrounding this full-res pixel
    float2 halfResSize = rcp(texelSizes.xy);
    float2 halfGridCoord = uv * halfResSize - 0.5f;
    float2 baseHalfTexel = floor(halfGridCoord);
    float2 f = frac(halfGridCoord); // Bilinear tent weights in [0, 1]

    float2 baseHalfUV = (baseHalfTexel + 0.5f) * texelSizes.xy;

    float2 offsets[4] =
    {
        float2(0.0f, 0.0f),
        float2(texelSizes.x, 0.0f),
        float2(0.0f, texelSizes.y),
        float2(texelSizes.x, texelSizes.y)
    };

    float bilinearWeights[4] =
    {
        (1.0f - f.x) * (1.0f - f.y),
        f.x * (1.0f - f.y),
        (1.0f - f.x) * f.y,
        f.x * f.y
    };

    float accumAO = 0.0f;
    float accumWeight = 0.0f;

    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        float2 tapHalfUV = saturate(baseHalfUV + offsets[i]);

        // Point-sample the exact half-res AO texel and its exact corresponding full-res depth texel
        float tapAO = aoTexture.SampleLevel(pointSampler, tapHalfUV, 0.0f);
        float tapZ = GetLinearViewZ(depthTexture.SampleLevel(pointSampler, SnapToFullResCenter(tapHalfUV), 0.0f));

        float depthDiff = abs(tapZ - fullZ) * invDepthTolerance;
        float depthWeight = exp2(-depthDiff * depthDiff * 4.0f);
        float w = bilinearWeights[i] * depthWeight + 0.0001f;

        accumAO += tapAO * w;
        accumWeight += w;
    }

    float ao = saturate(accumAO * rcp(accumWeight));

    // Preserve direct sunlight highlights & emissives: AO primarily darkens ambient crevices
    float luma = dot(sceneColor.rgb, float3(0.2126f, 0.7152f, 0.0722f));
    float directLightMask = rcp(1.0f + luma * aoColorAndPreserve.w);
    float effectiveOcclusion = (1.0f - ao) * directLightMask;

    // Tint occluded crevices toward the watercolor shadow tint rather than dead black
    float3 occludedColor = sceneColor.rgb * aoColorAndPreserve.rgb;
    float3 finalRGB = lerp(sceneColor.rgb, occludedColor, effectiveOcclusion);

    return float4(max(0.0f, finalRGB), sceneColor.a);
}

float4 main(VS_OUT pin) : SV_TARGET
{
    if (passMode == 0)
    {
        float rawAO = ComputeHalfResAO(pin.texcoord, pin.position.xy);
        return float4(rawAO.xxx, 1.0f);
    }
    else if (passMode == 1)
    {
        float blurH = ComputeSeparableBilateralBlur(pin.texcoord, float2(texelSizes.x, 0.0f));
        return float4(blurH.xxx, 1.0f);
    }
    else if (passMode == 2)
    {
        float blurV = ComputeSeparableBilateralBlur(pin.texcoord, float2(0.0f, texelSizes.y));
        return float4(blurV.xxx, 1.0f);
    }

    return CompositeUpsampleAO(pin.texcoord);
}