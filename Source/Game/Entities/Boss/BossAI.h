#pragma once
#include <memory>
#include <vector>
#include <DirectXMath.h>

// --- Phase 1 Attacks ---
#include "AttackRadial.h"
#include "AttackFan.h"
#include "AttackPhalanx.h"
#include "AttackRain.h"
#include "AttackUltimate.h"
#include "AttackWave.h"
#include "AttackMeteor.h"
#include "AttackDirect.h"

// --- Phase 2 Attacks ---
#include "AttackBouncing.h"
#include "AttackBoomerangs.h"
#include "AttackBlasters.h"
#include "AttackSpears.h"

class BossPhase01;
class BossPhase02;
class Boss;
class Player;

// ========================================================
// BOSS AI - PHASE 01
// ========================================================
class BossAI_Phase01 {
public:
    // Enum tetap dipertahankan
    enum class AttackSequence {
        Radial, RadialContinuos, Fan, FanContinuos,
        Phalanx, Rain, Ultimate, Wave, Meteor, Direct
    };

    BossAI_Phase01(BossPhase01* phase, Player* target);
    void Update(float dt, Boss* boss);

    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }
    void SetTarget(Player* target) { m_target = target; }

private:
    AttackSequence GetNextTacticianAttack();

    BossPhase01* m_phase = nullptr;
    Player* m_target = nullptr;
    bool         m_enabled = false;

    // ========================================================
    // [DIUBAH] Tracker untuk Tactician Mode (HP > 50%)
    // ========================================================
    int  m_mainAttackIndex = 0;     // 0: Wave, 1: Phalanx, 2: Meteor, 3: Rain (Targeted)
    bool m_isNextMainAttack = true; // Karena dimulai dari Radial (Filler), next = Main Attack
    // [DIUBAH] Filler kini hanya Radial; m_fillerAttackIndex & rotasi Direct/Fan dihapus dari pemakaian aktif

    // ========================================================
    // [BARU] Tracker untuk Chaos Mode (HP <= 50%)
    // ========================================================
    bool m_isEnraged = false;       // Flag untuk mendeteksi awal transisi Phase 2
    int  m_enragedMainIndex = 1;    // 0: Phalanx, 1: Wave, 2: Meteor, 3: Ultimate

    // Default mulai dari filler Radial
    AttackSequence m_currentAttack = AttackSequence::Radial;
    float m_cooldownTimer = 2.0f;
};

// ========================================================
// BOSS AI - PHASE 02 (WINDOWKILL)
// ========================================================
class BossAI_Phase02 {
public:

    enum class AttackSequence {
        Bouncing,
        Boomerang,
        Blaster,
        Spear
    };

    BossAI_Phase02(BossPhase02* phase, Player* target);
    void Update(float dt, Boss* boss);

    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }
    void SetTarget(Player* target) { m_target = target; }

    // CATATAN: Semua fungsi Get...Params() DIHAPUS karena sudah pindah ke ParamManager

private:
    BossPhase02* m_phase = nullptr;
    Player* m_target = nullptr;
    bool          m_enabled = false;

    // Sistem Antrean (Sequence)
    AttackSequence m_currentAttack = AttackSequence::Bouncing;
    float m_cooldownTimer = 3.0f; // Jeda awal saat masuk fase
};