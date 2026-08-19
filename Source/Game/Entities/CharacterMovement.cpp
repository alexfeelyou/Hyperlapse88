#include "CharacterMovement.h"
#include <cmath>

bool CharacterMovement::IsMoving() const
{
    return (fabsf(velocity.x) > 0.01f || fabsf(velocity.z) > 0.01f);
}