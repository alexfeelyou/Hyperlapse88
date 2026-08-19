#pragma once
#include <vector>

class Boss;
class Bullet;
class Camera;
struct ID3D11DeviceContext;

// Base interface for all modular boss attacks to enforce SRP and DRY principles.
class IBossAttackPattern {
public:
    virtual ~IBossAttackPattern() = default;

    // Initializes resources, tracking windows, and VFX for the attack.
    virtual void Start(Boss* boss) = 0;

    // Processes bullet physics, window tracking, and spawning logic.
    virtual void Update(float dt, Boss* boss) = 0;

    // Renders specific 3D models or UI related to this attack.
    virtual void Render(ID3D11DeviceContext* context, Camera* camera, Boss* boss) = 0;

    // Cleans up tracking windows and VFX if the attack is forcefully stopped.
    virtual void Stop(Boss* boss) = 0;

    // Returns true when all projectiles are destroyed and the attack is over.
    virtual bool IsFinished() const = 0;

    // Returns active bullets for the global collision manager.
    virtual std::vector<Bullet*> GetActiveProjectiles() const = 0;
};