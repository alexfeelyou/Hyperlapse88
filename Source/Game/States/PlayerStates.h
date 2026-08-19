#pragma once
#include "PlayerState.h"
#include "PlayerConstants.h"
#include <DirectXMath.h>

class Player;

// ============================================================
// STATE DECLARATIONS
// Implementations live in PlayerStates.cpp
// ============================================================

class PlayerIdle : public PlayerState
{
public:
    void Enter(Player* player) override;
    void Update(Player* player, float dt) override;
    void Exit(Player* player) override {}
};

class PlayerMoving : public PlayerState
{
public:
    void Enter(Player* player) override;
    void Update(Player* player, float dt) override;
    void Exit(Player* player) override {}
};

class PlayerDash : public PlayerState
{
public:
    void Enter(Player* player) override;
    void Update(Player* player, float dt) override;
    void Exit(Player* player) override;
private:
    float timer = 0.0f;
    DirectX::XMFLOAT2 dashDir = { 0.0f, 0.0f };
    int m_dashGoVfxHandle = -1;
};

class PlayerSlash : public PlayerState
{
public:
    void Enter(Player* player) override;
    void Update(Player* player, float dt) override;
    void Exit(Player* player) override;
private:
    float timer = PlayerConst::SlashDuration;
};

class PlayerParry : public PlayerState
{
public:
    void Enter(Player* player) override;
    void Update(Player* player, float dt) override;
    void Exit(Player* player) override;
private:
    float timer = PlayerConst::ParryDuration;
};

class PlayerShoot : public PlayerState
{
public:
    void Enter(Player* player) override;
    void Update(Player* player, float dt) override;
    void Exit(Player* player) override;
private:
    void PerformShootInternal(Player* player, bool isHeld = false);

    float m_timer{ 0.0f };
    float m_minTapCooldown{ 0.0f };
};

// TODO: implement damage and death behaviour
class PlayerDamage : public PlayerState
{
public:
    void Enter(Player* player) override {}
    void Update(Player* player, float dt) override {}
    void Exit(Player* player) override {}
};

class PlayerDead : public PlayerState
{
public:
    void Enter(Player* player) override {}
    void Update(Player* player, float dt) override {}
    void Exit(Player* player) override {}
};