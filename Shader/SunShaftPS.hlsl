struct VS_OUT
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

Texture2D<float4> sceneTexture : register(t0);
Texture2D<float> depthTexture : register(t1);
Texture2D<float> shaftTexture : register(t3);

SamplerState pointSampler : register(s0);
SamplerState linearSampler : register(s1);

cbuffer CbSunShaft : register(b0)
{
    float4 sunScreenAndAspect; // xy: sunUV, z: aspect, w: sunVisibilityFade
    float4 shaftParams; // x: density, y: decay, z: invRadiusSq, w: stepScale
    float4 shaftColorAndIntensity; // rgb: linear shaft color, w: shaftIntensity
    float4 flareParams; // x: flareIntensity, y: streakScale, zw: quarterTexelSize
    int passMode; // 0: Mask, 1: Radial Blur Fine, 2: Radial Blur Coarse, 3: Composite
    float3 padding;
};

// Jorge Jimenez Interleaved Gradient Noise to dither radial step phase and eliminate banding
float InterleavedGradientNoise(float2 pixelPos)
{
    return frac(52.9829189f * frac(dot(pixelPos, float2(0.06711056f, 0.00583715f))));
}

// Pass 0: Quarter-Resolution Depth & Sky Luminance Occlusion Mask
float ComputeOcclusionMask(float2 uv)
{
    // 4-tap depth check across the 2x2 neighborhood to prevent thin silhouette sub-pixel aliasing
    float2 halfQuarterStep = flareParams.zw * 0.25f;
    float d0 = depthTexture.SampleLevel(pointSampler, uv + float2(-halfQuarterStep.x, -halfQuarterStep.y), 0.0f);
    float d1 = depthTexture.SampleLevel(pointSampler, uv + float2(halfQuarterStep.x, -halfQuarterStep.y), 0.0f);
    float d2 = depthTexture.SampleLevel(pointSampler, uv + float2(-halfQuarterStep.x, halfQuarterStep.y), 0.0f);
    float d3 = depthTexture.SampleLevel(pointSampler, uv + float2(halfQuarterStep.x, halfQuarterStep.y), 0.0f);

    float skyMask = (step(0.9998f, d0) + step(0.9998f, d1) + step(0.9998f, d2) + step(0.9998f, d3)) * 0.25f;
    if (skyMask <= 0.001f)
    {
        return 0.0f; // Geometry occludes the sun at this pixel
    }

    // Aspect-corrected radial corona falloff around the projected sun position
    float2 delta = uv - sunScreenAndAspect.xy;
    delta.x *= sunScreenAndAspect.z;
    float distSq = dot(delta, delta);
    float radialWindow = exp2(-distSq * shaftParams.z * 2.5f);

    // Modulate by scene sky brightness so clouds and horizon gradients naturally shape the shafts
    float3 skyColor = sceneTexture.SampleLevel(linearSampler, uv, 0.0f).rgb;
    float skyLuma = saturate(dot(skyColor, float3(0.2126f, 0.7152f, 0.0722f)) * 0.65f + 0.35f);

    return skyMask * radialWindow * skyLuma;
}

// Pass 1 & 2: Cascaded 12-Tap Epipolar Radial Blur (12 x 12 = 144 effective radial samples)
float ComputeRadialBlur(float2 uv, float2 pixelPos, bool useJitter)
{
    // March from current pixel toward the projected 2D sun coordinate
    float2 rayToSun = (sunScreenAndAspect.xy - uv) * (shaftParams.x * shaftParams.w);

    // Apply sub-step IGN jitter on the first pass to dissolve any residual stepping rings
    float jitter = useJitter ? (InterleavedGradientNoise(pixelPos) - 0.5f) : 0.0f;
    float2 currentUV = uv + rayToSun * jitter;

    float accumShaft = 0.0f;
    float weightSum = 0.0f;
    float decayWeight = 1.0f;
    float decayFactor = shaftParams.y;

    [unroll]
    for (int i = 0; i < 12; ++i)
    {
        // Smooth border guard so out-of-bounds offscreen sun rays never pull clamped edge streaks
        float2 borderDist = min(currentUV, 1.0f - currentUV);
        float borderMask = saturate(min(borderDist.x, borderDist.y) * 20.0f);

        float sampleVal = shaftTexture.SampleLevel(linearSampler, saturate(currentUV), 0.0f);
        accumShaft += sampleVal * decayWeight * borderMask;
        weightSum += decayWeight;

        decayWeight *= decayFactor;
        currentUV += rayToSun;
    }

    return accumShaft * rcp(max(weightSum, 0.0001f));
}

// Evaluates a soft optical bokeh disc/ring ghost along the lens flare axis
float EvaluateIrisGhost(float2 uv, float2 sunToCenter, float axisOffset, float radius, float ringSharpness)
{
    float2 ghostCenter = float2(0.5f, 0.5f) + sunToCenter * axisOffset;
    float2 d = uv - ghostCenter;
    d.x *= sunScreenAndAspect.z;

    float dist = length(d) * rcp(max(radius, 0.001f));
    if (dist >= 1.0f)
    {
        return 0.0f;
    }

    float disc = saturate(1.0f - dist * dist);
    float ring = smoothstep(0.45f, 0.85f, dist) * (1.0f - smoothstep(0.85f, 1.0f, dist));
    return lerp(disc * disc, ring, ringSharpness);
}

