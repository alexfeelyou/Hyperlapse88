cbuffer UberConstantBuffer : register(b0)
{
    // Vignette Settings
    float4 v_color;
    float2 v_center;
    float v_intensity;
    float v_smoothness;
    float v_rounded; // 1.0 = true, 0.0 = false
    float v_roundness;
    
    // Lens Distortion / Glitch Settings
    float fx_blurStrength;
    float fx_chromaticAberration;
    float fx_distortion;
    float fx_glitchStrength;
    
    // CRT / Scanline Settings
    float crt_scanlineOpacity; 
    float crt_time;
    float crt_scanlineSpeed;
    float crt_scanlineSize; 
    
    float crt_fineOpacity; 
    float crt_fineDensity; 
    
    float fineRotation; 
    
    // HDR / Bloom Settings
    float bloomThreshold;
    float bloomIntensity;
    
    // PSX / Retro Settings
    float psxEnabled;
    float psxResWidth;
    float psxResHeight;
    float psxColorDepth;
    float psxDitherStrength;
    float3 padding_psx;
};

struct VS_OUT
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

Texture2D sceneTexture : register(t0);
SamplerState samplerState : register(s0);

static const int BLUR_SAMPLES = 10;

// HLSL Helper Functions
// Random Noise Generator
float rand(float2 n)
{
    return frac(sin(dot(n, float2(12.9898, 4.1414))) * 43758.5453);
}

// CRT / Fish Eye Distortion
float2 LensDistortion(float2 uv, float k)
{
    float2 t = uv - v_center;
    float r2 = t.x * t.x + t.y * t.y;
    float f = 1.0 + r2 * (k * 10.0f);
    return v_center + t * f;
}

// Standard 4x4 Bayer Dither Matrix used in Retro Graphics
static const float4x4 BayerMatrix = float4x4(
    0.0 / 16.0, 8.0 / 16.0, 2.0 / 16.0, 10.0 / 16.0,
    12.0 / 16.0, 4.0 / 16.0, 14.0 / 16.0, 6.0 / 16.0,
    3.0 / 16.0, 11.0 / 16.0, 1.0 / 16.0, 9.0 / 16.0,
    15.0 / 16.0, 7.0 / 16.0, 13.0 / 16.0, 5.0 / 16.0
);

// Single-Pass Vogel Disk Sampling for Bloom Effect
float3 SampleBloom(float2 uv, float2 texelSize, float maxRadius)
{
    float3 bloom = 0;
    float totalWeight = 0;

    // Reduced to 16 taps. On an upscaled low-res buffer, 
    // 16 taps provides a perfectly smooth Gaussian curve with half the GPU cost
    const int TAPS = 16;
    const float GOLDEN_ANGLE = 2.39996323;

    [unroll]
    for (int i = 0; i < TAPS; i++)
    {
        float r = sqrt(float(i) + 0.5f) / sqrt(float(TAPS));
        float theta = float(i) * GOLDEN_ANGLE;
        
        float2 offset = float2(cos(theta), sin(theta)) * (r * maxRadius);
        float3 c = sceneTexture.SampleLevel(samplerState, uv + offset * texelSize, 0).rgb;

        // Fast brightness approximation
        float brightness = dot(c, float3(0.2126, 0.7152, 0.0722));
        float contribution = max(0.0f, brightness - bloomThreshold);
        float weight = exp(-r * r * 3.0f);
        
        bloom += (c * (contribution / (brightness + 0.0001f))) * weight;
        totalWeight += weight;
    }
    
    return bloom / totalWeight;
}

