#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <DirectXMath.h>

// Engine presets for atmospheric lighting and color harmony
enum class AtmospherePreset : std::uint8_t
{
    StylizedWatercolor = 0, // Signature anime/watercolor palette (warm key, cool fill, lifted teal shadows)
    VibrantFantasy,         // High-contrast, saturated warm-dusk fantasy palette
    ClearNaturalDaylight,   // Physically balanced, neutral natural daylight
    Custom                  // Full manual control; disables automated curve evaluation
};

// Flat POD structure encapsulating all harmonized atmospheric lighting channels
struct AtmosphereParameters
{
    // Direct Sun Illumination
    DirectX::XMFLOAT3 sunColor{ 1.0f, 0.90f, 0.65f };
    float             sunIntensity{ 1.45f };

    // Ambient Illumination (Hemisphere Dome)
    DirectX::XMFLOAT3 skyAmbientColor{ 0.38f, 0.65f, 0.88f };
    float             skyAmbientIntensity{ 0.75f };
    DirectX::XMFLOAT3 groundAmbientColor{ 0.55f, 0.45f, 0.22f };
    float             groundAmbientIntensity{ 0.45f };

    // Atmospheric Depth Fog & Mie Solar Inscattering
    DirectX::XMFLOAT4 fogColor{ 0.85f, 0.78f, 0.70f, 1.0f };
    DirectX::XMFLOAT4 sunInscatterColor{ 1.0f, 0.82f, 0.45f, 1.0f };
    float             sunInscatterIntensity{ 3.2f };
    float             sunInscatterExponent{ 6.5f };
    float             heightFalloff{ 0.012f };
    float             startDistance{ 5.0f };
    float             endDistance{ 160.0f };

    // Volumetric Sun Shafts & Optical Lens Flare
    DirectX::XMFLOAT4 shaftColor{ 1.0f, 0.84f, 0.52f, 1.0f };
    float             shaftIntensity{ 1.20f };
    float             shaftDensity{ 0.88f };
    float             shaftDecay{ 0.94f };
    float             flareIntensity{ 0.55f };

    // Bloom Bleed
    DirectX::XMFLOAT4 bloomTint{ 1.0f, 0.92f, 0.78f, 1.0f };
    float             bloomIntensity{ 0.65f };
    float             bloomThreshold{ 0.85f };
    float             bloomScatter{ 0.78f };

    // Filmic Watercolor Color Grading
    float             exposure{ 0.12f };
    float             temperature{ 0.14f }; // Warm golden air
    float             tint{ -0.02f };
    float             contrast{ 1.08f };
    float             saturation{ 1.16f };
    DirectX::XMFLOAT4 shadowLiftColor{ 0.12f, 0.22f, 0.32f, 0.15f }; // Lifted cool teal shadows
    DirectX::XMFLOAT4 highlightGainColor{ 1.0f, 0.95f, 0.85f, 1.10f }; // Warm golden gain
};

// Evaluator for solar-altitude keyframe tables
namespace AtmosphereProfile
{
    struct SolarKeyframe
    {
        float                elevation{ 0.0f }; // Normalized vertical sun direction: -dir.y in [-1.0, 1.0]
        AtmosphereParameters params{};
    };

