#pragma once
#include "IBossAttackPattern.h"
#include "Bullet.h"
#include <vector>
#include <memory>

// ============================================================
// IPooledAttackPattern
//
// Extended interface for attack patterns that operate on a
// shared bullet pool owned by the parent phase.
//
// Use this for Phase 1 attacks (no OS windows, shared pool).
// Use IBossAttackPattern directly for Phase 2 attacks
// (self-owned bullets + OS windows).
// ============================================================
class IPooledAttackPattern : public IBossAttackPattern {
public:
    virtual ~IPooledAttackPattern() = default;

    // Called instead of Start(boss) when a shared pool is available.
    // Implementations should store the pointer and use it in Update().
    virtual void StartPooled(Boss* boss, std::vector<std::unique_ptr<Bullet>>* pool) = 0;

    // Base Start() should not be used directly for pooled patterns.
    // Provided as a no-op fallback to satisfy IBossAttackPattern.
    void Start(Boss* boss) override {}
};