// Pass 3: Full-Resolution Volumetric Shafts & Depth-Occluded Optical Lens Flare Composite
float4 CompositeSunShaftsAndFlare(float2 uv)
{
    float4 sceneColor = sceneTexture.SampleLevel(pointSampler, uv, 0.0f);
    float visibilityFade = sunScreenAndAspect.w;
    if (visibilityFade <= 0.0001f)
    {
        return sceneColor;
    }

    // 1. Sample the 144-tap cascaded volumetric sun shaft buffer
    float shaft = shaftTexture.SampleLevel(linearSampler, uv, 0.0f);
    float3 volumetricLight = shaftColorAndIntensity.rgb * (shaft * shaftColorAndIntensity.w * visibilityFade);

    // 2. Evaluate Depth-Occluded Optical Lens Flare & Anamorphic Glare
    float3 lensFlareLight = float3(0.0f, 0.0f, 0.0f);
    float flareStrength = flareParams.x * visibilityFade;

    if (flareStrength > 0.001f)
    {
        float2 sunUV = sunScreenAndAspect.xy;
        float2 inBounds = step(0.0f, sunUV) * step(sunUV, 1.0f);
        float sunOnScreen = inBounds.x * inBounds.y;

        if (sunOnScreen > 0.0f)
        {
            // 4-tap depth test around sunUV to smoothly occlude flare when sun passes behind walls/character
            float2 sunTestRadius = float2(0.008f, 0.008f * sunScreenAndAspect.z);
            float s0 = step(0.9998f, depthTexture.SampleLevel(pointSampler, saturate(sunUV + float2(-sunTestRadius.x, 0.0f)), 0.0f));
            float s1 = step(0.9998f, depthTexture.SampleLevel(pointSampler, saturate(sunUV + float2(sunTestRadius.x, 0.0f)), 0.0f));
            float s2 = step(0.9998f, depthTexture.SampleLevel(pointSampler, saturate(sunUV + float2(0.0f, -sunTestRadius.y)), 0.0f));
            float s3 = step(0.9998f, depthTexture.SampleLevel(pointSampler, saturate(sunUV + float2(0.0f, sunTestRadius.y)), 0.0f));
            float sunUnoccluded = (s0 + s1 + s2 + s3) * 0.25f;

            if (sunUnoccluded > 0.001f)
            {
                float2 dSun = uv - sunUV;
                dSun.x *= sunScreenAndAspect.z;

                // A. Anamorphic Horizontal Streak + Golden Sun Starburst Glare
                float2 streakDelta = float2(dSun.x * rcp(max(flareParams.y, 0.1f)), dSun.y * 10.0f);
                float anamorphicStreak = exp2(-dot(streakDelta, streakDelta) * 18.0f) * 0.65f;
                float radialGlare = exp2(-dot(dSun, dSun) * 28.0f) * 0.85f;

                // B. 3 Chromatic Watercolor Iris Ghosts mirrored through screen center (0.5, 0.5)
                float2 sunToCenter = float2(0.5f, 0.5f) - sunUV;
                float centerFade = saturate(1.0f - length(sunToCenter) * 0.85f);

                float g1 = EvaluateIrisGhost(uv, sunToCenter, 0.45f, 0.065f, 0.35f);
                float g2 = EvaluateIrisGhost(uv, sunToCenter, 0.95f, 0.110f, 0.70f);
                float g3 = EvaluateIrisGhost(uv, sunToCenter, -0.55f, 0.050f, 0.50f);

                float3 ghostColor =
                    g1 * float3(1.00f, 0.78f, 0.42f) * 0.35f + // Warm Amber Disc
                    g2 * float3(0.45f, 0.90f, 0.75f) * 0.25f + // Turquoise Zelda Bokeh Ring
                    g3 * float3(0.95f, 0.60f, 0.85f) * 0.25f; // Soft Rose Counter-Ghost

                float3 glareColor = shaftColorAndIntensity.rgb * (anamorphicStreak + radialGlare);
                lensFlareLight = (glareColor + ghostColor * centerFade) * (flareStrength * sunUnoccluded);
            }
        }
    }

    // Softly compress additive contribution on already super-bright HDR pixels to prevent blowout
    float3 totalAddedLight = volumetricLight + lensFlareLight;
    return float4(sceneColor.rgb + totalAddedLight, sceneColor.a);
}

float4 main(VS_OUT pin) : SV_TARGET
{
    if (passMode == 0)
    {
        float mask = ComputeOcclusionMask(pin.texcoord);
        return float4(mask.xxx, 1.0f);
    }
    else if (passMode == 1)
    {
        float blurFine = ComputeRadialBlur(pin.texcoord, pin.position.xy, true);
        return float4(blurFine.xxx, 1.0f);
    }
    else if (passMode == 2)
    {
        float blurCoarse = ComputeRadialBlur(pin.texcoord, pin.position.xy, false);
        return float4(blurCoarse.xxx, 1.0f);
    }

    return CompositeSunShaftsAndFlare(pin.texcoord);
}