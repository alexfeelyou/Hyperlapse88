#pragma once

#include <memory>
#include "PlayerState.h"

// Forward declaration of the new Brain component
class PlayerControllerComponent;

class StateMachine
{
public:
    StateMachine() noexcept = default;
    ~StateMachine() = default;

    // Delete copy/move to enforce strict ownership
    StateMachine(const StateMachine&) = delete;
    StateMachine& operator=(const StateMachine&) = delete;

    // Takes ownership of startState and immediately enters it
    void Initialize(std::unique_ptr<PlayerState> startState, PlayerControllerComponent* controller) noexcept
    {
        m_currentState = std::move(startState);
        if (m_currentState)
        {
            m_currentState->Enter(controller);
        }
    }

    // Exits current state, takes ownership of newState, enters it
    void ChangeState(PlayerControllerComponent* controller, std::unique_ptr<PlayerState> newState) noexcept
    {
        // Move to temporary to prevent recursive dangling pointers if Exit() triggers a state change
        std::unique_ptr<PlayerState> oldState{ std::move(m_currentState) };
        if (oldState)
        {
            oldState->Exit(controller);
        }

        m_currentState = std::move(newState);
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
    std::unique_ptr<PlayerState> m_currentState{};
};