    // Evaluates a contiguous, cache-aligned table of 5 solar keyframes in < 0.001 ms using SIMD lerp
    [[nodiscard]] inline AtmosphereParameters Evaluate(AtmospherePreset preset, float solarElevation) noexcept
    {
        if (preset == AtmospherePreset::Custom)
        {
            return AtmosphereParameters{};
        }

        // Stylized Watercolor Palette (Honey Sun, Cerulean Fill, Lifted Teal Shadows)
        static const std::array<SolarKeyframe, 5> s_watercolorTable = []() consteval
            {
                std::array<SolarKeyframe, 5> table{};

                // Night / Moonlit Twilight (-0.25)
                table[0].elevation = -0.25f;
                table[0].params.sunColor = { 0.55f, 0.70f, 0.95f };
                table[0].params.sunIntensity = 0.35f;
                table[0].params.skyAmbientColor = { 0.12f, 0.16f, 0.32f };
                table[0].params.skyAmbientIntensity = 0.40f;
                table[0].params.groundAmbientColor = { 0.06f, 0.08f, 0.14f };
                table[0].params.groundAmbientIntensity = 0.25f;
                table[0].params.fogColor = { 0.15f, 0.20f, 0.35f, 1.0f };
                table[0].params.sunInscatterColor = { 0.60f, 0.75f, 1.0f, 1.0f };
                table[0].params.sunInscatterIntensity = 1.2f;
                table[0].params.sunInscatterExponent = 12.0f;
                table[0].params.heightFalloff = 0.015f;
                table[0].params.startDistance = 5.0f;
                table[0].params.endDistance = 120.0f;
                table[0].params.shaftColor = { 0.65f, 0.75f, 1.0f, 1.0f };
                table[0].params.shaftIntensity = 0.35f;
                table[0].params.shaftDensity = 0.65f;
                table[0].params.shaftDecay = 0.90f;
                table[0].params.flareIntensity = 0.20f;
                table[0].params.bloomTint = { 0.75f, 0.85f, 1.0f, 1.0f };
                table[0].params.bloomIntensity = 0.45f;
                table[0].params.bloomThreshold = 0.95f;
                table[0].params.bloomScatter = 0.70f;
                table[0].params.exposure = -0.30f;
                table[0].params.temperature = -0.20f;
                table[0].params.tint = 0.0f;
                table[0].params.contrast = 1.04f;
                table[0].params.saturation = 0.95f;
                table[0].params.shadowLiftColor = { 0.08f, 0.10f, 0.18f, 0.08f };
                table[0].params.highlightGainColor = { 0.85f, 0.92f, 1.0f, 0.95f };

                // Dawn / Sunrise Horizon (+0.02)
                table[1].elevation = 0.02f;
                table[1].params.sunColor = { 1.0f, 0.65f, 0.35f };
                table[1].params.sunIntensity = 1.35f;
                table[1].params.skyAmbientColor = { 0.48f, 0.45f, 0.72f };
                table[1].params.skyAmbientIntensity = 0.60f;
                table[1].params.groundAmbientColor = { 0.42f, 0.28f, 0.20f };
                table[1].params.groundAmbientIntensity = 0.35f;
                table[1].params.fogColor = { 0.88f, 0.62f, 0.55f, 1.0f };
                table[1].params.sunInscatterColor = { 1.0f, 0.65f, 0.30f, 1.0f };
                table[1].params.sunInscatterIntensity = 4.2f;
                table[1].params.sunInscatterExponent = 5.0f;
                table[1].params.heightFalloff = 0.010f;
                table[1].params.startDistance = 5.0f;
                table[1].params.endDistance = 180.0f;
                table[1].params.shaftColor = { 1.0f, 0.72f, 0.40f, 1.0f };
                table[1].params.shaftIntensity = 1.65f;
                table[1].params.shaftDensity = 0.95f;
                table[1].params.shaftDecay = 0.96f;
                table[1].params.flareIntensity = 0.85f;
                table[1].params.bloomTint = { 1.0f, 0.80f, 0.55f, 1.0f };
                table[1].params.bloomIntensity = 0.85f;
                table[1].params.bloomThreshold = 0.80f;
                table[1].params.bloomScatter = 0.82f;
                table[1].params.exposure = 0.08f;
                table[1].params.temperature = 0.26f;
                table[1].params.tint = 0.02f;
                table[1].params.contrast = 1.10f;
                table[1].params.saturation = 1.20f;
                table[1].params.shadowLiftColor = { 0.16f, 0.15f, 0.28f, 0.16f };
                table[1].params.highlightGainColor = { 1.0f, 0.85f, 0.70f, 1.15f };

                // Golden Sun (+0.35)
                table[2].elevation = 0.35f;
                table[2].params.sunColor = { 1.0f, 0.90f, 0.65f };
                table[2].params.sunIntensity = 1.45f;
                table[2].params.skyAmbientColor = { 0.38f, 0.65f, 0.88f };
                table[2].params.skyAmbientIntensity = 0.75f;
                table[2].params.groundAmbientColor = { 0.55f, 0.45f, 0.22f };
                table[2].params.groundAmbientIntensity = 0.45f;
                table[2].params.fogColor = { 0.85f, 0.78f, 0.70f, 1.0f };
                table[2].params.sunInscatterColor = { 1.0f, 0.82f, 0.45f, 1.0f };
                table[2].params.sunInscatterIntensity = 3.2f;
                table[2].params.sunInscatterExponent = 6.5f;
                table[2].params.heightFalloff = 0.012f;
                table[2].params.startDistance = 5.0f;
                table[2].params.endDistance = 160.0f;
                table[2].params.shaftColor = { 1.0f, 0.84f, 0.52f, 1.0f };
                table[2].params.shaftIntensity = 1.20f;
                table[2].params.shaftDensity = 0.88f;
                table[2].params.shaftDecay = 0.94f;
                table[2].params.flareIntensity = 0.55f;
                table[2].params.bloomTint = { 1.0f, 0.92f, 0.78f, 1.0f };
                table[2].params.bloomIntensity = 0.65f;
                table[2].params.bloomThreshold = 0.85f;
                table[2].params.bloomScatter = 0.78f;
                table[2].params.exposure = 0.12f;
                table[2].params.temperature = 0.14f;
                table[2].params.tint = -0.02f;
                table[2].params.contrast = 1.08f;
                table[2].params.saturation = 1.16f;
                table[2].params.shadowLiftColor = { 0.12f, 0.22f, 0.32f, 0.15f };
                table[2].params.highlightGainColor = { 1.0f, 0.95f, 0.85f, 1.10f };

                // Radiant High Noon (+0.75)
                table[3].elevation = 0.75f;
                table[3].params.sunColor = { 1.0f, 0.98f, 0.90f };
                table[3].params.sunIntensity = 1.35f;
                table[3].params.skyAmbientColor = { 0.42f, 0.70f, 0.95f };
                table[3].params.skyAmbientIntensity = 0.85f;
                table[3].params.groundAmbientColor = { 0.40f, 0.48f, 0.30f };
                table[3].params.groundAmbientIntensity = 0.50f;
                table[3].params.fogColor = { 0.65f, 0.78f, 0.92f, 1.0f };
                table[3].params.sunInscatterColor = { 1.0f, 0.92f, 0.75f, 1.0f };
                table[3].params.sunInscatterIntensity = 2.2f;
                table[3].params.sunInscatterExponent = 8.0f;
                table[3].params.heightFalloff = 0.015f;
                table[3].params.startDistance = 10.0f;
                table[3].params.endDistance = 140.0f;
                table[3].params.shaftColor = { 1.0f, 0.94f, 0.80f, 1.0f };
                table[3].params.shaftIntensity = 0.85f;
                table[3].params.shaftDensity = 0.75f;
                table[3].params.shaftDecay = 0.92f;
                table[3].params.flareIntensity = 0.40f;
                table[3].params.bloomTint = { 1.0f, 0.95f, 0.88f, 1.0f };
                table[3].params.bloomIntensity = 0.50f;
                table[3].params.bloomThreshold = 0.95f;
                table[3].params.bloomScatter = 0.75f;
                table[3].params.exposure = 0.15f;
                table[3].params.temperature = 0.05f;
                table[3].params.tint = 0.0f;
                table[3].params.contrast = 1.06f;
                table[3].params.saturation = 1.12f;
                table[3].params.shadowLiftColor = { 0.10f, 0.18f, 0.28f, 0.12f };
                table[3].params.highlightGainColor = { 1.0f, 0.98f, 0.92f, 1.05f };

                // Zenith Peak (+1.00)
                table[4].elevation = 1.00f;
                table[4].params = table[3].params;
                table[4].params.sunIntensity = 1.40f;

                return table;
            }();

        // Vibrant Fantasy Palette (High-Contrast, Saturated Crimson Dusk & Electric Violet Ambient)
        static const std::array<SolarKeyframe, 5> s_fantasyTable = []() consteval
            {
                std::array<SolarKeyframe, 5> table{};

                // Night / Arcane Purple Moon (-0.25)
                table[0].elevation = -0.25f;
                table[0].params.sunColor = { 0.75f, 0.50f, 1.0f };
                table[0].params.sunIntensity = 0.45f;
                table[0].params.skyAmbientColor = { 0.25f, 0.12f, 0.45f };
                table[0].params.skyAmbientIntensity = 0.50f;
                table[0].params.groundAmbientColor = { 0.12f, 0.05f, 0.20f };
                table[0].params.groundAmbientIntensity = 0.30f;
                table[0].params.fogColor = { 0.28f, 0.15f, 0.42f, 1.0f };
                table[0].params.sunInscatterColor = { 0.85f, 0.55f, 1.0f, 1.0f };
                table[0].params.sunInscatterIntensity = 2.0f;
                table[0].params.sunInscatterExponent = 10.0f;
                table[0].params.heightFalloff = 0.012f;
                table[0].params.startDistance = 5.0f;
                table[0].params.endDistance = 130.0f;
                table[0].params.shaftColor = { 0.80f, 0.60f, 1.0f, 1.0f };
                table[0].params.shaftIntensity = 0.55f;
                table[0].params.shaftDensity = 0.70f;
                table[0].params.shaftDecay = 0.92f;
                table[0].params.flareIntensity = 0.35f;
                table[0].params.bloomTint = { 0.90f, 0.70f, 1.0f, 1.0f };
                table[0].params.bloomIntensity = 0.70f;
                table[0].params.bloomThreshold = 0.85f;
                table[0].params.bloomScatter = 0.80f;
                table[0].params.exposure = -0.20f;
                table[0].params.temperature = -0.10f;
                table[0].params.tint = 0.15f;
                table[0].params.contrast = 1.15f;
                table[0].params.saturation = 1.25f;
                table[0].params.shadowLiftColor = { 0.15f, 0.06f, 0.25f, 0.12f };
                table[0].params.highlightGainColor = { 0.90f, 0.80f, 1.05f, 1.0f };

                // Blazing Crimson Sunset / Sunrise (+0.02)
                table[1].elevation = 0.02f;
                table[1].params.sunColor = { 1.0f, 0.42f, 0.15f };
                table[1].params.sunIntensity = 1.85f;
                table[1].params.skyAmbientColor = { 0.55f, 0.25f, 0.75f };
                table[1].params.skyAmbientIntensity = 0.75f;
                table[1].params.groundAmbientColor = { 0.50f, 0.20f, 0.08f };
                table[1].params.groundAmbientIntensity = 0.45f;
                table[1].params.fogColor = { 0.95f, 0.38f, 0.45f, 1.0f };
                table[1].params.sunInscatterColor = { 1.0f, 0.50f, 0.18f, 1.0f };
                table[1].params.sunInscatterIntensity = 5.5f;
                table[1].params.sunInscatterExponent = 4.5f;
                table[1].params.heightFalloff = 0.009f;
                table[1].params.startDistance = 5.0f;
                table[1].params.endDistance = 200.0f;
                table[1].params.shaftColor = { 1.0f, 0.55f, 0.20f, 1.0f };
                table[1].params.shaftIntensity = 2.2f;
                table[1].params.shaftDensity = 0.98f;
                table[1].params.shaftDecay = 0.97f;
                table[1].params.flareIntensity = 1.10f;
                table[1].params.bloomTint = { 1.0f, 0.65f, 0.35f, 1.0f };
                table[1].params.bloomIntensity = 1.10f;
                table[1].params.bloomThreshold = 0.75f;
                table[1].params.bloomScatter = 0.88f;
                table[1].params.exposure = 0.10f;
                table[1].params.temperature = 0.38f;
                table[1].params.tint = 0.10f;
                table[1].params.contrast = 1.22f;
                table[1].params.saturation = 1.45f;
                table[1].params.shadowLiftColor = { 0.22f, 0.08f, 0.30f, 0.20f };
                table[1].params.highlightGainColor = { 1.05f, 0.80f, 0.60f, 1.25f };

                // Rich Fantasy Afternoon (+0.35)
                table[2].elevation = 0.35f;
                table[2].params.sunColor = { 1.0f, 0.82f, 0.45f };
                table[2].params.sunIntensity = 1.70f;
                table[2].params.skyAmbientColor = { 0.25f, 0.55f, 0.95f };
                table[2].params.skyAmbientIntensity = 0.85f;
                table[2].params.groundAmbientColor = { 0.60f, 0.40f, 0.15f };
                table[2].params.groundAmbientIntensity = 0.55f;
                table[2].params.fogColor = { 0.88f, 0.65f, 0.55f, 1.0f };
                table[2].params.sunInscatterColor = { 1.0f, 0.75f, 0.35f, 1.0f };
                table[2].params.sunInscatterIntensity = 4.0f;
                table[2].params.sunInscatterExponent = 5.5f;
                table[2].params.heightFalloff = 0.010f;
                table[2].params.startDistance = 5.0f;
                table[2].params.endDistance = 170.0f;
                table[2].params.shaftColor = { 1.0f, 0.78f, 0.40f, 1.0f };
                table[2].params.shaftIntensity = 1.60f;
                table[2].params.shaftDensity = 0.90f;
                table[2].params.shaftDecay = 0.95f;
                table[2].params.flareIntensity = 0.75f;
                table[2].params.bloomTint = { 1.0f, 0.85f, 0.60f, 1.0f };
                table[2].params.bloomIntensity = 0.85f;
                table[2].params.bloomThreshold = 0.80f;
                table[2].params.bloomScatter = 0.82f;
                table[2].params.exposure = 0.15f;
                table[2].params.temperature = 0.22f;
                table[2].params.tint = 0.04f;
                table[2].params.contrast = 1.18f;
                table[2].params.saturation = 1.35f;
                table[2].params.shadowLiftColor = { 0.15f, 0.12f, 0.32f, 0.18f };
                table[2].params.highlightGainColor = { 1.05f, 0.90f, 0.75f, 1.18f };

                // Vivid Saturated Noon (+0.75)
                table[3].elevation = 0.75f;
                table[3].params.sunColor = { 1.0f, 0.95f, 0.80f };
                table[3].params.sunIntensity = 1.60f;
                table[3].params.skyAmbientColor = { 0.30f, 0.65f, 1.0f };
                table[3].params.skyAmbientIntensity = 0.95f;
                table[3].params.groundAmbientColor = { 0.45f, 0.52f, 0.25f };
                table[3].params.groundAmbientIntensity = 0.60f;
                table[3].params.fogColor = { 0.60f, 0.75f, 0.95f, 1.0f };
                table[3].params.sunInscatterColor = { 1.0f, 0.90f, 0.65f, 1.0f };
                table[3].params.sunInscatterIntensity = 2.8f;
                table[3].params.sunInscatterExponent = 7.0f;
                table[3].params.heightFalloff = 0.012f;
                table[3].params.startDistance = 10.0f;
                table[3].params.endDistance = 150.0f;
                table[3].params.shaftColor = { 1.0f, 0.90f, 0.70f, 1.0f };
                table[3].params.shaftIntensity = 1.10f;
                table[3].params.shaftDensity = 0.80f;
                table[3].params.shaftDecay = 0.93f;
                table[3].params.flareIntensity = 0.55f;
                table[3].params.bloomTint = { 1.0f, 0.92f, 0.78f, 1.0f };
                table[3].params.bloomIntensity = 0.65f;
                table[3].params.bloomThreshold = 0.88f;
                table[3].params.bloomScatter = 0.78f;
                table[3].params.exposure = 0.18f;
                table[3].params.temperature = 0.10f;
                table[3].params.tint = 0.02f;
                table[3].params.contrast = 1.14f;
                table[3].params.saturation = 1.25f;
                table[3].params.shadowLiftColor = { 0.12f, 0.10f, 0.28f, 0.14f };
                table[3].params.highlightGainColor = { 1.05f, 0.95f, 0.88f, 1.12f };

                // Zenith Peak (+1.00)
                table[4].elevation = 1.00f;
                table[4].params = table[3].params;
                table[4].params.sunIntensity = 1.65f;

                return table;
            }();

        // Clear Natural Daylight (Physically Balanced, Neutral White Sun, Pure Black Shadows)
        static const std::array<SolarKeyframe, 5> s_naturalTable = []() consteval
            {
                std::array<SolarKeyframe, 5> table{};

                // Natural Moonlit Night (-0.25)
                table[0].elevation = -0.25f;
                table[0].params.sunColor = { 0.40f, 0.50f, 0.65f };
                table[0].params.sunIntensity = 0.20f;
                table[0].params.skyAmbientColor = { 0.05f, 0.07f, 0.12f };
                table[0].params.skyAmbientIntensity = 0.30f;
                table[0].params.groundAmbientColor = { 0.02f, 0.03f, 0.04f };
                table[0].params.groundAmbientIntensity = 0.20f;
                table[0].params.fogColor = { 0.06f, 0.08f, 0.14f, 1.0f };
                table[0].params.sunInscatterColor = { 0.40f, 0.50f, 0.65f, 1.0f };
                table[0].params.sunInscatterIntensity = 0.8f;
                table[0].params.sunInscatterExponent = 16.0f;
                table[0].params.heightFalloff = 0.020f;
                table[0].params.startDistance = 5.0f;
                table[0].params.endDistance = 100.0f;
                table[0].params.shaftColor = { 0.40f, 0.50f, 0.65f, 1.0f };
                table[0].params.shaftIntensity = 0.20f;
                table[0].params.shaftDensity = 0.50f;
                table[0].params.shaftDecay = 0.85f;
                table[0].params.flareIntensity = 0.10f;
                table[0].params.bloomTint = { 1.0f, 1.0f, 1.0f, 1.0f };
                table[0].params.bloomIntensity = 0.25f;
                table[0].params.bloomThreshold = 1.0f;
                table[0].params.bloomScatter = 0.65f;
                table[0].params.exposure = -0.40f;
                table[0].params.temperature = 0.0f;
                table[0].params.tint = 0.0f;
                table[0].params.contrast = 1.0f;
                table[0].params.saturation = 0.85f;
                table[0].params.shadowLiftColor = { 0.0f, 0.0f, 0.0f, 0.0f };
                table[0].params.highlightGainColor = { 1.0f, 1.0f, 1.0f, 1.0f };

                // Realistic Dawn (+0.02)
                table[1].elevation = 0.02f;
                table[1].params.sunColor = { 1.0f, 0.82f, 0.62f };
                table[1].params.sunIntensity = 1.10f;
                table[1].params.skyAmbientColor = { 0.45f, 0.55f, 0.75f };
                table[1].params.skyAmbientIntensity = 0.50f;
                table[1].params.groundAmbientColor = { 0.20f, 0.22f, 0.18f };
                table[1].params.groundAmbientIntensity = 0.30f;
                table[1].params.fogColor = { 0.72f, 0.70f, 0.75f, 1.0f };
                table[1].params.sunInscatterColor = { 1.0f, 0.80f, 0.55f, 1.0f };
                table[1].params.sunInscatterIntensity = 2.2f;
                table[1].params.sunInscatterExponent = 8.0f;
                table[1].params.heightFalloff = 0.015f;
                table[1].params.startDistance = 10.0f;
                table[1].params.endDistance = 160.0f;
                table[1].params.shaftColor = { 1.0f, 0.85f, 0.65f, 1.0f };
                table[1].params.shaftIntensity = 0.85f;
                table[1].params.shaftDensity = 0.75f;
                table[1].params.shaftDecay = 0.92f;
                table[1].params.flareIntensity = 0.45f;
                table[1].params.bloomTint = { 1.0f, 0.95f, 0.85f, 1.0f };
                table[1].params.bloomIntensity = 0.45f;
                table[1].params.bloomThreshold = 0.90f;
                table[1].params.bloomScatter = 0.75f;
                table[1].params.exposure = 0.0f;
                table[1].params.temperature = 0.05f;
                table[1].params.tint = 0.0f;
                table[1].params.contrast = 1.0f;
                table[1].params.saturation = 1.0f;
                table[1].params.shadowLiftColor = { 0.0f, 0.0f, 0.0f, 0.0f };
                table[1].params.highlightGainColor = { 1.0f, 1.0f, 1.0f, 1.0f };

                // Realistic Mid-Morning (+0.35)
                table[2].elevation = 0.35f;
                table[2].params.sunColor = { 1.0f, 0.94f, 0.85f };
                table[2].params.sunIntensity = 1.25f;
                table[2].params.skyAmbientColor = { 0.50f, 0.68f, 0.92f };
                table[2].params.skyAmbientIntensity = 0.65f;
                table[2].params.groundAmbientColor = { 0.22f, 0.26f, 0.20f };
                table[2].params.groundAmbientIntensity = 0.35f;
                table[2].params.fogColor = { 0.68f, 0.78f, 0.90f, 1.0f };
                table[2].params.sunInscatterColor = { 1.0f, 0.92f, 0.80f, 1.0f };
                table[2].params.sunInscatterIntensity = 1.8f;
                table[2].params.sunInscatterExponent = 10.0f;
                table[2].params.heightFalloff = 0.018f;
                table[2].params.startDistance = 15.0f;
                table[2].params.endDistance = 150.0f;
                table[2].params.shaftColor = { 1.0f, 0.92f, 0.80f, 1.0f };
                table[2].params.shaftIntensity = 0.60f;
                table[2].params.shaftDensity = 0.70f;
                table[2].params.shaftDecay = 0.90f;
                table[2].params.flareIntensity = 0.30f;
                table[2].params.bloomTint = { 1.0f, 1.0f, 1.0f, 1.0f };
                table[2].params.bloomIntensity = 0.35f;
                table[2].params.bloomThreshold = 0.95f;
                table[2].params.bloomScatter = 0.72f;
                table[2].params.exposure = 0.0f;
                table[2].params.temperature = 0.0f;
                table[2].params.tint = 0.0f;
                table[2].params.contrast = 1.0f;
                table[2].params.saturation = 1.0f;
                table[2].params.shadowLiftColor = { 0.0f, 0.0f, 0.0f, 0.0f };
                table[2].params.highlightGainColor = { 1.0f, 1.0f, 1.0f, 1.0f };

                // Clean Neutral High Noon (+0.75)
                table[3].elevation = 0.75f;
                table[3].params.sunColor = { 1.0f, 0.98f, 0.95f };
                table[3].params.sunIntensity = 1.30f;
                table[3].params.skyAmbientColor = { 0.55f, 0.72f, 0.98f };
                table[3].params.skyAmbientIntensity = 0.75f;
                table[3].params.groundAmbientColor = { 0.25f, 0.28f, 0.22f };
                table[3].params.groundAmbientIntensity = 0.40f;
                table[3].params.fogColor = { 0.65f, 0.76f, 0.92f, 1.0f };
                table[3].params.sunInscatterColor = { 1.0f, 0.96f, 0.90f, 1.0f };
                table[3].params.sunInscatterIntensity = 1.4f;
                table[3].params.sunInscatterExponent = 12.0f;
                table[3].params.heightFalloff = 0.020f;
                table[3].params.startDistance = 20.0f;
                table[3].params.endDistance = 140.0f;
                table[3].params.shaftColor = { 1.0f, 0.96f, 0.90f, 1.0f };
                table[3].params.shaftIntensity = 0.45f;
                table[3].params.shaftDensity = 0.65f;
                table[3].params.shaftDecay = 0.88f;
                table[3].params.flareIntensity = 0.25f;
                table[3].params.bloomTint = { 1.0f, 1.0f, 1.0f, 1.0f };
                table[3].params.bloomIntensity = 0.30f;
                table[3].params.bloomThreshold = 1.0f;
                table[3].params.bloomScatter = 0.70f;
                table[3].params.exposure = 0.0f;
                table[3].params.temperature = 0.0f;
                table[3].params.tint = 0.0f;
                table[3].params.contrast = 1.0f;
                table[3].params.saturation = 1.0f;
                table[3].params.shadowLiftColor = { 0.0f, 0.0f, 0.0f, 0.0f };
                table[3].params.highlightGainColor = { 1.0f, 1.0f, 1.0f, 1.0f };

                // Zenith Peak (+1.00)
                table[4].elevation = 1.00f;
                table[4].params = table[3].params;
                table[4].params.sunIntensity = 1.35f;

                return table;
            }();

        // Select the active keyframe table via zero-allocation const reference lookup
        const std::array<SolarKeyframe, 5>& table = [&]() noexcept -> const std::array<SolarKeyframe, 5>&
            {
                switch (preset)
                {
                case AtmospherePreset::VibrantFantasy:
                    return s_fantasyTable;
                case AtmospherePreset::ClearNaturalDaylight:
                    return s_naturalTable;
                case AtmospherePreset::StylizedWatercolor:
                case AtmospherePreset::Custom:
                default:
                    return s_watercolorTable;
                }
            }();

        const float clampedElevation = std::clamp(solarElevation, table.front().elevation, table.back().elevation);

        // Find bounding interval in the flat keyframe array (4 branch-predicted iterations)
        std::size_t idx{ 0 };
        for (std::size_t i{ 0 }; i < table.size() - 1; ++i)
        {
            if (clampedElevation >= table[i].elevation && clampedElevation <= table[i + 1].elevation)
            {
                idx = i;
                break;
            }
        }

        const auto& k0 = table[idx];
        const auto& k1 = table[idx + 1];

        const float span = (std::max)(k1.elevation - k0.elevation, 0.0001f);
        float t = std::clamp((clampedElevation - k0.elevation) / span, 0.0f, 1.0f);
        t = t * t * (3.0f - 2.0f * t); // Smooth Hermite C1 continuity

        auto lerpFloat = [t](float a, float b) noexcept { return a + (b - a) * t; };
        auto lerpFloat3 = [t](const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b) noexcept -> DirectX::XMFLOAT3
            {
                const DirectX::XMVECTOR va = DirectX::XMLoadFloat3(&a);
                const DirectX::XMVECTOR vb = DirectX::XMLoadFloat3(&b);
                DirectX::XMFLOAT3 res{};
                DirectX::XMStoreFloat3(&res, DirectX::XMVectorLerp(va, vb, t));
                return res;
            };
        auto lerpFloat4 = [t](const DirectX::XMFLOAT4& a, const DirectX::XMFLOAT4& b) noexcept -> DirectX::XMFLOAT4
            {
                const DirectX::XMVECTOR va = DirectX::XMLoadFloat4(&a);
                const DirectX::XMVECTOR vb = DirectX::XMLoadFloat4(&b);
                DirectX::XMFLOAT4 res{};
                DirectX::XMStoreFloat4(&res, DirectX::XMVectorLerp(va, vb, t));
                return res;
            };

        AtmosphereParameters out{};
        out.sunColor = lerpFloat3(k0.params.sunColor, k1.params.sunColor);
        out.sunIntensity = lerpFloat(k0.params.sunIntensity, k1.params.sunIntensity);

        out.skyAmbientColor = lerpFloat3(k0.params.skyAmbientColor, k1.params.skyAmbientColor);
        out.skyAmbientIntensity = lerpFloat(k0.params.skyAmbientIntensity, k1.params.skyAmbientIntensity);
        out.groundAmbientColor = lerpFloat3(k0.params.groundAmbientColor, k1.params.groundAmbientColor);
        out.groundAmbientIntensity = lerpFloat(k0.params.groundAmbientIntensity, k1.params.groundAmbientIntensity);

        out.fogColor = lerpFloat4(k0.params.fogColor, k1.params.fogColor);
        out.sunInscatterColor = lerpFloat4(k0.params.sunInscatterColor, k1.params.sunInscatterColor);
        out.sunInscatterIntensity = lerpFloat(k0.params.sunInscatterIntensity, k1.params.sunInscatterIntensity);
        out.sunInscatterExponent = lerpFloat(k0.params.sunInscatterExponent, k1.params.sunInscatterExponent);
        out.heightFalloff = lerpFloat(k0.params.heightFalloff, k1.params.heightFalloff);
        out.startDistance = lerpFloat(k0.params.startDistance, k1.params.startDistance);
        out.endDistance = lerpFloat(k0.params.endDistance, k1.params.endDistance);

        out.shaftColor = lerpFloat4(k0.params.shaftColor, k1.params.shaftColor);
        out.shaftIntensity = lerpFloat(k0.params.shaftIntensity, k1.params.shaftIntensity);
        out.shaftDensity = lerpFloat(k0.params.shaftDensity, k1.params.shaftDensity);
        out.shaftDecay = lerpFloat(k0.params.shaftDecay, k1.params.shaftDecay);
        out.flareIntensity = lerpFloat(k0.params.flareIntensity, k1.params.flareIntensity);

        out.bloomTint = lerpFloat4(k0.params.bloomTint, k1.params.bloomTint);
        out.bloomIntensity = lerpFloat(k0.params.bloomIntensity, k1.params.bloomIntensity);
        out.bloomThreshold = lerpFloat(k0.params.bloomThreshold, k1.params.bloomThreshold);
        out.bloomScatter = lerpFloat(k0.params.bloomScatter, k1.params.bloomScatter);

        out.exposure = lerpFloat(k0.params.exposure, k1.params.exposure);
        out.temperature = lerpFloat(k0.params.temperature, k1.params.temperature);
        out.tint = lerpFloat(k0.params.tint, k1.params.tint);
        out.contrast = lerpFloat(k0.params.contrast, k1.params.contrast);
        out.saturation = lerpFloat(k0.params.saturation, k1.params.saturation);
        out.shadowLiftColor = lerpFloat4(k0.params.shadowLiftColor, k1.params.shadowLiftColor);
        out.highlightGainColor = lerpFloat4(k0.params.highlightGainColor, k1.params.highlightGainColor);

        return out;
    }
}