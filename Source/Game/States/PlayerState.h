#pragma once
#include <string>

// Forward Declaration agar tidak circular dependency
class Player;

// --- 1. Abstract Base State ---
class PlayerState
{
public:
    virtual ~PlayerState() = default;

    // Dipanggil sekali saat masuk state (tempat play animasi)
    virtual void Enter(Player* player) = 0;

    // Logic per frame (input handling, check transition)
    virtual void Update(Player* player, float elapsedTime) = 0;

    // Dipanggil sekali saat keluar state (cleanup jika perlu)
    virtual void Exit(Player* player) = 0;
};