// Main Pixel Shader Function
float4 main(VS_OUT pin) : SV_TARGET
{
    float2 uv = pin.texcoord;

    // PSX / Retro Pixelation Effect
    [branch]
    if (psxEnabled > 0.5f)
    {
        float2 psxRes = float2(psxResWidth, psxResHeight);
        // Floor the UVs to create chunky pixels
        uv = floor(uv * psxRes) / psxRes;
    }

    // Glitch Effect: Random Horizontal Shake
    if (fx_glitchStrength > 0.0f)
    {
        // Random horizontal shake based on Time + Y position
        float shake = (rand(float2(0, uv.y + crt_time)) - 0.5f) * fx_glitchStrength * 0.1f;
        uv.x += shake;
    }

    // Lens Distortion and Chromatic Aberration
    // Calculate Base UV with Lens Distortion (if active)
    float2 distortedUV = LensDistortion(uv, fx_distortion);

    // Lateral Chromatic Aberration: Shift R and B channels based on fx_chromaticAberration
    
    // A small multiplier to make the slider values easier to manage in the GUI
    float shiftAmount = fx_chromaticAberration * 0.5f;

    // Red shifts Left, Blue shifts Right (relative to center Green)
    float2 uvR = distortedUV + float2(shiftAmount, 0.0f);
    float2 uvG = distortedUV; 
    float2 uvB = distortedUV - float2(shiftAmount, 0.0f);

    // Black Border Check 
    [flatten]
    if (any(uvR < 0.0) || any(uvR > 1.0) || any(uvB < 0.0) || any(uvB > 1.0))
    {
        return float4(0, 0, 0, 1);
    }

    // Vignette Masking: Circular Gradient based on Distance from Center
    float width, height;
    sceneTexture.GetDimensions(width, height);
    
    float2 coord = uvG - v_center;
    float2 correctedCoord = coord;
    
    // Apply Aspect Ratio Correction based on Roundness setting
    correctedCoord.x *= lerp(1.0f, width / height, v_rounded);
    
    float distSq = dot(correctedCoord, correctedCoord);
    float mask = saturate(1.0f - distSq * v_intensity);
    mask = smoothstep(0.0f, v_smoothness, mask);

    // Apply Roundness to the Vignette
    float4 finalColor = float4(0, 0, 0, 1);

    if (fx_blurStrength > 0.0f)
    {
        // Radial Blur Logic
        float blurAmount = fx_blurStrength * distSq * 4.0f;
        float3 accumColor = float3(0, 0, 0);

        [unroll]
        for (int i = 0; i < BLUR_SAMPLES; i++)
        {
            float scale = 1.0f - blurAmount * (float(i) / float(BLUR_SAMPLES - 1));
            
            // Sample R, G, B separately using distorted UVs
            accumColor.r += sceneTexture.Sample(samplerState, v_center + (uvR - v_center) * scale).r;
            accumColor.g += sceneTexture.Sample(samplerState, v_center + (uvG - v_center) * scale).g;
            accumColor.b += sceneTexture.Sample(samplerState, v_center + (uvB - v_center) * scale).b;
        }
        finalColor.rgb = accumColor / float(BLUR_SAMPLES);
    }
    else
    {
        finalColor.r = sceneTexture.Sample(samplerState, uvR).r;
        finalColor.g = sceneTexture.Sample(samplerState, uvG).g;
        finalColor.b = sceneTexture.Sample(samplerState, uvB).b;
        
        if (any(isnan(finalColor)) || any(isinf(finalColor)))
        {
            finalColor = float4(0, 0, 0, 1);
        }
    }

    finalColor.rgb *= mask;

    // Roll in CRT Scanline Effects
    if (crt_scanlineOpacity > 0.0f)
    {
        float bar = sin(uvG.y * 3.0f - crt_time * crt_scanlineSpeed);
        bar = (bar + 1.0f) * 0.5f;
        bar = pow(bar, max(1.0f, crt_scanlineSize));
        
        finalColor.rgb *= (1.0f - bar * crt_scanlineOpacity * 0.5f);
    }

    // Fine CRT Mesh Overlay (Rotated)
    if (crt_fineOpacity > 0.0f)
    {
        float s = sin(fineRotation);
        float c = cos(fineRotation);
        float2x2 rotationMatrix = float2x2(c, -s, s, c);
        float2 centeredUV = uvG - 0.5f;
        float2 rotatedUV = mul(centeredUV, rotationMatrix);
        rotatedUV += 0.5f;

        float mesh = sin(rotatedUV.y * crt_fineDensity * 50.0f);
        mesh = (mesh + 1.0f) * 0.5f;
        mesh = pow(mesh, 1.2f);

        // [FIX] MULTIPLY instead of Subtract!
        finalColor.rgb *= (1.0f - (1.0f - mesh) * crt_fineOpacity * 0.3f);
    }
    
    // Voxel-Based Bloom Effect
    float2 texelSize = 1.0f / float2(width, height);

    float3 bloomColor = SampleBloom(uvG, texelSize, 15.0f);
    
    finalColor.rgb += (bloomColor * bloomIntensity);

    float exposure = 2.0f;
    finalColor.rgb *= exposure;
    
    finalColor.rgb = saturate((finalColor.rgb * (2.51f * finalColor.rgb + 0.03f)) /
                              (finalColor.rgb * (2.43f * finalColor.rgb + 0.59f) + 0.14f));
    
    // 5-Bit Color Crunch with Dithering
    [branch]
    if (psxEnabled > 0.5f)
    {
        // Calculate screen coordinate for the dither matrix
        float2 screenPos = pin.texcoord * float2(psxResWidth, psxResHeight);
        
        // Fetch the Bayer matrix value (using modulo to tile it 4x4)
        int x = int(fmod(screenPos.x, 4.0));
        int y = int(fmod(screenPos.y, 4.0));
        float dither = BayerMatrix[x][y];
        
        // Center the dither around 0 (-0.5 to 0.5) and apply strength
        dither = (dither - 0.5f) * psxDitherStrength;

        // Apply 5-bit color crunch (32 levels per channel)
        finalColor.r = floor((finalColor.r + dither / psxColorDepth) * psxColorDepth) / psxColorDepth;
        finalColor.g = floor((finalColor.g + dither / psxColorDepth) * psxColorDepth) / psxColorDepth;
        finalColor.b = floor((finalColor.b + dither / psxColorDepth) * psxColorDepth) / psxColorDepth;
    }
    
    return finalColor;
}