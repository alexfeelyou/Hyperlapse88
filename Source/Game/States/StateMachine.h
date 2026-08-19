#pragma once
#include <memory>
#include "PlayerState.h"

class StateMachine
{
public:
    // currentState cleaned up automatically by unique_ptr
    ~StateMachine() = default;

    // Takes ownership of startState and immediately enters it
    void Initialize(std::unique_ptr<PlayerState> startState, Player* player)
    {
        currentState = std::move(startState);
        currentState->Enter(player);
    }

    // Exits current state, takes ownership of newState, enters it
    void ChangeState(Player* player, std::unique_ptr<PlayerState> newState)
    {
        if (currentState)
            currentState->Exit(player);

        currentState = std::move(newState);
        currentState->Enter(player);
    }

    void Update(Player* player, float elapsedTime)
    {
        if (currentState)
            currentState->Update(player, elapsedTime);
    }

private:
    std::unique_ptr<PlayerState> currentState;
};