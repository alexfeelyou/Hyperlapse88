#pragma once

// ============================================================
// PLAYER CONSTANTS
// All tunable values for Player, PlayerStates, and related
// systems live here. Change a number once, it applies everywhere.
// ============================================================

namespace PlayerConst
{
    // --- Movement ---
    inline constexpr float MoveSpeed = 15.0f;  // Default walk speed (units/s)
    inline constexpr float Acceleration = 8.0f;   // Input ramp-up rate
    inline constexpr float Deceleration = 12.0f;  // Input ramp-down rate
    inline constexpr float RotSmoothSpeed = 15.0f;  // Foot yaw lerp speed (rad/s)
    inline constexpr float Gravity = -9.81f; // Applied per-frame via PhysX

    // --- Capsule (PhysX controller descriptor) ---
    inline constexpr float CapsuleHeight = 2.0f;
    inline constexpr float CapsuleRadius = 0.5f;
    inline constexpr float CapsuleStep = 0.5f;
    inline constexpr float CapsuleHalfHeight = 1.5f;  // Subtracted from PxPos.y to get feet pos
    inline constexpr float PhysXMinDist = 0.001f; // move() minDist parameter

    // --- Dash ---
    inline constexpr float DashSpeed = 45.0f;  // Burst speed during dash (units/s)
    inline constexpr float DashDuration = 0.15f;  // How long the dash lasts (s)
    inline constexpr float DashCooldown = 1.0f;   // Time before dash can be used again (s)

    // --- Slash ---
    inline constexpr float SlashLungeForce = 40.0f;  // Initial velocity burst on slash enter
    inline constexpr float SlashDrag = 0.7f;   // Velocity multiplier applied per frame during slash
    inline constexpr float SlashDuration = 0.15f;  // State timer (s)

    // --- Parry ---
    inline constexpr float ParryDuration = 0.2f;   // Active window (s)

    // --- Shoot ---
    inline constexpr float ShootDuration = 0.15f;  // State lock duration (s)

    // --- Aiming ---
    inline constexpr float AimMinDistSq = 0.0f;   // Minimum sq-distance to aim target before clamping
    inline constexpr float MaxTorsoAngle = 1.5707963f; // XM_PIDIV2 — max upper-body twist (rad)

    // --- Projectile (player-fired) ---
    inline constexpr float BulletSpeed = 50.0f;  // Speed of player bullets (units/s)
    inline constexpr float BulletSpawnFwd = 1.5f;   // Spawn offset forward from player center
    inline constexpr float BulletSpawnY = 1.0f;   // Spawn height offset (chest level)
    inline constexpr int   MaxBullets = 150;      // Hard cap on active player projectiles

    // --- Animation blend times ---
    inline constexpr float AnimBlendDefault = 0.2f;   // Standard cross-fade duration (s)
}