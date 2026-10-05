#pragma once

#include "PlayerState.h"

// Forward declaration 
class PlayerControllerComponent;

class StateMachine
{
public:
    StateMachine() noexcept = default;
    ~StateMachine() = default;

    StateMachine(const StateMachine&) = delete;
    StateMachine& operator=(const StateMachine&) = delete;

    // Use raw pointers. The Controller owns the memory, we just point to it.
    void Initialize(PlayerState* startState, PlayerControllerComponent* controller) noexcept
    {
        m_currentState = startState;
        if (m_currentState)
        {
            m_currentState->Enter(controller);
        }
    }

    void ChangeState(PlayerControllerComponent* controller, PlayerState* newState) noexcept
    {
        if (m_currentState)
        {
            m_currentState->Exit(controller);
        }

        m_currentState = newState;
        if (m_currentState)
        {
            m_currentState->Enter(controller);
        }
    }

    void Update(PlayerControllerComponent* controller, float elapsedTime) noexcept
    {
        if (m_currentState)
        {
            m_currentState->Update(controller, elapsedTime);
        }
    }

private:
    // Only pointing to active state, memory is safely managed in Controller component
    PlayerState* m_currentState{ nullptr };
};