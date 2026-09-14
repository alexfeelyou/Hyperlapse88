#pragma once

// Forward declaration 
class PlayerControllerComponent;

class PlayerState
{
public:
    // Virtual destructor is mandatory for polymorphic base classes
    virtual ~PlayerState() = default;

    // All state hooks on the Controller, not the physical body
    virtual void Enter(PlayerControllerComponent* controller) = 0;
    virtual void Update(PlayerControllerComponent* controller, float elapsedTime) = 0;
    virtual void Exit(PlayerControllerComponent* controller) = 0;
};